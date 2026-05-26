#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Create an Apollo Lake/Gemini Lake IFWI image from build inputs."""

import argparse
import struct
import sys
from pathlib import Path


KiB = 1024
BPDT_SIGNATURE = 0x55AA
BPDT_HEADER_SIZE = 24
BPDT_ENTRY_SIZE = 12
CPD_HEADER_SIZE = 16
CPD_ENTRY_SIZE = 24


BPDT_DLMP = 9
BPDT_IFP_OVERRIDE = 10
BPDT_S_BPDT = 5
BPDT_RBEP = 1
BPDT_UFS_PHY = 12
BPDT_UFS_GPP = 13
BPDT_FTPR = 2
BPDT_UEP = 17
BPDT_SMIP = 0
BPDT_PMCP = 14
BPDT_UCOD = 3
BPDT_IBBP = 4
BPDT_DEBUG_TOKENS = 11
BPDT_NFTP = 7
BPDT_OBBP = 6


def align_up(value, align):
	if value % align:
		value += align - value % align
	return value


def read_file(filename):
	return Path(filename).read_bytes()


def padded(data, align=4 * KiB):
	size = align_up(len(data), align)
	return data + b"\xff" * (size - len(data))


def cpd_checksum(header):
	data = bytearray(header)
	data[11] = 0
	return (-sum(data)) & 0xff


def cpd_header(name, entries):
	header = bytearray()
	header += b"$CPD"
	header += struct.pack("<I", len(entries))
	header += bytes([1, 1, CPD_HEADER_SIZE, 0])
	header += name.encode("ascii")
	for entry_name, offset, length in entries:
		header += entry_name.encode("ascii").ljust(12, b"\x00")
		header += struct.pack("<III", offset, length, 0)
	header[11] = cpd_checksum(header)
	return bytes(header)


def create_cpd(name, payloads):
	offset = CPD_HEADER_SIZE + len(payloads) * CPD_ENTRY_SIZE
	entries = []
	for entry_name, payload in payloads:
		entries.append((entry_name, offset, len(payload)))
		offset += len(payload)

	out = bytearray(b"\xff" * align_up(offset, 4 * KiB))
	out[:CPD_HEADER_SIZE + len(payloads) * CPD_ENTRY_SIZE] = cpd_header(name, entries)

	for (_, payload), (_, offset, _) in zip(payloads, entries):
		out[offset:offset + len(payload)] = payload

	return bytes(out)


def parse_cpd(data, name):
	if data[:4] != b"$CPD":
		raise ValueError(f"{name}: missing CPD header")

	count = struct.unpack_from("<I", data, 4)[0]
	header_version, entry_version, header_len = data[8], data[9], data[10]
	if header_version != 1 or entry_version != 1 or header_len != CPD_HEADER_SIZE:
		raise ValueError(f"{name}: unsupported CPD header")

	entries = []
	offset = header_len
	for _ in range(count):
		entry_name = data[offset:offset + 12].rstrip(b"\x00").decode("ascii")
		entry_offset, length, _ = struct.unpack_from("<III", data, offset + 12)
		entries.append([entry_name, entry_offset, length])
		offset += CPD_ENTRY_SIZE

	return entries


def patch_cpd_entries(data, replacements):
	out = bytearray(data)
	entries = parse_cpd(out, "FTPR")

	end = 0
	for _, offset, length in entries:
		if length:
			end = max(end, offset + length)
	end = align_up(end, 4 * KiB)

	for entry_name, payload in replacements:
		for entry in entries:
			if entry[0] == entry_name:
				entry[1] = end
				entry[2] = len(payload)
				break
		else:
			raise ValueError(f"FTPR: missing CPD entry {entry_name}")

		new_end = end + len(payload)
		if len(out) < new_end:
			out.extend(b"\xff" * (new_end - len(out)))
		out[end:new_end] = payload
		end = new_end

	out.extend(b"\xff" * (align_up(len(out), 4 * KiB) - len(out)))
	out[:CPD_HEADER_SIZE + len(entries) * CPD_ENTRY_SIZE] = cpd_header(
		"FTPR", [(name, offset, length) for name, offset, length in entries])
	return bytes(out)


def parse_fpt_partition(cse_image, name):
	for fpt_offset in (0x10, 0):
		if cse_image[fpt_offset:fpt_offset + 4] == b"$FPT":
			break
	else:
		raise ValueError("CSE image does not contain an FPT")

	count = struct.unpack_from("<I", cse_image, fpt_offset + 4)[0]
	header_len = cse_image[fpt_offset + 10]
	offset = fpt_offset + header_len

	for _ in range(count):
		entry_name = cse_image[offset:offset + 4].decode("ascii")
		entry_offset, length = struct.unpack_from("<II", cse_image, offset + 8)
		flags = struct.unpack_from("<I", cse_image, offset + 28)[0]
		valid = entry_offset != 0 and length != 0 and ((flags >> 24) & 0xff) != 0xff
		if entry_name == name:
			if not valid:
				raise ValueError(f"CSE FPT partition {name} is invalid")
			return cse_image[entry_offset:entry_offset + length]
		offset += 32

	raise ValueError(f"CSE FPT partition {name} not found")


def parse_bios_partition(bios, name):
	if bios[:4] != b"BIOS":
		raise ValueError("BIOS manifest does not start with BIOS")

	count = struct.unpack_from("<I", bios, 4)[0]
	offset = 0x10
	for _ in range(count):
		entry_name = bios[offset:offset + 4].decode("ascii")
		entry_offset, length, _ = struct.unpack_from("<III", bios, offset + 4)
		if entry_name == name:
			return bios[entry_offset:entry_offset + length]
		offset += 16

	raise ValueError(f"BIOS partition {name} not found")


def parse_ifd_region(descriptor, region):
	reg_offset = 0x40 + region * 4
	value = struct.unpack_from("<I", descriptor, reg_offset)[0]
	base = value & 0x7fff
	limit = (value >> 16) & 0x7fff
	if base > limit:
		return None
	return base * 4 * KiB, (limit - base + 1) * 4 * KiB


def bpdt_header(count, fit_tool_version):
	return struct.pack("<IHHIIQ", BPDT_SIGNATURE, count, 1, 0, 0, fit_tool_version)


def bpdt_entry(entry_type, offset, size):
	return struct.pack("<HHII", entry_type, 0, offset, size)


def write_bpdt(entries, fit_tool_version, min_size=0x200):
	data = bytearray(bpdt_header(len(entries), fit_tool_version))
	for entry_type, offset, size in entries:
		data += bpdt_entry(entry_type, offset, size)
	if len(data) < min_size:
		data.extend(b"\xff" * (min_size - len(data)))
	return bytes(data)


def build_bp1(parts, fit_tool_version, debug_token_size):
	ifp = parts["IFP_OVERRIDE"]
	uep = parts["UEP"]
	subparts = {
		BPDT_SMIP: parts["SMIP"],
		BPDT_RBEP: parts["RBEP"],
		BPDT_PMCP: parts["PMCP"],
		BPDT_FTPR: parts["FTPR"],
		BPDT_UCOD: parts["UCOD"],
		BPDT_IBBP: parts["IBBP"],
		BPDT_DEBUG_TOKENS: b"\xff" * debug_token_size,
		BPDT_NFTP: parts["NFTP"],
	}

	offsets = {}
	offset = 0x200
	offsets[BPDT_IFP_OVERRIDE] = offset
	offset += len(ifp)
	offsets[BPDT_UEP] = offset
	offset += len(uep)
	offset = align_up(offset, 4 * KiB)

	for entry_type in (BPDT_SMIP, BPDT_RBEP, BPDT_PMCP, BPDT_FTPR, BPDT_UCOD,
			   BPDT_IBBP, BPDT_DEBUG_TOKENS):
		offsets[entry_type] = offset
		subparts[entry_type] = padded(subparts[entry_type])
		offset += len(subparts[entry_type])

	sbpdt_offset = offset
	nftp_offset = sbpdt_offset + 4 * KiB
	nftp = padded(subparts[BPDT_NFTP])
	sbpdt_size = 4 * KiB + len(nftp)

	bpdt_entries = [
		(BPDT_DLMP, 0, 0),
		(BPDT_IFP_OVERRIDE, offsets[BPDT_IFP_OVERRIDE], len(ifp)),
		(BPDT_S_BPDT, sbpdt_offset, sbpdt_size),
		(BPDT_RBEP, offsets[BPDT_RBEP], len(subparts[BPDT_RBEP])),
		(BPDT_UFS_PHY, 0, 0),
		(BPDT_UFS_GPP, 0, 0),
		(BPDT_FTPR, offsets[BPDT_FTPR], len(subparts[BPDT_FTPR])),
		(BPDT_UEP, offsets[BPDT_UEP], len(uep)),
		(BPDT_SMIP, offsets[BPDT_SMIP], len(subparts[BPDT_SMIP])),
		(BPDT_PMCP, offsets[BPDT_PMCP], len(subparts[BPDT_PMCP])),
		(BPDT_UCOD, offsets[BPDT_UCOD], len(subparts[BPDT_UCOD])),
		(BPDT_IBBP, offsets[BPDT_IBBP], len(subparts[BPDT_IBBP])),
		(BPDT_DEBUG_TOKENS, offsets[BPDT_DEBUG_TOKENS],
		 len(subparts[BPDT_DEBUG_TOKENS])),
	]

	sbpdt = write_bpdt([(BPDT_NFTP, nftp_offset, len(nftp))], fit_tool_version,
			   min_size=4 * KiB)

	total = nftp_offset + len(nftp)
	out = bytearray(b"\xff" * total)
	out[:0x200] = write_bpdt(bpdt_entries, fit_tool_version)
	out[offsets[BPDT_IFP_OVERRIDE]:offsets[BPDT_IFP_OVERRIDE] + len(ifp)] = ifp
	out[offsets[BPDT_UEP]:offsets[BPDT_UEP] + len(uep)] = uep
	for entry_type, payload in subparts.items():
		if entry_type == BPDT_NFTP:
			continue
		part_offset = offsets[entry_type]
		out[part_offset:part_offset + len(payload)] = payload
	out[sbpdt_offset:sbpdt_offset + len(sbpdt)] = sbpdt
	out[nftp_offset:nftp_offset + len(nftp)] = nftp
	return bytes(out)


def build_bp2(obbp, fit_tool_version):
	obbp = padded(obbp)
	sbpdt_offset = 0x200
	obbp_offset = 0x1000
	sbpdt_size = obbp_offset + len(obbp) - sbpdt_offset
	bpdt_entries = [
		(BPDT_DLMP, 0, 0),
		(BPDT_IFP_OVERRIDE, 0, 0),
		(BPDT_S_BPDT, sbpdt_offset, sbpdt_size),
		(BPDT_RBEP, 0, 0),
		(BPDT_UFS_PHY, 0, 0),
		(BPDT_UFS_GPP, 0, 0),
		(BPDT_FTPR, 0, 0),
		(BPDT_UEP, 0, 0),
	]
	sbpdt = write_bpdt([(BPDT_OBBP, obbp_offset, len(obbp))], fit_tool_version,
			   min_size=obbp_offset - sbpdt_offset)

	out = bytearray(b"\xff" * (obbp_offset + len(obbp)))
	out[:0x200] = write_bpdt(bpdt_entries, fit_tool_version)
	out[sbpdt_offset:sbpdt_offset + len(sbpdt)] = sbpdt
	out[obbp_offset:obbp_offset + len(obbp)] = obbp
	return bytes(out)


def parse_int(value):
	return int(value, 0)


def create_ifwi(args):
	descriptor = read_file(args.descriptor)
	if len(descriptor) != 4 * KiB:
		raise ValueError("descriptor must be 4 KiB")

	bios_region = parse_ifd_region(descriptor, 1)
	if bios_region is None:
		raise ValueError("descriptor has no BIOS region")
	bios_start, bios_size = bios_region
	bios_end = bios_start + bios_size

	ec_region = parse_ifd_region(descriptor, 8)

	cse_image = read_file(args.cse_image)
	bios = read_file(args.bios)

	ucod = create_cpd("UCOD", [(f"upatch{i + 1}", read_file(path))
				   for i, path in enumerate(args.microcode)])

	ftpr = patch_cpd_entries(
		parse_fpt_partition(cse_image, "FTPR"),
		[("oem.key", read_file(args.oem_key)),
		 ("fitc.cfg", read_file(args.fitc_config))])

	parts = {
		"IFP_OVERRIDE": read_file(args.ifp_override),
		"UEP": read_file(args.uep),
		"SMIP": read_file(args.smip),
		"RBEP": parse_fpt_partition(cse_image, "RBEP"),
		"PMCP": read_file(args.pmcp),
		"FTPR": ftpr,
		"UCOD": ucod,
		"IBBP": parse_bios_partition(bios, "IBBP"),
		"NFTP": parse_fpt_partition(cse_image, "NFTP"),
	}

	bp1 = build_bp1(parts, args.fit_tool_version, args.debug_token_size)
	bp2 = build_bp2(parse_bios_partition(bios, "OBBP"), args.fit_tool_version)

	if bios_start != args.bp1_offset:
		raise ValueError(f"BP1 offset {args.bp1_offset:#x} does not match BIOS "
				 f"region start {bios_start:#x}")

	if args.bp1_offset + len(bp1) > args.bp2_offset:
		raise ValueError("BP1 overlaps BP2")

	if args.bp2_offset + len(bp2) > bios_end:
		raise ValueError("BP2 exceeds BIOS region")

	out = bytearray(b"\xff" * args.flash_size)
	out[:len(descriptor)] = descriptor
	out[args.bp1_offset:args.bp1_offset + len(bp1)] = bp1
	out[args.bp2_offset:args.bp2_offset + len(bp2)] = bp2

	if args.ec:
		if ec_region is None:
			raise ValueError("EC image supplied, but descriptor has no EC region")
		ec_offset, ec_size = ec_region
		ec = read_file(args.ec)
		if len(ec) > ec_size:
			raise ValueError("EC image exceeds EC region")
		out[ec_offset:ec_offset + len(ec)] = ec

	Path(args.output).write_bytes(out)


def main(argv):
	parser = argparse.ArgumentParser()
	parser.add_argument("--output", required=True)
	parser.add_argument("--flash-size", type=parse_int, default=8 * 1024 * 1024)
	parser.add_argument("--bp1-offset", type=parse_int, default=0x1000)
	parser.add_argument("--bp2-offset", type=parse_int, required=True)
	parser.add_argument("--fit-tool-version", type=parse_int, default=0)
	parser.add_argument("--debug-token-size", type=parse_int, default=0x2000)
	parser.add_argument("--descriptor", required=True)
	parser.add_argument("--cse-image", required=True)
	parser.add_argument("--bios", required=True)
	parser.add_argument("--pmcp", required=True)
	parser.add_argument("--smip", required=True)
	parser.add_argument("--ifp-override", required=True)
	parser.add_argument("--uep", required=True)
	parser.add_argument("--oem-key", required=True)
	parser.add_argument("--fitc-config", required=True)
	parser.add_argument("--microcode", action="append", required=True)
	parser.add_argument("--ec")
	args = parser.parse_args(argv)

	create_ifwi(args)
	return 0


if __name__ == "__main__":
	try:
		sys.exit(main(sys.argv[1:]))
	except ValueError as err:
		print(f"apl_ifwi.py: {err}", file=sys.stderr)
		sys.exit(1)
