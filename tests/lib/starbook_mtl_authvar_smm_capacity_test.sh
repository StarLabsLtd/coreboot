#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

cp "$root/src/Kconfig" "$temporary/Kconfig"
cat >> "$temporary/Kconfig" <<'EOF'

config TEST_STARBOOK_MTL_AUTHVAR_SMM_CAPACITY
	def_bool y
	select STARLABS_STARBOOK_MTL_AUTHVAR_SMM_CAPACITY
EOF

cp "$root/configs/config.starlabs_starbook_mtl" "$temporary/default.config"
make -s -C "$root" DOTCONFIG="$temporary/default.config" \
	obj="$temporary/obj-default" olddefconfig
! grep -Fq 'CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_SMM_CAPACITY=y' \
	"$temporary/default.config"
grep -Fqx 'CONFIG_SMM_TSEG_SIZE=0x800000' "$temporary/default.config"

cp "$root/configs/config.starlabs_starbook_mtl" "$temporary/authvar.config"
make -s -C "$root" KBUILD_KCONFIG="$temporary/Kconfig" \
	DOTCONFIG="$temporary/authvar.config" obj="$temporary/obj-authvar" olddefconfig
grep -Fqx 'CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_SMM_CAPACITY=y' \
	"$temporary/authvar.config"
grep -Fqx 'CONFIG_SMM_TSEG_SIZE=0x1000000' "$temporary/authvar.config"

python3 - "$root" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])

def source(path):
    return (root / path).read_text()

fsp_headers = (
    source("src/vendorcode/intel/fsp/fsp2_0/meteorlake/x86_32/FspmUpd.h")
    + source("src/vendorcode/intel/fsp/fsp2_0/meteorlake/x86_64/FspmUpd.h")
)
fsp_params = source("src/soc/intel/meteorlake/romstage/fsp_params.c")
memmap = source("src/soc/intel/common/block/systemagent/memmap.c")
resources = source("src/soc/intel/meteorlake/systemagent.c")
device = source("src/include/device/device.h")
bootmem = source("src/lib/bootmem.c")
acpi = source("src/acpi/acpigen_pci_root_resource_producer.c")

checks = {
    "both FSP ABIs document 16 MiB TSEG": r"(?:0x01000000:16MB.*){2}",
    "FSP-M consumes the configured size": r"m_cfg->TsegSize\s*=\s*CONFIG_SMM_TSEG_SIZE;",
    "SMRAM reports the configured size": r"\*size\s*=\s*CONFIG_SMM_TSEG_SIZE;",
    "SA resource uses the configured size": r"size\s*=\s*CONFIG_SMM_TSEG_SIZE;.*?set_mmio_resource\([^;]+tseg_base,\s*size,\s*\"TSEG\"\)",
    "fixed SA ranges use reserved MMIO": r"const struct resource \*mmio_range\([^}]+return fixed_mem_range_flags\([^;]+IORESOURCE_RESERVE\s*\|\s*IORESOURCE_STORED\);",
    "reserved resources enter the OS map": r"memranges_add_resources\(bm,\s*reserved,\s*reserved,\s*BM_MEM_RESERVED\);",
    "reserved memory emits LB reserved/E820 reserved": r"case BM_MEM_RESERVED:\s*return LB_MEM_RESERVED;",
    "ACPI excludes reserved ranges": r"if \(res->flags & IORESOURCE_RESERVE\)\s*continue;",
}
texts = {
    "both FSP ABIs document 16 MiB TSEG": fsp_headers,
    "FSP-M consumes the configured size": fsp_params,
    "SMRAM reports the configured size": memmap,
    "SA resource uses the configured size": resources,
    "fixed SA ranges use reserved MMIO": device,
    "reserved resources enter the OS map": bootmem,
    "reserved memory emits LB reserved/E820 reserved": bootmem,
    "ACPI excludes reserved ranges": acpi,
}
for name, pattern in checks.items():
    if not re.search(pattern, texts[name], re.S):
        raise SystemExit(f"missing production memory-map contract: {name}")
PY
