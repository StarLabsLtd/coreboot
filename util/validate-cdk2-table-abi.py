#!/usr/bin/env python3
"""Validate the shared local-APIC and payload-SPI wire ABIs."""

import ast
import ctypes
import pathlib
import re
import sys


class AbiMismatch(Exception):
	pass


def integer_expression(expression):
	expression = re.sub(r"(?<=[0-9a-fA-F])[uUlL]+\b", "", expression)
	node = ast.parse(expression, mode="eval")
	allowed = (ast.Expression, ast.Constant, ast.BinOp, ast.Add, ast.Sub,
		ast.Mult, ast.FloorDiv, ast.LShift, ast.RShift, ast.BitOr,
		ast.BitAnd, ast.BitXor, ast.UnaryOp, ast.UAdd, ast.USub)
	if any(not isinstance(item, allowed) for item in ast.walk(node)):
		raise AbiMismatch(f"unsupported integer expression: {expression}")
	return eval(compile(node, "<ABI expression>", "eval"), {"__builtins__": {}})


def constant(source, name):
	match = re.search(rf"^#define\s+{name}\s+([^/\n]+)|"
		rf"^\s*{name}\s*=\s*([^,\n]+)", source, re.MULTILINE)
	if not match:
		raise AbiMismatch(f"missing {name}")
	value = next(item for item in match.groups() if item is not None)
	return integer_expression(value.strip())


def require(source, pattern, description):
	if not re.search(pattern, source, re.MULTILINE | re.DOTALL):
		raise AbiMismatch(f"missing or changed {description}")


def struct_definition(source, name):
	match = re.search(rf"struct\s+{name}\s*\{{(.*?)\}}\s*(__packed)?\s*;",
		source, re.DOTALL)
	if not match:
		raise AbiMismatch(f"missing struct {name}")
	body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.DOTALL)
	return body, match.group(2) is not None


def normalize_declaration(declaration):
	declaration = re.sub(r"\s+", " ", declaration.strip())
	declaration = re.sub(r"\s*\[\s*", "[", declaration)
	declaration = re.sub(r"\s*\]", "]", declaration)
	return declaration


def require_exact_struct(source, name, expected, packed):
	body, actual_packed = struct_definition(source, name)
	actual = tuple(normalize_declaration(item) for item in body.split(";") if item.strip())
	expected = tuple(normalize_declaration(item) for item in expected)
	if actual != expected:
		raise AbiMismatch(
			f"struct {name} declaration mismatch: actual={actual!r}, expected={expected!r}")
	if actual_packed != packed:
		raise AbiMismatch(
			f"struct {name} packing mismatch: packed={actual_packed}, expected={packed}")


class LocalApic(ctypes.LittleEndianStructure):
	_pack_ = 1
	_fields_ = [("tag", ctypes.c_uint32), ("size", ctypes.c_uint32),
		("revision", ctypes.c_uint16), ("reserved", ctypes.c_uint16),
		("frequency_hz", ctypes.c_uint64)]


class SpiRecord(ctypes.LittleEndianStructure):
	_pack_ = 1
	_fields_ = [("tag", ctypes.c_uint32), ("size", ctypes.c_uint32),
		("version", ctypes.c_uint16), ("request_header_size", ctypes.c_uint16),
		("com_buffer", ctypes.c_uint64), ("com_buffer_size", ctypes.c_uint32),
		("max_chunk", ctypes.c_uint32), ("boot_limit", ctypes.c_uint32),
		("apm_cmd", ctypes.c_uint8), ("reserved", ctypes.c_uint8 * 3)]


class SpiRequest(ctypes.LittleEndianStructure):
	_pack_ = 1
	_fields_ = [("signature", ctypes.c_uint32), ("version", ctypes.c_uint16),
		("header_size", ctypes.c_uint16), ("length", ctypes.c_uint16),
		("flags", ctypes.c_uint16), ("status", ctypes.c_uint32)]


def validate_sources(cb, payload, cdk, cdk_handoff, cdk_diagnostic, cdk_test):
	pairs = {
		"LB_TAG_LOCAL_APIC_TIMER_INFO": ("CB_TAG_LOCAL_APIC_TIMER_INFO", 0x004e),
		"LB_TAG_PAYLOAD_SPI_CONSOLE": ("CB_TAG_PAYLOAD_SPI_CONSOLE", 0x0050),
	}
	values = []
	for cb_name, (cdk_name, expected) in pairs.items():
		producer = constant(cb, cb_name)
		consumer = constant(cdk, cdk_name)
		if producer != expected or consumer != expected:
			raise AbiMismatch(
				f"ABI mismatch: {cb_name}={producer:#06x}, {cdk_name}={consumer:#06x}")
		values.append(producer)
	if len(values) != len(set(values)):
		raise AbiMismatch("coreboot table tag collision")

	require_exact_struct(cb, "lb_local_apic_timer_info", (
		"uint32_t tag", "uint32_t size", "uint16_t revision", "uint16_t reserved",
		"lb_uint64_t frequency_hz"), False)
	require_exact_struct(cdk, "cb_local_apic_timer_info", (
		"struct cb_record header", "UINT16 revision", "UINT16 reserved",
		"UINT64 frequency_hz"), True)
	if ctypes.sizeof(LocalApic) != 20 or LocalApic.frequency_hz.offset != 12:
		raise AbiMismatch("local APIC record layout model changed")
	require(cb, r"sizeof\(struct lb_local_apic_timer_info\) == 20",
		"producer local APIC size assertion")
	require(cb, r"offsetof\(struct lb_local_apic_timer_info, frequency_hz\) == 12",
		"producer local APIC offset assertion")

	require_exact_struct(cb, "lb_payload_spi_console", (
		"uint32_t tag", "uint32_t size", "uint16_t version",
		"uint16_t request_header_size", "lb_uint64_t com_buffer",
		"uint32_t com_buffer_size", "uint32_t max_chunk", "uint32_t boot_limit",
		"uint8_t apm_cmd", "uint8_t reserved[3]"), False)
	require_exact_struct(cdk, "cb_payload_spi_console", (
		"UINT32 tag", "UINT32 size", "UINT16 version",
		"UINT16 request_header_size", "UINT64 com_buffer", "UINT32 com_buffer_size",
		"UINT32 max_chunk", "UINT32 boot_limit", "UINT8 apm_cmd",
		"UINT8 reserved[3]"), True)
	if (ctypes.sizeof(SpiRecord) != 36 or SpiRecord.com_buffer.offset != 12 or
	    SpiRecord.apm_cmd.offset != 32):
		raise AbiMismatch("payload SPI record layout model changed")
	for pattern, description in (
		(r"sizeof\(struct lb_payload_spi_console\) == 36", "producer SPI record size"),
		(r"offsetof\(struct lb_payload_spi_console, com_buffer\) == 12",
			"producer SPI buffer offset"),
		(r"offsetof\(struct lb_payload_spi_console, apm_cmd\) == 32",
			"producer SPI command offset")):
		require(cb, pattern, description)

	require_exact_struct(payload, "payload_spi_console_request", (
		"uint32_t signature", "uint16_t version", "uint16_t header_size",
		"uint16_t length", "uint16_t flags", "uint32_t status", "uint8_t data[]"),
		True)
	require_exact_struct(cdk_diagnostic, "spi_console_request", (
		"UINT32 signature", "UINT16 version", "UINT16 header_size", "UINT16 length",
		"UINT16 flags", "volatile UINT32 status", "UINT8 data[]"), True)
	if ctypes.sizeof(SpiRequest) != 16 or SpiRequest.status.offset != 12:
		raise AbiMismatch("payload SPI request layout model changed")

	expected = {
		"PAYLOAD_SPI_CONSOLE_SIGNATURE": 0x434c5053,
		"PAYLOAD_SPI_CONSOLE_VERSION": 1,
		"PAYLOAD_SPI_CONSOLE_APM_CMD": 0xe8,
		"PAYLOAD_SPI_CONSOLE_MAX_CHUNK": 256,
		"PAYLOAD_SPI_CONSOLE_BUFFER_SIZE": 512,
		"PAYLOAD_SPI_CONSOLE_BOOT_LIMIT": 64 * 1024,
		"PAYLOAD_SPI_CONSOLE_PENDING": 0,
		"PAYLOAD_SPI_CONSOLE_SUCCESS": 1,
		"PAYLOAD_SPI_CONSOLE_INVALID": 2,
		"PAYLOAD_SPI_CONSOLE_BUSY": 3,
		"PAYLOAD_SPI_CONSOLE_LIMIT": 4,
		"PAYLOAD_SPI_CONSOLE_IO_ERROR": 5,
	}
	for name, value in expected.items():
		actual = constant(payload, name)
		if actual != value:
			raise AbiMismatch(f"ABI mismatch: {name}={actual:#x}, expected {value:#x}")

	for name, value in (("SPI_CONSOLE_SIGNATURE", 0x434c5053),
		("SPI_CONSOLE_VERSION", 1), ("SPI_CONSOLE_PENDING", 0),
		("SPI_CONSOLE_SUCCESS", 1)):
		if constant(cdk_diagnostic, name) != value:
			raise AbiMismatch(f"CDK2 {name} changed")
	for pattern, description in (
		(r"sizeof\(struct cb_local_apic_timer_info\) == 20U",
			"CDK2 local APIC record size assertion"),
		(r"OFFSET_OF\(struct cb_local_apic_timer_info, frequency_hz\) == 12U",
			"CDK2 local APIC frequency offset assertion"),
		(r"sizeof\(struct cb_payload_spi_console\) == 36U",
			"CDK2 SPI record size assertion"),
		(r"OFFSET_OF\(struct cb_payload_spi_console, com_buffer\) == 12U",
			"CDK2 SPI buffer offset assertion"),
		(r"OFFSET_OF\(struct cb_payload_spi_console, apm_cmd\) == 32U",
			"CDK2 SPI command offset assertion"),
		(r"spi->request_header_size\s*!=\s*16U", "CDK2 request header size"),
		(r"spi->max_chunk\s*>\s*256U", "CDK2 maximum chunk"),
		(r"spi->apm_cmd\s*!=\s*0xe8U", "CDK2 APM command"),
		(r"UINT8\s+spi_console_storage\[512\]", "CDK2 SPI buffer size"),
		(r"spi->boot_limit\s*=\s*64U\s*\*\s*1024U", "CDK2 boot limit")):
		require(cdk_handoff + "\n" + cdk_test, pattern, description)
	if "uint32_t sequence;" in payload:
		raise AbiMismatch("v1 SPI request must not expose an unenforced sequence")


def validate(coreboot, cdk2, hostile=False):
	paths = {
		"cb": coreboot / "src/commonlib/include/commonlib/coreboot_tables.h",
		"payload": coreboot / "src/include/console/payload_spi_console.h",
		"cdk": cdk2 / "include/coreboot_tables.h",
		"handoff": cdk2 / "src/boot/coreboot_handoff.c",
		"diagnostic": cdk2 / "src/lib/diagnostic.c",
		"test": cdk2 / "src/boot/coreboot_test.c",
	}
	sources = {name: path.read_text() for name, path in paths.items()}
	validate_sources(sources["cb"], sources["payload"], sources["cdk"],
		sources["handoff"], sources["diagnostic"], sources["test"])
	if hostile:
		mutations = (
			("cdk", "#define CB_TAG_LOCAL_APIC_TIMER_INFO 0x004eU",
				"#define CB_TAG_LOCAL_APIC_TIMER_INFO 0x004dU"),
			("cb", "lb_uint64_t frequency_hz;", "uint32_t frequency_hz;"),
			("cdk", "UINT16 reserved;\n\tUINT64 frequency_hz;",
				"UINT16 reserved;\n\tUINT32 hostile_layout_drift;\n\tUINT64 frequency_hz;"),
			("cdk", "UINT16 request_header_size;\n\tUINT64 com_buffer;",
				"UINT16 request_header_size;\n\tUINT32 hostile_layout_drift;\n\tUINT64 com_buffer;"),
			("payload", "#define PAYLOAD_SPI_CONSOLE_MAX_CHUNK 256U",
				"#define PAYLOAD_SPI_CONSOLE_MAX_CHUNK 257U"),
			("handoff", "spi->apm_cmd != 0xe8U", "spi->apm_cmd != 0xe9U"),
		)
		for source_name, old, new in mutations:
			changed = dict(sources)
			if old not in changed[source_name]:
				raise AbiMismatch(f"hostile-test fixture missing: {old}")
			changed[source_name] = changed[source_name].replace(old, new, 1)
			try:
				validate_sources(changed["cb"], changed["payload"], changed["cdk"],
					changed["handoff"], changed["diagnostic"], changed["test"])
			except AbiMismatch:
				continue
			raise AbiMismatch(f"hostile ABI mutation escaped validation: {old}")


if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--selftest"):
	raise SystemExit(f"usage: {sys.argv[0]} CDK2_TREE [--selftest]")

try:
	validate(pathlib.Path(__file__).resolve().parents[1], pathlib.Path(sys.argv[1]).resolve(),
		len(sys.argv) == 3)
except AbiMismatch as error:
	raise SystemExit(str(error))
print("coreboot/CDK2 table ABI validation passed")
if len(sys.argv) == 3:
	print("coreboot/CDK2 hostile ABI mutations rejected")
