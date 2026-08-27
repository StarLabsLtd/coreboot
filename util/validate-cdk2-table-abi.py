#!/usr/bin/env python3
"""Validate coreboot/CDK2 table tag ownership and shared SPI layout."""

import pathlib
import re
import sys


def constant(source, name):
	match = re.search(rf"(?:#define\s+{name}\s+|{name}\s*=\s*)(0x[0-9a-fA-F]+)", source)
	if not match:
		raise SystemExit(f"missing {name}")
	return int(match.group(1), 16)


if len(sys.argv) != 2:
	raise SystemExit(f"usage: {sys.argv[0]} CDK2_TREE")

coreboot = pathlib.Path(__file__).resolve().parents[1]
cdk2 = pathlib.Path(sys.argv[1]).resolve()
cb = (coreboot / "src/commonlib/include/commonlib/coreboot_tables.h").read_text()
payload = (coreboot / "src/include/console/payload_spi_console.h").read_text()
cdk = (cdk2 / "include/coreboot_tables.h").read_text()

expected = {
	"LB_TAG_PAYLOAD_SPI_CONSOLE": ("CB_TAG_PAYLOAD_SPI_CONSOLE", 0x0050),
}
values = []
for cb_name, (cdk_name, value) in expected.items():
	actual = constant(cb, cb_name)
	peer = constant(cdk, cdk_name)
	if actual != value or peer != value:
		raise SystemExit(f"ABI mismatch: {cb_name}={actual:#06x}, {cdk_name}={peer:#06x}")
	values.append(actual)
if len(values) != len(set(values)):
	raise SystemExit("coreboot table tag collision")

if "uint32_t sequence;" in payload:
	raise SystemExit("v1 SPI request must not expose an unenforced sequence")
print("coreboot/CDK2 table ABI validation passed")
