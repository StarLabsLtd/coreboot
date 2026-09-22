#!/usr/bin/env python3
"""Validate coreboot/CDK2 shared table and fixed-message ABIs."""

import ast
import ctypes
import pathlib
import re
import subprocess
import sys
import tempfile


class AbiMismatch(Exception):
	pass


def toolchain(source, include_dirs, preprocess):
	command = ["cc", "-Werror"]
	if preprocess:
		command += ["-E", "-P", "-dD", "-fdirectives-only"]
	else:
		command += ["-fsyntax-only"]
	command += [f"-I{directory}" for directory in include_dirs]
	command += ["-x", "c", "-"]
	result = subprocess.run(command, input=source, text=True, capture_output=True,
		check=False)
	if result.returncode:
		detail = result.stderr.strip().splitlines()
		raise AbiMismatch(f"C {'preprocessing' if preprocess else 'compilation'} failed: "
			f"{' | '.join(detail[-4:]) if detail else 'unknown compiler error'}")
	return result.stdout


def assertion_is_enforced(source, include_dirs, needle, replacement, description):
	if needle not in source:
		raise AbiMismatch(f"missing assertion probe fixture: {description}")
	mutated = source.replace(needle, replacement)
	command = ["cc", "-Werror", "-fsyntax-only"]
	command += [f"-I{directory}" for directory in include_dirs]
	command += ["-x", "c", "-"]
	result = subprocess.run(command, input=mutated, text=True, capture_output=True,
		check=False)
	if result.returncode == 0:
		raise AbiMismatch(f"required assertion is not compiler-enforced: {description}")


def consumer_compiler_probe(source, include_dirs, constants, layouts, field_types,
		extra=()):
	probe = []
	for name, value in constants.items():
		probe.append(f'_Static_assert({name} == {value}, "{name} ABI");')
	for structure, size, alignment, offsets in layouts:
		probe.append(f'_Static_assert(sizeof(struct {structure}) == {size}, '
			f'"{structure} size ABI");')
		probe.append(f'_Static_assert(_Alignof(struct {structure}) == {alignment}, '
			f'"{structure} alignment ABI");')
		for field, offset in offsets.items():
			probe.append(f'_Static_assert(__builtin_offsetof(struct {structure}, {field}) == '
				f'{offset}, "{structure}.{field} offset ABI");')
	for structure, fields in field_types.items():
		for field, expected_type in fields.items():
			probe.append('_Static_assert(__builtin_types_compatible_p('
				f'__typeof__(((struct {structure} *)0)->{field}), {expected_type}), '
				f'"{structure}.{field} type ABI");')
	probe.extend(extra)
	toolchain(source + "\n" + "\n".join(probe) + "\n", include_dirs, False)


def integer_expression(expression, symbols=None):
	expression = re.sub(r"(?<=[0-9a-fA-F])[uUlL]+\b", "", expression)
	symbols = symbols or {}
	for symbol in set(re.findall(r"\b[A-Za-z_]\w*\b", expression)):
		if symbol not in symbols:
			raise AbiMismatch(f"unsupported integer symbol: {symbol}")
		expression = re.sub(rf"\b{symbol}\b", str(symbols[symbol]), expression)
	node = ast.parse(expression, mode="eval")
	allowed = (ast.Expression, ast.Constant, ast.BinOp, ast.Add, ast.Sub,
		ast.Mult, ast.FloorDiv, ast.LShift, ast.RShift, ast.BitOr,
		ast.BitAnd, ast.BitXor, ast.UnaryOp, ast.UAdd, ast.USub)
	if any(not isinstance(item, allowed) for item in ast.walk(node)):
		raise AbiMismatch(f"unsupported integer expression: {expression}")
	return eval(compile(node, "<ABI expression>", "eval"), {"__builtins__": {}})


def constant(source, name, resolving=()):
	logical_source = re.sub(r"\\\n\s*", " ", source)
	matches = list(re.finditer(rf"^#define\s+{name}\s+([^/\n]+)|"
		rf"^\s*{name}\s*=(?!=)\s*([^,\n]+)", logical_source, re.MULTILINE))
	if not matches:
		raise AbiMismatch(f"missing {name}")
	if len(matches) != 1:
		raise AbiMismatch(f"ambiguous duplicate constant {name}")
	match = matches[0]
	value = next(item for item in match.groups() if item is not None)
	value = re.sub(r"\bBIT\s*\(\s*(\d+)\s*\)", r"(1 << \1)", value)
	if name in resolving:
		raise AbiMismatch(f"recursive integer constant: {name}")
	symbols = {}
	for symbol in set(re.findall(r"\b[A-Za-z_]\w*\b", value)):
		if symbol in ("UINT32_MAX", "MAX_UINT32"):
			symbols[symbol] = (1 << 32) - 1
		elif symbol in ("UINT64_MAX", "MAX_UINT64"):
			symbols[symbol] = (1 << 64) - 1
		else:
			symbols[symbol] = constant(source, symbol, resolving + (name,))
	return integer_expression(value.strip(), symbols)


def require(source, pattern, description):
	if not re.search(pattern, source, re.MULTILINE | re.DOTALL):
		raise AbiMismatch(f"missing or changed {description}")


def require_unique(source, pattern, description):
	matches = re.findall(pattern, source, re.MULTILINE | re.DOTALL)
	if len(matches) != 1:
		raise AbiMismatch(f"missing, changed or ambiguous {description}")


def struct_definition(source, name):
	matches = list(re.finditer(rf"struct\s+{name}\s*\{{(.*?)\}}\s*"
		r"(__packed|__aligned\s*\(\s*\d+\s*\))?\s*;",
		source, re.DOTALL))
	if not matches:
		raise AbiMismatch(f"missing struct {name}")
	if len(matches) != 1:
		raise AbiMismatch(f"ambiguous duplicate struct {name}")
	match = matches[0]
	body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.DOTALL)
	attribute = re.sub(r"\s+", "", match.group(2) or "")
	return body, attribute


def normalize_declaration(declaration):
	declaration = re.sub(r"\s+", " ", declaration.strip())
	declaration = re.sub(r"\s*\[\s*", "[", declaration)
	declaration = re.sub(r"\s*\]", "]", declaration)
	return declaration


def require_exact_struct(source, name, expected, attribute=""):
	body, actual_attribute = struct_definition(source, name)
	actual = tuple(normalize_declaration(item) for item in body.split(";") if item.strip())
	expected = tuple(normalize_declaration(item) for item in expected)
	if actual != expected:
		raise AbiMismatch(
			f"struct {name} declaration mismatch: actual={actual!r}, expected={expected!r}")
	if actual_attribute != attribute:
		raise AbiMismatch(
			f"struct {name} attribute mismatch: actual={actual_attribute!r}, "
			f"expected={attribute!r}")


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


class CapsuleRegion(ctypes.LittleEndianStructure):
	_pack_ = 1
	_fields_ = [("image_offset", ctypes.c_uint64), ("flash_offset", ctypes.c_uint64),
		("size", ctypes.c_uint64), ("flags", ctypes.c_uint32),
		("reserved", ctypes.c_uint32)]


class CapsuleHandoff(ctypes.LittleEndianStructure):
	_pack_ = 1
	_fields_ = [("tag", ctypes.c_uint32), ("size", ctypes.c_uint32),
		("revision", ctypes.c_uint16), ("header_size", ctypes.c_uint16),
		("flags", ctypes.c_uint32), ("broker_type", ctypes.c_uint16),
		("capsule_format", ctypes.c_uint16),
		("authentication_format", ctypes.c_uint16),
		("board_binding_format", ctypes.c_uint16),
		("payload_format", ctypes.c_uint16), ("reserved16", ctypes.c_uint16),
		("broker_capabilities", ctypes.c_uint32), ("image_type_guid", ctypes.c_uint8 * 16),
		("version", ctypes.c_uint32), ("lowest_supported_version", ctypes.c_uint32),
		("capsule_flags", ctypes.c_uint32), ("block_size", ctypes.c_uint32),
		("erase_size", ctypes.c_uint32), ("region_count", ctypes.c_uint32),
		("image_size", ctypes.c_uint64), ("boot_media_size", ctypes.c_uint64),
		("smmstore_offset", ctypes.c_uint64), ("smmstore_size", ctypes.c_uint64),
		("reserved32", ctypes.c_uint32 * 2)]


class CapsuleEndpoint(ctypes.LittleEndianStructure):
	_pack_ = 1
	_fields_ = [("tag", ctypes.c_uint32), ("size", ctypes.c_uint32),
		("revision", ctypes.c_uint16), ("header_size", ctypes.c_uint16),
		("flags", ctypes.c_uint32), ("generation", ctypes.c_uint64),
		("communication_base", ctypes.c_uint64), ("communication_size", ctypes.c_uint32),
		("message_size", ctypes.c_uint32), ("staging_base", ctypes.c_uint64),
		("staging_size", ctypes.c_uint64), ("transport", ctypes.c_uint16),
		("trigger_width", ctypes.c_uint16), ("trigger_address", ctypes.c_uint32),
		("trigger_value", ctypes.c_uint32), ("reserved", ctypes.c_uint32 * 3)]


class TransportRequest(ctypes.LittleEndianStructure):
	_fields_ = [("revision", ctypes.c_uint32), ("size", ctypes.c_uint32),
		("operation", ctypes.c_uint32), ("flags", ctypes.c_uint32),
		("generation", ctypes.c_uint64), ("transaction", ctypes.c_uint64),
		("intent_size", ctypes.c_uint32), ("result_size", ctypes.c_uint32)]


class CapsuleIntent(ctypes.LittleEndianStructure):
	_fields_ = [("revision", ctypes.c_uint32), ("size", ctypes.c_uint32),
		("operation", ctypes.c_uint32), ("flags", ctypes.c_uint32),
		("broker_generation", ctypes.c_uint64), ("transaction", ctypes.c_uint64),
		("capsule_size", ctypes.c_uint64), ("digest_algorithm", ctypes.c_uint32),
		("digest_size", ctypes.c_uint32), ("digest", ctypes.c_uint8 * 32),
		("attempted_version", ctypes.c_uint32), ("reserved", ctypes.c_uint32)]


class TransportResult(ctypes.LittleEndianStructure):
	_fields_ = [("revision", ctypes.c_uint32), ("size", ctypes.c_uint32),
		("generation", ctypes.c_uint64), ("transaction", ctypes.c_uint64),
		("attempted_version", ctypes.c_uint32), ("reserved", ctypes.c_uint32),
		("last_attempt_status", ctypes.c_uint32), ("result", ctypes.c_uint32)]


class TransportInfo(ctypes.LittleEndianStructure):
	_fields_ = [("revision", ctypes.c_uint32), ("size", ctypes.c_uint32),
		("generation", ctypes.c_uint64), ("transaction", ctypes.c_uint64),
		("image_type", ctypes.c_uint8 * 16), ("hardware_instance", ctypes.c_uint64),
		("current_version", ctypes.c_uint32),
		("lowest_supported_version", ctypes.c_uint32), ("image_size", ctypes.c_uint32),
		("capabilities", ctypes.c_uint32), ("state_flags", ctypes.c_uint32),
		("last_attempt_version", ctypes.c_uint32),
		("last_attempt_status", ctypes.c_uint32), ("reserved", ctypes.c_uint32 * 12),
		("result", ctypes.c_uint32)]


def require_constants(source, expected, owner):
	for name, value in expected.items():
		actual = constant(source, name)
		if actual != value:
			raise AbiMismatch(
				f"{owner} ABI mismatch: {name}={actual:#x}, expected {value:#x}")


def require_layout(model, size, offsets, description, alignment=None):
	if ctypes.sizeof(model) != size:
		raise AbiMismatch(f"{description} size model changed")
	if alignment is not None and ctypes.alignment(model) != alignment:
		raise AbiMismatch(f"{description} alignment model changed")
	for field, expected in offsets.items():
		if getattr(model, field).offset != expected:
			raise AbiMismatch(f"{description} {field} offset model changed")


def validate_sources(cb, payload, cdk, cdk_handoff, cdk_diagnostic, cdk_test):
	pairs = {
		"LB_TAG_LOCAL_APIC_TIMER_INFO": ("CB_TAG_LOCAL_APIC_TIMER_INFO", 0x004e),
		"LB_TAG_PAYLOAD_SPI_CONSOLE": ("CB_TAG_PAYLOAD_SPI_CONSOLE", 0x0050),
		"LB_TAG_CAPSULE_HANDOFF": ("CB_TAG_CAPSULE_HANDOFF", 0x0052),
		"LB_TAG_CAPSULE_BROKER_ENDPOINT": ("CB_TAG_CAPSULE_BROKER_ENDPOINT", 0x0054),
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
		"lb_uint64_t frequency_hz"))
	require_exact_struct(cdk, "cb_local_apic_timer_info", (
		"struct cb_record header", "UINT16 revision", "UINT16 reserved",
		"UINT64 frequency_hz"), "__packed")
	if ctypes.sizeof(LocalApic) != 20 or LocalApic.frequency_hz.offset != 12:
		raise AbiMismatch("local APIC record layout model changed")
	require_unique(cb, r"sizeof\(struct lb_local_apic_timer_info\) == 20",
		"producer local APIC size assertion")
	require_unique(cb, r"offsetof\(struct lb_local_apic_timer_info, frequency_hz\) == 12",
		"producer local APIC offset assertion")

	require_exact_struct(cb, "lb_payload_spi_console", (
		"uint32_t tag", "uint32_t size", "uint16_t version",
		"uint16_t request_header_size", "lb_uint64_t com_buffer",
		"uint32_t com_buffer_size", "uint32_t max_chunk", "uint32_t boot_limit",
		"uint8_t apm_cmd", "uint8_t reserved[3]"))
	require_exact_struct(cdk, "cb_payload_spi_console", (
		"UINT32 tag", "UINT32 size", "UINT16 version",
		"UINT16 request_header_size", "UINT64 com_buffer", "UINT32 com_buffer_size",
		"UINT32 max_chunk", "UINT32 boot_limit", "UINT8 apm_cmd",
		"UINT8 reserved[3]"), "__packed")
	if (ctypes.sizeof(SpiRecord) != 36 or SpiRecord.com_buffer.offset != 12 or
	    SpiRecord.apm_cmd.offset != 32):
		raise AbiMismatch("payload SPI record layout model changed")
	for pattern, description in (
		(r"sizeof\(struct lb_payload_spi_console\) == 36", "producer SPI record size"),
		(r"offsetof\(struct lb_payload_spi_console, com_buffer\) == 12",
			"producer SPI buffer offset"),
		(r"offsetof\(struct lb_payload_spi_console, apm_cmd\) == 32",
			"producer SPI command offset")):
		require_unique(cb, pattern, description)

	require_exact_struct(payload, "payload_spi_console_request", (
		"uint32_t signature", "uint16_t version", "uint16_t header_size",
		"uint16_t length", "uint16_t flags", "uint32_t status", "uint8_t data[]"),
		"__packed")
	require_exact_struct(cdk_diagnostic, "spi_console_request", (
		"UINT32 signature", "UINT16 version", "UINT16 header_size", "UINT16 length",
		"UINT16 flags", "volatile UINT32 status", "UINT8 data[]"), "__packed")
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


def validate_dma_sources(producer, consumer):
	constants = {
		"DMA_HANDOFF_REVISION": 2,
		"DMA_HANDOFF_GRANULE_SHIFT": 12,
		"DMA_HANDOFF_REQUIRED_FLAGS": 0x3f,
		"DMA_HANDOFF_REQUESTER_FLAGS": 0xf,
		"DMA_HANDOFF_ARENA_READ": 0x1,
		"DMA_HANDOFF_ARENA_WRITE": 0x2,
		"DMA_HANDOFF_ARENA_PREMAPPED": 0x4,
		"DMA_HANDOFF_ARENA_IMMUTABLE": 0x8,
		"DMA_HANDOFF_ARENA_COHERENT": 0x10,
		"DMA_HANDOFF_ARENA_FLAGS": 0x1f,
	}
	require_constants(producer, constants, "coreboot DMA handoff")
	require_constants(consumer, constants, "CDK2 DMA handoff")
	producer_requester = (
		"uint16_t segment", "uint16_t bdf", "uint16_t protection_domain",
		"uint16_t flags", "uint64_t arena_cpu_base",
		"uint64_t arena_device_base", "uint32_t arena_pages", "uint32_t arena_flags",
	)
	consumer_requester = tuple(field.replace("uint16_t", "UINT16").replace(
		"uint32_t", "UINT32").replace("uint64_t", "UINT64")
		for field in producer_requester)
	require_exact_struct(producer, "dma_handoff_requester", producer_requester,
		"__packed")
	require_exact_struct(consumer, "dma_handoff_requester", consumer_requester,
		"__packed")
	for source, owner in ((producer, "producer"), (consumer, "consumer")):
		require_unique(source,
			r"sizeof\(struct dma_handoff_requester\) == 32U?",
			f"{owner} DMA requester size assertion")


def validate_capsule_sources(cb, cdk, broker, authvar, transport):
	producer_handoff = {
		"LB_CAPSULE_HANDOFF_REVISION": 2,
		"LB_CAPSULE_HANDOFF_AUTHENTICATED": 1,
		"LB_CAPSULE_HANDOFF_RESET_REQUIRED": 2,
		"LB_CAPSULE_HANDOFF_REQUIRED_FLAGS": 3,
		"LB_CAPSULE_BROKER_COREBOOT_UPDATE": 1,
		"LB_CAPSULE_BROKER_APPLY_REGIONS": 1,
		"LB_CAPSULE_BROKER_PRESERVE_UNLISTED": 2,
		"LB_CAPSULE_BROKER_VERIFY_READBACK": 4,
		"LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES": 7,
		"LB_CAPSULE_FORMAT_FMP_V3": 3,
		"LB_CAPSULE_AUTH_EFI_PKCS7": 1,
		"LB_CAPSULE_BOARD_BINDING_CBFS_BUILD_INFO_V1": 1,
		"LB_CAPSULE_PAYLOAD_MSS1_V1": 1,
		"LB_CAPSULE_FLAGS_PERSIST_RESET": 0x00050000,
		"LB_CAPSULE_REGION_BIOS": 1,
		"LB_CAPSULE_REGION_VALID_FLAGS": 1,
	}
	consumer_handoff = {
		name.replace("LB_", "CB_", 1): value for name, value in producer_handoff.items()
	}
	require_constants(cb, producer_handoff, "coreboot capsule handoff")
	require_constants(cdk, consumer_handoff, "CDK2 capsule handoff")

	producer_endpoint = {
		"LB_CAPSULE_BROKER_ENDPOINT_REVISION": 1,
		"LB_CAPSULE_ENDPOINT_COREBOOT_SMM_OWNER": 1,
		"LB_CAPSULE_ENDPOINT_SMM_ONLY_SPI": 2,
		"LB_CAPSULE_ENDPOINT_FIXED_COMMUNICATION": 4,
		"LB_CAPSULE_ENDPOINT_DMA_PROTECTED": 8,
		"LB_CAPSULE_ENDPOINT_CPU_RENDEZVOUS": 16,
		"LB_CAPSULE_ENDPOINT_ONE_SHOT": 32,
		"LB_CAPSULE_ENDPOINT_NO_RAW_FLASH": 64,
		"LB_CAPSULE_ENDPOINT_REQUIRED_FLAGS": 127,
		"LB_CAPSULE_ENDPOINT_TRANSPORT_APM_IO8": 1,
	}
	consumer_endpoint = {
		name.replace("LB_", "CB_", 1): value for name, value in producer_endpoint.items()
	}
	require_constants(cb, producer_endpoint, "coreboot capsule endpoint")
	require_constants(cdk, consumer_endpoint, "CDK2 capsule endpoint")

	require_exact_struct(cb, "lb_capsule_update_region", (
		"lb_uint64_t image_offset", "lb_uint64_t flash_offset", "lb_uint64_t size",
		"uint32_t flags", "uint32_t reserved"), "__packed")
	require_exact_struct(cdk, "cb_capsule_update_region", (
		"struct cbuint64 image_offset", "struct cbuint64 flash_offset",
		"struct cbuint64 size", "UINT32 flags", "UINT32 reserved"), "__packed")
	require_exact_struct(cb, "lb_capsule_handoff", (
		"uint32_t tag", "uint32_t size", "uint16_t revision", "uint16_t header_size",
		"uint32_t flags", "uint16_t broker_type", "uint16_t capsule_format",
		"uint16_t authentication_format", "uint16_t board_binding_format",
		"uint16_t payload_format", "uint16_t reserved16",
		"uint32_t broker_capabilities", "uint8_t image_type_guid[16]",
		"uint32_t version", "uint32_t lowest_supported_version",
		"uint32_t capsule_flags", "uint32_t block_size", "uint32_t erase_size",
		"uint32_t region_count", "lb_uint64_t image_size", "lb_uint64_t boot_media_size",
		"lb_uint64_t smmstore_offset", "lb_uint64_t smmstore_size",
		"uint32_t reserved32[2]", "struct lb_capsule_update_region regions[]"),
		"__packed")
	require_exact_struct(cdk, "cb_capsule_handoff", (
		"UINT32 tag", "UINT32 size", "UINT16 revision", "UINT16 header_size",
		"UINT32 flags", "UINT16 broker_type", "UINT16 capsule_format",
		"UINT16 authentication_format", "UINT16 board_binding_format",
		"UINT16 payload_format", "UINT16 reserved16", "UINT32 broker_capabilities",
		"UINT8 image_type_guid[16]", "UINT32 version", "UINT32 lowest_supported_version",
		"UINT32 capsule_flags", "UINT32 block_size", "UINT32 erase_size",
		"UINT32 region_count", "struct cbuint64 image_size",
		"struct cbuint64 boot_media_size", "struct cbuint64 smmstore_offset",
		"struct cbuint64 smmstore_size", "UINT32 reserved32[2]",
		"struct cb_capsule_update_region regions[]"), "__packed")
	require_layout(CapsuleRegion, 32, {}, "capsule update region")
	require_layout(CapsuleHandoff, 112, {
		"broker_type": 16, "broker_capabilities": 28, "image_type_guid": 32,
		"version": 48, "image_size": 72, "reserved32": 104,
	}, "capsule handoff")

	endpoint_fields = (
		"uint32_t tag", "uint32_t size", "uint16_t revision", "uint16_t header_size",
		"uint32_t flags", "lb_uint64_t generation", "lb_uint64_t communication_base",
		"uint32_t communication_size", "uint32_t message_size",
		"lb_uint64_t staging_base", "lb_uint64_t staging_size", "uint16_t transport",
		"uint16_t trigger_width", "uint32_t trigger_address", "uint32_t trigger_value",
		"uint32_t reserved[3]")
	require_exact_struct(cb, "lb_capsule_broker_endpoint", endpoint_fields, "__packed")
	require_exact_struct(cdk, "cb_capsule_broker_endpoint", tuple(
		field.replace("uint32_t", "UINT32").replace("uint16_t", "UINT16").replace(
			"lb_uint64_t", "struct cbuint64") for field in endpoint_fields), "__packed")
	require_layout(CapsuleEndpoint, 80, {
		"generation": 16, "communication_base": 24, "staging_base": 40,
		"transport": 56, "reserved": 68,
	}, "capsule broker endpoint")

	producer_transport = {
		"CAPSULE_BROKER_TRANSPORT_REVISION_1": 1,
		"CAPSULE_BROKER_TRANSPORT_REVISION": 2,
		"CAPSULE_BROKER_DIGEST_SHA256": 1,
		"CAPSULE_BROKER_DIGEST_SIZE": 32,
		"CAPSULE_BROKER_RESULT_PENDING": 0xffffffff,
		"CAPSULE_BROKER_STATUS_PENDING": 0xffffffff,
		"CAPSULE_BROKER_RESULT_SUCCESS": 0,
		"CAPSULE_BROKER_RESULT_EXECUTION": 1,
		"CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS": 0,
		"CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL": 1,
		"CAPSULE_BROKER_TRANSPORT_EXECUTE": 1,
		"CAPSULE_BROKER_TRANSPORT_READ_INFO": 2,
		"CAPSULE_BROKER_INFO_STATE_LAST_ATTEMPT_STATUS_VALID": 1,
		"CAPSULE_BROKER_INFO_STATE_LAST_ATTEMPT_VERSION_VALID": 2,
		"CAPSULE_BROKER_INFO_STATE_VALID_FLAGS": 3,
		"CAPSULE_BROKER_TRANSPORT_REQUEST_OFFSET": 0,
		"CAPSULE_BROKER_TRANSPORT_INTENT_OFFSET": 40,
		"CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET": 128,
		"CAPSULE_BROKER_TRANSPORT_INFO_OFFSET": 40,
		"CAPSULE_BROKER_TRANSPORT_SIZE": 168,
	}
	consumer_transport = {
		"CDK2_SYSTEM_FMP_TRANSPORT_REVISION_1": 1,
		"CDK2_SYSTEM_FMP_TRANSPORT_REVISION": 2,
		"CDK2_SYSTEM_FMP_DIGEST_SHA256": 1,
		"CDK2_SYSTEM_FMP_DIGEST_SIZE": 32,
		"CDK2_SYSTEM_FMP_RESULT_PENDING": 0xffffffff,
		"CDK2_SYSTEM_FMP_RESULT_SUCCESS": 0,
		"CDK2_SYSTEM_FMP_RESULT_EXECUTION": 1,
		"CDK2_SYSTEM_FMP_LAST_ATTEMPT_SUCCESS": 0,
		"CDK2_SYSTEM_FMP_LAST_ATTEMPT_UNSUCCESSFUL": 1,
		"CDK2_SYSTEM_FMP_TRANSPORT_EXECUTE": 1,
		"CDK2_SYSTEM_FMP_TRANSPORT_READ_INFO": 2,
		"CDK2_SYSTEM_FMP_INFO_STATE_LAST_ATTEMPT_STATUS_VALID": 1,
		"CDK2_SYSTEM_FMP_INFO_STATE_LAST_ATTEMPT_VERSION_VALID": 2,
		"CDK2_SYSTEM_FMP_INFO_STATE_VALID_FLAGS": 3,
		"CDK2_SYSTEM_FMP_REQUEST_OFFSET": 0,
		"CDK2_SYSTEM_FMP_INTENT_OFFSET": 40,
		"CDK2_SYSTEM_FMP_RESULT_OFFSET": 128,
		"CDK2_SYSTEM_FMP_INFO_OFFSET": 40,
		"CDK2_SYSTEM_FMP_TRANSPORT_SIZE": 168,
	}
	require_constants(broker, producer_transport, "coreboot capsule transport")
	require_constants(transport, consumer_transport, "CDK2 capsule transport")
	require_constants(authvar, {
		"PAYLOAD_MM_FMP_CAPSULE_INTENT_REVISION": 1,
		"PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256": 1,
		"PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE": 32,
		"PAYLOAD_MM_FMP_CAPSULE_CHECK": 1,
		"PAYLOAD_MM_FMP_CAPSULE_SET": 2,
	}, "coreboot capsule intent")
	require_constants(transport, {
		"CDK2_SYSTEM_FMP_INTENT_REVISION": 1,
		"CDK2_SYSTEM_FMP_CHECK": 1,
		"CDK2_SYSTEM_FMP_SET": 2,
	}, "CDK2 capsule intent")

	request_fields = ("uint32_t revision", "uint32_t size", "uint32_t operation",
		"uint32_t flags", "uint64_t generation", "uint64_t transaction",
		"uint32_t intent_size", "uint32_t result_size")
	consumer_request_fields = tuple(field.replace("uint32_t", "UINT32").replace(
		"uint64_t", "UINT64") for field in request_fields)
	require_exact_struct(broker, "capsule_broker_transport_request", request_fields,
		"__aligned(8)")
	require_exact_struct(transport, "cdk2_system_fmp_request", consumer_request_fields,
		"__aligned(8)")
	intent_fields = ("uint32_t revision", "uint32_t size", "uint32_t operation",
		"uint32_t flags", "uint64_t broker_generation", "uint64_t transaction",
		"uint64_t capsule_size", "uint32_t digest_algorithm", "uint32_t digest_size",
		"uint8_t digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE]",
		"uint32_t attempted_version", "uint32_t reserved")
	require_exact_struct(authvar, "payload_mm_fmp_capsule_intent", intent_fields,
		"__aligned(8)")
	consumer_intent_fields = tuple(field.replace("uint32_t", "UINT32").replace(
		"uint64_t", "UINT64").replace("uint8_t", "UINT8").replace(
			"PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE", "CDK2_SYSTEM_FMP_DIGEST_SIZE")
		for field in intent_fields)
	require_exact_struct(transport, "cdk2_system_fmp_intent", consumer_intent_fields,
		"__aligned(8)")
	result_fields = ("uint32_t revision", "uint32_t size", "uint64_t generation",
		"uint64_t transaction", "uint32_t attempted_version", "uint32_t reserved",
		"uint32_t last_attempt_status", "uint32_t result")
	require_exact_struct(broker, "capsule_broker_transport_result", result_fields,
		"__aligned(8)")
	require_exact_struct(transport, "cdk2_system_fmp_result", tuple(
		field.replace("uint32_t", "UINT32").replace("uint64_t", "UINT64")
		for field in result_fields), "__aligned(8)")
	info_fields = ("uint32_t revision", "uint32_t size", "uint64_t generation",
		"uint64_t transaction", "guid_t image_type", "uint64_t hardware_instance",
		"uint32_t current_version", "uint32_t lowest_supported_version",
		"uint32_t image_size", "uint32_t capabilities", "uint32_t state_flags",
		"uint32_t last_attempt_version", "uint32_t last_attempt_status",
		"uint32_t reserved[12]", "uint32_t result")
	require_exact_struct(broker, "capsule_broker_transport_info", info_fields,
		"__aligned(8)")
	consumer_info_fields = tuple(field.replace("uint32_t", "UINT32").replace(
		"uint64_t", "UINT64").replace("guid_t", "EFI_GUID") for field in info_fields)
	require_exact_struct(transport, "cdk2_system_fmp_transport_info",
		consumer_info_fields, "__aligned(8)")

	require_layout(TransportRequest, 40, {
		"generation": 16, "transaction": 24, "intent_size": 32,
	}, "capsule transport request", 8)
	require_layout(CapsuleIntent, 88, {
		"broker_generation": 16, "capsule_size": 32, "digest_algorithm": 40,
		"digest": 48, "attempted_version": 80,
	}, "capsule intent", 8)
	require_layout(TransportResult, 40, {
		"generation": 8, "transaction": 16, "last_attempt_status": 32, "result": 36,
	}, "capsule transport result", 8)
	require_layout(TransportInfo, 128, {
		"generation": 8, "image_type": 24, "hardware_instance": 40,
		"current_version": 48, "state_flags": 64, "reserved": 76, "result": 124,
	}, "capsule READ_INFO response", 8)
	if (ctypes.sizeof(TransportRequest) + ctypes.sizeof(CapsuleIntent) != 128 or
	    ctypes.sizeof(TransportRequest) + ctypes.sizeof(TransportInfo) != 168 or
	    128 + ctypes.sizeof(TransportResult) != 168):
		raise AbiMismatch("capsule 168-byte transport composition model changed")

	for source, patterns in ((cb, (
		(r"sizeof\(struct lb_capsule_update_region\) == 32", "producer region size"),
		(r"sizeof\(struct lb_capsule_handoff\) == 112", "producer handoff size"),
		(r"offsetof\(struct lb_capsule_handoff, image_size\) == 72", "producer handoff offset"),
		(r"offsetof\(struct lb_capsule_handoff, regions\) == 112", "producer region offset"),
		(r"sizeof\(struct lb_capsule_broker_endpoint\) == 80", "producer endpoint size"),
		(r"offsetof\(struct lb_capsule_broker_endpoint, reserved\) == 68", "producer endpoint offset"),
	)), (cdk, (
		(r"sizeof\(struct cb_capsule_update_region\) == 32U", "consumer region size"),
		(r"sizeof\(struct cb_capsule_handoff\) == 112U", "consumer handoff size"),
		(r"OFFSET_OF\(struct cb_capsule_handoff, regions\) == 112U", "consumer region offset"),
		(r"sizeof\(struct cb_capsule_broker_endpoint\) == 80U", "consumer endpoint size"),
		(r"OFFSET_OF\(struct cb_capsule_broker_endpoint, reserved\) == 68U", "consumer endpoint offset"),
	)), (broker, (
		(r"sizeof\(struct capsule_broker_transport_request\) == 40",
			"producer request size"),
		(r"offsetof\(struct capsule_broker_transport_request, intent_size\) == 32",
			"producer request offset"),
		(r"sizeof\(struct capsule_broker_transport_result\) == 40",
			"producer result size"),
		(r"offsetof\(struct capsule_broker_transport_result, result\) == 36",
			"producer result offset"),
		(r"sizeof\(struct capsule_broker_transport_info\) == 128",
			"producer info size"),
		(r"offsetof\(struct capsule_broker_transport_info, result\) == 124",
			"producer info result offset"),
	)), (authvar, (
		(r"sizeof\(struct payload_mm_fmp_capsule_intent\) == 88",
			"producer intent size"),
		(r"offsetof\(struct payload_mm_fmp_capsule_intent, attempted_version\) == 80",
			"producer intent offset"),
	)), (transport, (
		(r"sizeof\(struct cdk2_system_fmp_request\) == 40U", "consumer request size"),
		(r"sizeof\(struct cdk2_system_fmp_intent\) == 88U", "consumer intent size"),
		(r"sizeof\(struct cdk2_system_fmp_result\) == 40U", "consumer result size"),
		(r"sizeof\(struct cdk2_system_fmp_transport_info\) == 128U", "consumer info size"),
		(r"offsetof\(struct cdk2_system_fmp_transport_info, result\) == 124U",
			"consumer info result offset"),
	))):
		for pattern, description in patterns:
			require_unique(source, pattern, description)


def validate(coreboot, cdk2, hostile=False):
	paths = {
		"cb": coreboot / "src/commonlib/include/commonlib/coreboot_tables.h",
		"payload": coreboot / "src/include/console/payload_spi_console.h",
		"cdk": cdk2 / "include/coreboot_tables.h",
		"handoff": cdk2 / "src/boot/coreboot_handoff.c",
		"diagnostic": cdk2 / "src/lib/diagnostic.c",
		"test": cdk2 / "src/boot/coreboot_test.c",
		"broker": coreboot / "src/include/boot/capsule_broker.h",
		"authvar": coreboot / "src/include/boot/payload_mm_authvar.h",
		"transport": cdk2 / "include/cdk2/system_fmp_transport.h",
		"dma_producer": coreboot / "src/commonlib/include/commonlib/dma_handoff.h",
		"dma_consumer": cdk2 / "include/cdk2/dma_handoff.h",
	}
	sources = {name: path.read_text() for name, path in paths.items()}
	coreboot_includes = (coreboot / "src/include", coreboot / "src/commonlib/include",
		coreboot / "src/commonlib/bsd/include")

	def preprocess_all(raw):
		with tempfile.TemporaryDirectory(prefix="cdk2-abi-") as temporary:
			configuration = pathlib.Path(temporary) / "cdk2/config.h"
			configuration.parent.mkdir()
			macros = sorted(set(re.findall(r"\bCONFIG_[A-Z0-9_]+\b",
				"\n".join(raw[name] for name in
					("cdk", "handoff", "diagnostic", "test", "transport")))))
			configuration.write_text("".join(f"#define {name} 1\n" for name in macros))
			cdk_includes = (cdk2 / "include", cdk2 / "src/boot", cdk2 / "src/lib",
				pathlib.Path(temporary))
			processed = {}
			for name, source in raw.items():
				includes = coreboot_includes if name in (
					"cb", "payload", "broker", "authvar", "dma_producer") else cdk_includes
				processed[name] = toolchain(source, includes, True)
			return processed

	producer_assertions = (
		("cb", "sizeof(struct lb_local_apic_timer_info) == 20",
			"sizeof(struct lb_local_apic_timer_info) == 21", "local APIC size"),
		("cb", "offsetof(struct lb_local_apic_timer_info, frequency_hz) == 12",
			"offsetof(struct lb_local_apic_timer_info, frequency_hz) == 13",
			"local APIC offset"),
		("cb", "sizeof(struct lb_payload_spi_console) == 36",
			"sizeof(struct lb_payload_spi_console) == 37", "SPI record size"),
		("cb", "offsetof(struct lb_payload_spi_console, com_buffer) == 12",
			"offsetof(struct lb_payload_spi_console, com_buffer) == 13",
			"SPI buffer offset"),
		("cb", "offsetof(struct lb_payload_spi_console, apm_cmd) == 32",
			"offsetof(struct lb_payload_spi_console, apm_cmd) == 33",
			"SPI command offset"),
		("cb", "sizeof(struct lb_capsule_update_region) == 32",
			"sizeof(struct lb_capsule_update_region) == 33", "capsule region size"),
		("cb", "sizeof(struct lb_capsule_handoff) == 112",
			"sizeof(struct lb_capsule_handoff) == 113", "capsule handoff size"),
		("cb", "offsetof(struct lb_capsule_handoff, image_size) == 72",
			"offsetof(struct lb_capsule_handoff, image_size) == 73",
			"capsule image offset"),
		("cb", "offsetof(struct lb_capsule_handoff, regions) == 112",
			"offsetof(struct lb_capsule_handoff, regions) == 113",
			"capsule regions offset"),
		("cb", "sizeof(struct lb_capsule_broker_endpoint) == 80",
			"sizeof(struct lb_capsule_broker_endpoint) == 81", "endpoint size"),
		("cb", "offsetof(struct lb_capsule_broker_endpoint, reserved) == 68",
			"offsetof(struct lb_capsule_broker_endpoint, reserved) == 69",
			"endpoint reserved offset"),
		("broker", "sizeof(struct capsule_broker_transport_request) == 40",
			"sizeof(struct capsule_broker_transport_request) == 41", "request size"),
		("broker", "offsetof(struct capsule_broker_transport_request, intent_size) == 32",
			"offsetof(struct capsule_broker_transport_request, intent_size) == 33",
			"request offset"),
		("broker", "sizeof(struct capsule_broker_transport_result) == 40",
			"sizeof(struct capsule_broker_transport_result) == 41", "result size"),
		("broker", "offsetof(struct capsule_broker_transport_result, result) == 36",
			"offsetof(struct capsule_broker_transport_result, result) == 37",
			"result offset"),
		("broker", "sizeof(struct capsule_broker_transport_info) == 128",
			"sizeof(struct capsule_broker_transport_info) == 129", "READ_INFO size"),
		("broker", "offsetof(struct capsule_broker_transport_info, result) == 124",
			"offsetof(struct capsule_broker_transport_info, result) == 125",
			"READ_INFO result offset"),
		("authvar", "sizeof(struct payload_mm_fmp_capsule_intent) == 88",
			"sizeof(struct payload_mm_fmp_capsule_intent) == 89", "intent size"),
		("authvar", "offsetof(struct payload_mm_fmp_capsule_intent, attempted_version) == 80",
			"offsetof(struct payload_mm_fmp_capsule_intent, attempted_version) == 81",
			"intent offset"),
	)
	consumer_table_constants = {
		"CB_TAG_LOCAL_APIC_TIMER_INFO": 0x004e,
		"CB_TAG_PAYLOAD_SPI_CONSOLE": 0x0050,
		"CB_TAG_CAPSULE_HANDOFF": 0x0052,
		"CB_TAG_CAPSULE_BROKER_ENDPOINT": 0x0054,
		"CB_CAPSULE_HANDOFF_REVISION": 2,
		"CB_CAPSULE_HANDOFF_AUTHENTICATED": 1,
		"CB_CAPSULE_HANDOFF_RESET_REQUIRED": 2,
		"CB_CAPSULE_HANDOFF_REQUIRED_FLAGS": 3,
		"CB_CAPSULE_BROKER_COREBOOT_UPDATE": 1,
		"CB_CAPSULE_BROKER_APPLY_REGIONS": 1,
		"CB_CAPSULE_BROKER_PRESERVE_UNLISTED": 2,
		"CB_CAPSULE_BROKER_VERIFY_READBACK": 4,
		"CB_CAPSULE_BROKER_REQUIRED_CAPABILITIES": 7,
		"CB_CAPSULE_FORMAT_FMP_V3": 3,
		"CB_CAPSULE_AUTH_EFI_PKCS7": 1,
		"CB_CAPSULE_BOARD_BINDING_CBFS_BUILD_INFO_V1": 1,
		"CB_CAPSULE_PAYLOAD_MSS1_V1": 1,
		"CB_CAPSULE_FLAGS_PERSIST_RESET": 0x00050000,
		"CB_CAPSULE_REGION_BIOS": 1,
		"CB_CAPSULE_REGION_VALID_FLAGS": 1,
		"CB_CAPSULE_BROKER_ENDPOINT_REVISION": 1,
		"CB_CAPSULE_ENDPOINT_COREBOOT_SMM_OWNER": 1,
		"CB_CAPSULE_ENDPOINT_SMM_ONLY_SPI": 2,
		"CB_CAPSULE_ENDPOINT_FIXED_COMMUNICATION": 4,
		"CB_CAPSULE_ENDPOINT_DMA_PROTECTED": 8,
		"CB_CAPSULE_ENDPOINT_CPU_RENDEZVOUS": 16,
		"CB_CAPSULE_ENDPOINT_ONE_SHOT": 32,
		"CB_CAPSULE_ENDPOINT_NO_RAW_FLASH": 64,
		"CB_CAPSULE_ENDPOINT_REQUIRED_FLAGS": 127,
		"CB_CAPSULE_ENDPOINT_TRANSPORT_APM_IO8": 1,
	}
	consumer_table_layouts = (
		("cb_local_apic_timer_info", 20, 1, {"frequency_hz": 12}),
		("cb_payload_spi_console", 36, 1, {"com_buffer": 12, "apm_cmd": 32}),
		("cb_capsule_update_region", 32, 1, {
			"image_offset": 0, "flash_offset": 8, "size": 16, "flags": 24,
			"reserved": 28,
		}),
		("cb_capsule_handoff", 112, 1, {
			"tag": 0, "size": 4, "revision": 8, "header_size": 10, "flags": 12,
			"broker_type": 16, "capsule_format": 18, "authentication_format": 20,
			"board_binding_format": 22, "payload_format": 24, "reserved16": 26,
			"broker_capabilities": 28, "image_type_guid": 32, "version": 48,
			"lowest_supported_version": 52, "capsule_flags": 56, "block_size": 60,
			"erase_size": 64, "region_count": 68, "image_size": 72,
			"boot_media_size": 80, "smmstore_offset": 88, "smmstore_size": 96,
			"reserved32": 104, "regions": 112,
		}),
		("cb_capsule_broker_endpoint", 80, 1, {
			"tag": 0, "size": 4, "revision": 8, "header_size": 10, "flags": 12,
			"generation": 16, "communication_base": 24, "communication_size": 32,
			"message_size": 36, "staging_base": 40, "staging_size": 48,
			"transport": 56, "trigger_width": 58, "trigger_address": 60,
			"trigger_value": 64, "reserved": 68,
		}),
	)
	consumer_table_types = {
		"cb_local_apic_timer_info": {"revision": "UINT16", "frequency_hz": "UINT64"},
		"cb_payload_spi_console": {
			"request_header_size": "UINT16", "com_buffer": "UINT64",
			"reserved": "UINT8[3]",
		},
		"cb_capsule_update_region": {
			"image_offset": "struct cbuint64", "flash_offset": "struct cbuint64",
			"size": "struct cbuint64", "flags": "UINT32", "reserved": "UINT32",
		},
		"cb_capsule_handoff": {
			"tag": "UINT32", "size": "UINT32", "revision": "UINT16",
			"header_size": "UINT16", "flags": "UINT32", "broker_type": "UINT16",
			"capsule_format": "UINT16", "authentication_format": "UINT16",
			"board_binding_format": "UINT16", "payload_format": "UINT16",
			"reserved16": "UINT16", "broker_capabilities": "UINT32",
			"image_type_guid": "UINT8[16]", "version": "UINT32",
			"lowest_supported_version": "UINT32", "capsule_flags": "UINT32",
			"block_size": "UINT32", "erase_size": "UINT32", "region_count": "UINT32",
			"image_size": "struct cbuint64", "boot_media_size": "struct cbuint64",
			"smmstore_offset": "struct cbuint64", "smmstore_size": "struct cbuint64",
			"reserved32": "UINT32[2]",
			"regions": "struct cb_capsule_update_region[]",
		},
		"cb_capsule_broker_endpoint": {
			"tag": "UINT32", "size": "UINT32", "revision": "UINT16",
			"header_size": "UINT16", "flags": "UINT32",
			"generation": "struct cbuint64", "communication_base": "struct cbuint64",
			"communication_size": "UINT32", "message_size": "UINT32",
			"staging_base": "struct cbuint64", "staging_size": "struct cbuint64",
			"transport": "UINT16", "trigger_width": "UINT16",
			"trigger_address": "UINT32", "trigger_value": "UINT32",
			"reserved": "UINT32[3]",
		},
	}
	consumer_transport_constants = {
		"CDK2_SYSTEM_FMP_TRANSPORT_SIZE": 168,
		"CDK2_SYSTEM_FMP_TRANSPORT_REVISION_1": 1,
		"CDK2_SYSTEM_FMP_TRANSPORT_REVISION": 2,
		"CDK2_SYSTEM_FMP_INTENT_REVISION": 1,
		"CDK2_SYSTEM_FMP_DIGEST_SHA256": 1,
		"CDK2_SYSTEM_FMP_DIGEST_SIZE": 32,
		"CDK2_SYSTEM_FMP_TRANSPORT_EXECUTE": 1,
		"CDK2_SYSTEM_FMP_TRANSPORT_READ_INFO": 2,
		"CDK2_SYSTEM_FMP_RESULT_SUCCESS": 0,
		"CDK2_SYSTEM_FMP_RESULT_EXECUTION": 1,
		"CDK2_SYSTEM_FMP_LAST_ATTEMPT_SUCCESS": 0,
		"CDK2_SYSTEM_FMP_LAST_ATTEMPT_UNSUCCESSFUL": 1,
		"CDK2_SYSTEM_FMP_RESULT_PENDING": 0xffffffff,
		"CDK2_SYSTEM_FMP_REQUEST_OFFSET": 0,
		"CDK2_SYSTEM_FMP_INTENT_OFFSET": 40,
		"CDK2_SYSTEM_FMP_RESULT_OFFSET": 128,
		"CDK2_SYSTEM_FMP_INFO_OFFSET": 40,
		"CDK2_SYSTEM_FMP_INFO_STATE_LAST_ATTEMPT_STATUS_VALID": 1,
		"CDK2_SYSTEM_FMP_INFO_STATE_LAST_ATTEMPT_VERSION_VALID": 2,
		"CDK2_SYSTEM_FMP_INFO_STATE_VALID_FLAGS": 3,
		"CDK2_SYSTEM_FMP_CHECK": 1,
		"CDK2_SYSTEM_FMP_SET": 2,
	}
	consumer_transport_layouts = (
		("cdk2_system_fmp_request", 40, 8, {
			"revision": 0, "size": 4, "operation": 8, "flags": 12,
			"generation": 16, "transaction": 24, "intent_size": 32,
			"result_size": 36,
		}),
		("cdk2_system_fmp_intent", 88, 8, {
			"revision": 0, "size": 4, "operation": 8, "flags": 12,
			"broker_generation": 16, "transaction": 24, "capsule_size": 32,
			"digest_algorithm": 40, "digest_size": 44, "digest": 48,
			"attempted_version": 80, "reserved": 84,
		}),
		("cdk2_system_fmp_result", 40, 8, {
			"revision": 0, "size": 4, "generation": 8, "transaction": 16,
			"attempted_version": 24, "reserved": 28, "last_attempt_status": 32,
			"result": 36,
		}),
		("cdk2_system_fmp_transport_info", 128, 8, {
			"revision": 0, "size": 4, "generation": 8, "transaction": 16,
			"image_type": 24, "hardware_instance": 40, "current_version": 48,
			"lowest_supported_version": 52, "image_size": 56, "capabilities": 60,
			"state_flags": 64, "last_attempt_version": 68,
			"last_attempt_status": 72, "reserved": 76, "result": 124,
		}),
	)
	consumer_transport_types = {
		"cdk2_system_fmp_request": {
			"revision": "UINT32", "size": "UINT32", "operation": "UINT32",
			"flags": "UINT32", "generation": "UINT64", "transaction": "UINT64",
			"intent_size": "UINT32", "result_size": "UINT32",
		},
		"cdk2_system_fmp_intent": {
			"revision": "UINT32", "size": "UINT32", "operation": "UINT32",
			"flags": "UINT32", "broker_generation": "UINT64",
			"transaction": "UINT64", "capsule_size": "UINT64",
			"digest_algorithm": "UINT32", "digest_size": "UINT32",
			"digest": "UINT8[CDK2_SYSTEM_FMP_DIGEST_SIZE]",
			"attempted_version": "UINT32", "reserved": "UINT32",
		},
		"cdk2_system_fmp_result": {
			"revision": "UINT32", "size": "UINT32", "generation": "UINT64",
			"transaction": "UINT64", "attempted_version": "UINT32",
			"reserved": "UINT32", "last_attempt_status": "UINT32",
			"result": "UINT32",
		},
		"cdk2_system_fmp_transport_info": {
			"revision": "UINT32", "size": "UINT32", "generation": "UINT64",
			"transaction": "UINT64", "image_type": "EFI_GUID",
			"hardware_instance": "UINT64", "current_version": "UINT32",
			"lowest_supported_version": "UINT32", "image_size": "UINT32",
			"capabilities": "UINT32", "state_flags": "UINT32",
			"last_attempt_version": "UINT32", "last_attempt_status": "UINT32",
			"reserved": "UINT32[12]", "result": "UINT32",
		},
	}
	consumer_transport_extra = (
		'_Static_assert(CDK2_SYSTEM_FMP_INTENT_OFFSET + '
		 'sizeof(struct cdk2_system_fmp_intent) == CDK2_SYSTEM_FMP_RESULT_OFFSET, '
		 '"intent/result composition ABI");',
		'_Static_assert(CDK2_SYSTEM_FMP_RESULT_OFFSET + '
		 'sizeof(struct cdk2_system_fmp_result) == CDK2_SYSTEM_FMP_TRANSPORT_SIZE, '
		 '"result/transport composition ABI");',
		'_Static_assert(CDK2_SYSTEM_FMP_INFO_OFFSET + '
		 'sizeof(struct cdk2_system_fmp_transport_info) == '
		 'CDK2_SYSTEM_FMP_TRANSPORT_SIZE, "info/transport composition ABI");',
	)
	producer_table_constants = {
		name.replace("CB_", "LB_", 1): value
		for name, value in consumer_table_constants.items()
	}
	producer_table_layouts = tuple(
		(name.replace("cb_", "lb_", 1), size, alignment, offsets)
		for name, size, alignment, offsets in consumer_table_layouts[2:])
	producer_table_types = {
		"lb_capsule_update_region": {
			"image_offset": "lb_uint64_t", "flash_offset": "lb_uint64_t",
			"size": "lb_uint64_t", "flags": "uint32_t", "reserved": "uint32_t",
		},
		"lb_capsule_handoff": {
			"tag": "uint32_t", "size": "uint32_t", "revision": "uint16_t",
			"header_size": "uint16_t", "flags": "uint32_t",
			"broker_type": "uint16_t", "capsule_format": "uint16_t",
			"authentication_format": "uint16_t", "board_binding_format": "uint16_t",
			"payload_format": "uint16_t", "reserved16": "uint16_t",
			"broker_capabilities": "uint32_t", "image_type_guid": "uint8_t[16]",
			"version": "uint32_t", "lowest_supported_version": "uint32_t",
			"capsule_flags": "uint32_t", "block_size": "uint32_t",
			"erase_size": "uint32_t", "region_count": "uint32_t",
			"image_size": "lb_uint64_t", "boot_media_size": "lb_uint64_t",
			"smmstore_offset": "lb_uint64_t", "smmstore_size": "lb_uint64_t",
			"reserved32": "uint32_t[2]", "regions": "struct lb_capsule_update_region[]",
		},
		"lb_capsule_broker_endpoint": {
			"tag": "uint32_t", "size": "uint32_t", "revision": "uint16_t",
			"header_size": "uint16_t", "flags": "uint32_t",
			"generation": "lb_uint64_t", "communication_base": "lb_uint64_t",
			"communication_size": "uint32_t", "message_size": "uint32_t",
			"staging_base": "lb_uint64_t", "staging_size": "lb_uint64_t",
			"transport": "uint16_t", "trigger_width": "uint16_t",
			"trigger_address": "uint32_t", "trigger_value": "uint32_t",
			"reserved": "uint32_t[3]",
		},
	}
	producer_broker_constants = {
		"CAPSULE_BROKER_TRANSPORT_REVISION_1": 1,
		"CAPSULE_BROKER_TRANSPORT_REVISION": 2,
		"CAPSULE_BROKER_DIGEST_SHA256": 1,
		"CAPSULE_BROKER_DIGEST_SIZE": 32,
		"CAPSULE_BROKER_RESULT_PENDING": 0xffffffff,
		"CAPSULE_BROKER_STATUS_PENDING": 0xffffffff,
		"CAPSULE_BROKER_RESULT_SUCCESS": 0,
		"CAPSULE_BROKER_RESULT_EXECUTION": 1,
		"CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS": 0,
		"CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL": 1,
		"CAPSULE_BROKER_TRANSPORT_EXECUTE": 1,
		"CAPSULE_BROKER_TRANSPORT_READ_INFO": 2,
		"CAPSULE_BROKER_INFO_STATE_LAST_ATTEMPT_STATUS_VALID": 1,
		"CAPSULE_BROKER_INFO_STATE_LAST_ATTEMPT_VERSION_VALID": 2,
		"CAPSULE_BROKER_INFO_STATE_VALID_FLAGS": 3,
		"CAPSULE_BROKER_TRANSPORT_REQUEST_OFFSET": 0,
		"CAPSULE_BROKER_TRANSPORT_INTENT_OFFSET": 40,
		"CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET": 128,
		"CAPSULE_BROKER_TRANSPORT_INFO_OFFSET": 40,
		"CAPSULE_BROKER_TRANSPORT_SIZE": 168,
	}
	producer_broker_layouts = (
		("capsule_broker_transport_request", 40, 8,
			consumer_transport_layouts[0][3]),
		("capsule_broker_transport_result", 40, 8,
			consumer_transport_layouts[2][3]),
		("capsule_broker_transport_info", 128, 8,
			consumer_transport_layouts[3][3]),
	)
	def native_type(expected):
		return re.sub(r"\bUINT(8|16|32|64)\b", r"uint\1_t", expected)

	producer_broker_types = {
		"capsule_broker_transport_request": {
			field: native_type(expected)
			for field, expected in consumer_transport_types[
				"cdk2_system_fmp_request"].items()
		},
		"capsule_broker_transport_result": {
			field: native_type(expected)
			for field, expected in consumer_transport_types[
				"cdk2_system_fmp_result"].items()
		},
		"capsule_broker_transport_info": {
			field: ("guid_t" if expected == "EFI_GUID" else native_type(expected))
			for field, expected in consumer_transport_types[
				"cdk2_system_fmp_transport_info"].items()
		},
	}
	producer_intent_constants = {
		"PAYLOAD_MM_FMP_CAPSULE_INTENT_REVISION": 1,
		"PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256": 1,
		"PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE": 32,
		"PAYLOAD_MM_FMP_CAPSULE_CHECK": 1,
		"PAYLOAD_MM_FMP_CAPSULE_SET": 2,
	}
	producer_intent_layouts = (("payload_mm_fmp_capsule_intent", 88, 8,
		consumer_transport_layouts[1][3]),)
	producer_intent_types = {
		"payload_mm_fmp_capsule_intent": {
			field: expected.replace("CDK2_SYSTEM_FMP_DIGEST_SIZE",
				"PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE")
			for field, expected in consumer_transport_types["cdk2_system_fmp_intent"].items()
		},
	}
	producer_intent_types["payload_mm_fmp_capsule_intent"] = {
		field: native_type(expected) for field, expected in
		producer_intent_types["payload_mm_fmp_capsule_intent"].items()
	}
	producer_broker_extra = (
		'_Static_assert(CAPSULE_BROKER_TRANSPORT_INTENT_OFFSET + '
		 'sizeof(struct payload_mm_fmp_capsule_intent) == '
		 'CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET, "producer intent composition ABI");',
		'_Static_assert(CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET + '
		 'sizeof(struct capsule_broker_transport_result) == '
		 'CAPSULE_BROKER_TRANSPORT_SIZE, "producer result composition ABI");',
		'_Static_assert(CAPSULE_BROKER_TRANSPORT_INFO_OFFSET + '
		 'sizeof(struct capsule_broker_transport_info) == '
		 'CAPSULE_BROKER_TRANSPORT_SIZE, "producer info composition ABI");',
	)

	def validate_raw(raw):
		processed = preprocess_all(raw)
		validate_sources(processed["cb"], processed["payload"], processed["cdk"],
			processed["handoff"], processed["diagnostic"], processed["test"])
		validate_capsule_sources(processed["cb"], processed["cdk"],
			processed["broker"], processed["authvar"], processed["transport"])
		validate_dma_sources(raw["dma_producer"], raw["dma_consumer"])
		for name in ("cb", "broker", "authvar"):
			toolchain(raw[name], coreboot_includes, False)
		for name, needle, replacement, description in producer_assertions:
			assertion_is_enforced(raw[name], coreboot_includes, needle, replacement,
				description)
		consumer_compiler_probe(raw["cb"], coreboot_includes, producer_table_constants,
			producer_table_layouts, producer_table_types)
		consumer_compiler_probe(raw["authvar"], coreboot_includes,
			producer_intent_constants, producer_intent_layouts, producer_intent_types)
		consumer_compiler_probe(raw["broker"], coreboot_includes,
			producer_broker_constants, producer_broker_layouts, producer_broker_types,
			producer_broker_extra)
		cdk_includes = (cdk2 / "include",)
		consumer_compiler_probe(raw["cdk"], cdk_includes, consumer_table_constants,
			consumer_table_layouts, consumer_table_types)
		consumer_compiler_probe(raw["transport"], cdk_includes,
			consumer_transport_constants, consumer_transport_layouts,
			consumer_transport_types, consumer_transport_extra)

	validate_raw(sources)
	if hostile:
		def accept(changed, description):
			try:
				validate_raw(changed)
			except AbiMismatch as error:
				raise AbiMismatch(f"valid hostile fixture rejected: {description}: {error}")

		def reject(changed, description, expected_error=None):
			try:
				validate_raw(changed)
			except AbiMismatch as error:
				expected = ((expected_error,) if isinstance(expected_error, str)
					else expected_error)
				if expected is not None and not any(item in str(error) for item in expected):
					raise AbiMismatch(
						f"hostile fixture rejected for wrong reason: {description}: {error}")
				return
			raise AbiMismatch(f"hostile ABI mutation escaped validation: {description}")

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
			("cdk", "#define CB_TAG_CAPSULE_HANDOFF 0x0052U",
				"#define CB_TAG_CAPSULE_HANDOFF 0x0055U"),
			("cb", "#define LB_CAPSULE_HANDOFF_REVISION 2",
				"#define LB_CAPSULE_HANDOFF_REVISION 3"),
			("cb", "#define LB_CAPSULE_BROKER_VERIFY_READBACK   (1U << 2)",
				"#define LB_CAPSULE_BROKER_VERIFY_READBACK   (1U << 3)"),
			("cdk", "UINT32 flags;\n\tstruct cbuint64 generation;",
				"UINT32 flags;\n\tUINT32 hostile_endpoint_drift;\n\tstruct cbuint64 generation;"),
			("broker", "uint32_t flags;\n\tuint64_t generation;",
				"uint64_t flags;\n\tuint64_t generation;"),
			("authvar", "uint32_t digest_size;\n\tuint8_t digest[",
				"uint64_t digest_size;\n\tuint8_t digest["),
			("transport", "UINT32 last_attempt_status;\n\tUINT32 result;",
				"UINT64 last_attempt_status;\n\tUINT32 result;"),
			("transport", "UINT32 reserved[12];\n\tUINT32 result;",
				"UINT32 reserved[11];\n\tUINT32 result;"),
			("transport", "#define CDK2_SYSTEM_FMP_TRANSPORT_SIZE 168U",
				"#define CDK2_SYSTEM_FMP_TRANSPORT_SIZE 169U"),
			("cdk", "#define CB_CAPSULE_REGION_BIOS (1U << 0)",
				"#define CB_CAPSULE_REGION_BIOS (1U << 1)"),
			("cdk", "#define CB_CAPSULE_REGION_BIOS (1U << 0)\n", ""),
		)
		for source_name, old, new in mutations:
			changed = dict(sources)
			if old not in changed[source_name]:
				raise AbiMismatch(f"hostile-test fixture missing: {old}")
			changed[source_name] = changed[source_name].replace(old, new, 1)
			reject(changed, old)

		def hidden_type_drift(source, structure_name, old, new):
			definition = re.search(
				rf"struct\s+{structure_name}\s*\{{.*?\}}\s*"
				r"(?:__packed|__aligned\(8\))\s*;", source, re.DOTALL)
			if not definition or old not in definition.group(0):
				raise AbiMismatch(f"hostile-test fixture missing: {structure_name}.{old}")
			hostile_name = "hostile_" + structure_name
			drifted = definition.group(0).replace(
				f"struct {structure_name}", f"struct {hostile_name}", 1).replace(old, new, 1)
			replacement = ("/* " + definition.group(0) + " */\n" +
				f"#define {structure_name} {hostile_name}\n" + drifted)
			return source.replace(definition.group(0), replacement, 1)

		for source_name, structure_name, old, new in (
			("cb", "lb_capsule_update_region", "uint32_t flags;", "int32_t flags;"),
			("cb", "lb_capsule_handoff", "uint32_t version;", "int32_t version;"),
			("cb", "lb_capsule_broker_endpoint", "uint32_t trigger_value;",
				"int32_t trigger_value;"),
			("broker", "capsule_broker_transport_request", "uint32_t operation;",
				"int32_t operation;"),
			("authvar", "payload_mm_fmp_capsule_intent", "uint32_t attempted_version;",
				"int32_t attempted_version;"),
			("broker", "capsule_broker_transport_result", "uint32_t result;",
				"int32_t result;"),
			("broker", "capsule_broker_transport_info", "uint32_t current_version;",
				"int32_t current_version;"),
			("cdk", "cb_capsule_update_region", "UINT32 flags;", "INT32 flags;"),
			("cdk", "cb_capsule_handoff", "UINT32 version;", "INT32 version;"),
			("cdk", "cb_capsule_broker_endpoint", "UINT32 trigger_value;",
				"INT32 trigger_value;"),
			("transport", "cdk2_system_fmp_request", "UINT32 operation;",
				"INT32 operation;"),
			("transport", "cdk2_system_fmp_intent", "UINT32 attempted_version;",
				"INT32 attempted_version;"),
			("transport", "cdk2_system_fmp_result", "UINT32 result;", "INT32 result;"),
			("transport", "cdk2_system_fmp_transport_info", "UINT32 current_version;",
				"INT32 current_version;"),
		):
			changed = dict(sources)
			changed[source_name] = hidden_type_drift(changed[source_name], structure_name,
				old, new)
			reject(changed, f"comment-hidden macro-renamed type drift in {structure_name}",
				"C compilation failed")

		for source_name, structure_name, first, second in (
			("cb", "lb_capsule_update_region", "lb_uint64_t image_offset;",
				"lb_uint64_t flash_offset;"),
			("cb", "lb_capsule_handoff", "uint32_t version;",
				"uint32_t lowest_supported_version;"),
			("cb", "lb_capsule_broker_endpoint", "uint32_t communication_size;",
				"uint32_t message_size;"),
			("broker", "capsule_broker_transport_request", "uint32_t operation;",
				"uint32_t flags;"),
			("authvar", "payload_mm_fmp_capsule_intent", "uint32_t digest_algorithm;",
				"uint32_t digest_size;"),
			("broker", "capsule_broker_transport_result", "uint32_t attempted_version;",
				"uint32_t reserved;"),
			("broker", "capsule_broker_transport_info", "uint32_t current_version;",
				"uint32_t lowest_supported_version;"),
			("cdk", "cb_capsule_update_region", "struct cbuint64 image_offset;",
				"struct cbuint64 flash_offset;"),
			("cdk", "cb_capsule_handoff", "UINT32 version;",
				"UINT32 lowest_supported_version;"),
			("cdk", "cb_capsule_broker_endpoint", "UINT32 communication_size;",
				"UINT32 message_size;"),
			("transport", "cdk2_system_fmp_request", "UINT32 operation;", "UINT32 flags;"),
			("transport", "cdk2_system_fmp_intent", "UINT32 digest_algorithm;",
				"UINT32 digest_size;"),
			("transport", "cdk2_system_fmp_result", "UINT32 attempted_version;",
				"UINT32 reserved;"),
			("transport", "cdk2_system_fmp_transport_info", "UINT32 current_version;",
				"UINT32 lowest_supported_version;"),
		):
			changed = dict(sources)
			ordered = first + "\n\t" + second
			reordered = second + "\n\t" + first
			changed[source_name] = hidden_type_drift(changed[source_name], structure_name,
				ordered, reordered)
			reject(changed, f"comment-hidden macro-renamed field reorder in {structure_name}",
				"C compilation failed")

		changed = dict(sources)
		changed["transport"] = changed["transport"].replace(
			"#define CDK2_SYSTEM_FMP_TRANSPORT_SIZE 168U",
			"#define CDK2_SYSTEM_FMP_TRANSPORT_SIZE 169U", 1)
		changed["transport"] = ("#if 0\n"
			"#define CDK2_SYSTEM_FMP_TRANSPORT_SIZE 168U\n"
			"#endif\n" + changed["transport"])
		reject(changed, "dead expected constant before active drift",
			"CDK2_SYSTEM_FMP_TRANSPORT_SIZE=0xa9")

		changed = dict(sources)
		structure = re.search(
			r"struct\s+cdk2_system_fmp_transport_info\s*\{.*?\}\s*__aligned\(8\)\s*;",
			changed["transport"], re.DOTALL)
		if not structure:
			raise AbiMismatch("hostile-test fixture missing: READ_INFO struct")
		changed["transport"] = changed["transport"].replace(
			"UINT32 reserved[12];", "UINT32 reserved[11];", 1)
		changed["transport"] = ("#if 0\n" + structure.group(0) + "\n#endif\n" +
			changed["transport"])
		reject(changed, "dead expected struct before active drift",
			"struct cdk2_system_fmp_transport_info declaration mismatch")

		changed = dict(sources)
		info_structure = re.search(
			r"struct\s+cdk2_system_fmp_transport_info\s*\{.*?\}\s*__aligned\(8\)\s*;",
			changed["transport"], re.DOTALL)
		info_assertions = re.search(
			r"_Static_assert\(sizeof\(struct cdk2_system_fmp_transport_info\).*?"
			r'"SystemFmp info transport layout"\);', changed["transport"], re.DOTALL)
		if not info_structure or not info_assertions:
			raise AbiMismatch("hostile-test fixture missing: compiled READ_INFO ABI")
		drifted_info = info_structure.group(0).replace(
			"struct cdk2_system_fmp_transport_info", "struct hostile_system_fmp_info",
			1).replace("UINT32 reserved[12];", "UINT32 reserved[11];", 1)
		replacement = ("/* " + info_structure.group(0) + " */\n"
			"#define cdk2_system_fmp_transport_info hostile_system_fmp_info\n" +
			drifted_info + "\n"
			"_Static_assert(sizeof(struct cdk2_system_fmp_transport_info) == 124U, "
			'"hostile compiled size");\n'
			"_Static_assert(offsetof(struct cdk2_system_fmp_transport_info, result) == "
			'120U, "hostile compiled result offset");')
		changed["transport"] = changed["transport"].replace(
			info_structure.group(0), replacement, 1).replace(
			info_assertions.group(0), "/* " + info_assertions.group(0) + " */", 1)
		reject(changed, "compiled macro-renamed READ_INFO drift hidden by comments",
			"C compilation failed")

		changed = dict(sources)
		assertion = re.search(
			r"_Static_assert\(sizeof\(struct capsule_broker_transport_request\) == 40,"
			r".*?\);", changed["broker"], re.DOTALL)
		if not assertion:
			raise AbiMismatch("hostile-test fixture missing: producer request assertion")
		changed["broker"] = changed["broker"].replace(assertion.group(0),
			"/* " + assertion.group(0) + " */", 1)
		reject(changed, "commented required producer assertion",
			"required assertion is not compiler-enforced: request size")

		false_literals = ("0L", "0l", "0U", "0u", "0UL", "0ul", "0LU", "0lu",
			"0LL", "0ll", "0ULL", "0ull", "0LLU", "0llu", "00L", "0x0UL")
		for literal in false_literals:
			changed = dict(sources)
			changed["broker"] = changed["broker"].replace(assertion.group(0),
				f"#if {literal}\n{assertion.group(0)}\n#endif", 1)
			reject(changed, f"dead producer assertion under #if {literal}",
				("producer request size",
				 "required assertion is not compiler-enforced: request size"))

		changed = dict(sources)
		changed["broker"] = changed["broker"].replace(assertion.group(0),
			f"#if +0\n{assertion.group(0)}\n#endif", 1)
		reject(changed, "dead producer assertion under #if +0",
			("producer request size",
			 "required assertion is not compiler-enforced: request size"))

		changed = dict(sources)
		continued_comment = "// hostile continued comment \\\n" + assertion.group(0).replace(
			"\n", " \\\n")
		changed["broker"] = changed["broker"].replace(assertion.group(0),
			continued_comment, 1)
		reject(changed, "producer assertion hidden by spliced line comment",
			("producer request size",
			 "required assertion is not compiler-enforced: request size"))

		for literal in ("1L", "1U", "1UL", "1LU", "1LL", "1ULL", "01L", "0x1UL"):
			changed = dict(sources)
			changed["broker"] = changed["broker"].replace(assertion.group(0),
				f"#if {literal}\n{assertion.group(0)}\n#endif", 1)
			accept(changed, f"active producer assertion under #if {literal}")

		changed = dict(sources)
		changed["broker"] = changed["broker"].replace(assertion.group(0),
			("static const char hostile_assertion[] = \"sizeof(struct "
			 "capsule_broker_transport_request) == 40\";"), 1)
		reject(changed, "required producer assertion text in string literal",
			"required assertion is not compiler-enforced: request size")

		changed = dict(sources)
		changed["broker"] = changed["broker"].replace(assertion.group(0),
			f"#if 0LUU\n{assertion.group(0)}\n#endif", 1)
		reject(changed, "unsupported numeric conditional suffix",
			"C preprocessing failed")

		for literal in ('"unterminated', "'unterminated"):
			changed = dict(sources)
			changed["broker"] = f"static const char hostile[] = {literal};\n" + changed["broker"]
			reject(changed, f"unterminated C literal {literal[0]}",
				"C preprocessing failed")

		changed = dict(sources)
		changed["cdk"] = "#define CB_TAG_CAPSULE_HANDOFF 0x0052U\n" + changed["cdk"]
		reject(changed, "duplicate constant ambiguity",
			"ambiguous duplicate constant CB_TAG_CAPSULE_HANDOFF")


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
