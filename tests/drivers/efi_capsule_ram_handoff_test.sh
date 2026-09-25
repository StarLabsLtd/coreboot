#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
capsules=${CAPSULE_RAM_HANDOFF_SOURCE:-$root/src/drivers/efi/capsules.c}
legacy=${CAPSULE_RAM_HANDOFF_LEGACY_SOURCE:-$root/src/drivers/efi/capsules_legacy.c}
header=${CAPSULE_RAM_HANDOFF_HEADER:-$root/src/drivers/efi/capsules.h}
kconfig=${CAPSULE_RAM_HANDOFF_KCONFIG:-$root/src/drivers/efi/Kconfig}
makefile=${CAPSULE_RAM_HANDOFF_MAKEFILE:-$root/src/drivers/efi/Makefile.mk}
table=${CAPSULE_RAM_HANDOFF_TABLE_SOURCE:-$root/src/lib/coreboot_table.c}

check_sources()
{
	python3 - "$capsules" "$legacy" "$header" "$kconfig" "$makefile" "$table" <<'PY'
import pathlib
import re
import sys

capsules, legacy, header, kconfig, makefile, table = [
    pathlib.Path(path).read_text() for path in sys.argv[1:]
]

assert "SMMSTORE_CMD_USE_FULL_FLASH" not in capsules
assert "call_smm(" not in capsules
assert "smmstore_lookup_region(" not in capsules
assert capsules.count("smmstore_lookup_read_region(") == 2
assert "SMMSTORE_CMD_USE_FULL_FLASH" in legacy
assert "call_smm(" in legacy

assert re.search(
    r"capsule->tag\s*=\s*LB_TAG_CAPSULE\s*;", capsules
), "LB_TAG_CAPSULE publication missing"
assert "set_boot_mode(LB_BOOT_MODE_FLASH_UPDATE);" in capsules
assert "#if CONFIG(DRIVERS_EFI_CAPSULE_RAM_HANDOFF)" in header
assert re.search(
    r"if \(CONFIG\(DRIVERS_EFI_CAPSULE_RAM_HANDOFF\)\)\s*"
    r"lb_efi_capsules\(head\);", table
), "coreboot table is not gated by the RAM handoff"

handoff = re.search(
    r"config DRIVERS_EFI_CAPSULE_RAM_HANDOFF\n(?P<body>.*?)\nconfig ",
    kconfig,
    re.S,
)
legacy_config = re.search(
    r"config DRIVERS_EFI_UPDATE_CAPSULES\n(?P<body>.*?)\nconfig ",
    kconfig,
    re.S,
)
assert handoff is not None
assert legacy_config is not None
assert "select SMMSTORE_READ_REGION" in handoff.group("body")
assert "depends on SMMSTORE" not in handoff.group("body")
assert "select DRIVERS_EFI_CAPSULE_RAM_HANDOFF" in legacy_config.group("body")

assert re.search(
    r"ramstage-\$\(CONFIG_DRIVERS_EFI_CAPSULE_RAM_HANDOFF\)\s*\+=\s*capsules\.c",
    makefile,
)
assert re.search(
    r"ramstage-\$\(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES\)\s*\+=\s*"
    r"capsules_legacy\.c",
    makefile,
)
PY
}

check_sources
[ "${CAPSULE_RAM_HANDOFF_CHECK_ONLY:-0}" = 1 ] && exit 0

temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

reject()
{
	if CAPSULE_RAM_HANDOFF_SOURCE="$temporary/capsules.c" \
		CAPSULE_RAM_HANDOFF_LEGACY_SOURCE="$temporary/capsules_legacy.c" \
		CAPSULE_RAM_HANDOFF_HEADER="$temporary/capsules.h" \
		CAPSULE_RAM_HANDOFF_KCONFIG="$temporary/Kconfig" \
		CAPSULE_RAM_HANDOFF_MAKEFILE="$temporary/Makefile.mk" \
		CAPSULE_RAM_HANDOFF_TABLE_SOURCE="$temporary/coreboot_table.c" \
		CAPSULE_RAM_HANDOFF_CHECK_ONLY=1 "$0" >/dev/null 2>&1; then
		echo "capsule RAM handoff contract accepted mutation: $1" >&2
		exit 1
	fi
}

copy_sources()
{
	cp "$capsules" "$temporary/capsules.c"
	cp "$legacy" "$temporary/capsules_legacy.c"
	cp "$header" "$temporary/capsules.h"
	cp "$kconfig" "$temporary/Kconfig"
	cp "$makefile" "$temporary/Makefile.mk"
	cp "$table" "$temporary/coreboot_table.c"
}

copy_sources
printf '%s\n' 'SMMSTORE_CMD_USE_FULL_FLASH' >> "$temporary/capsules.c"
reject full-flash-command-in-handoff

copy_sources
sed -i 's/smmstore_lookup_read_region/smmstore_lookup_region/' \
	"$temporary/capsules.c"
reject writable-smmstore-accessor-in-handoff

copy_sources
sed -i 's/capsule->tag = LB_TAG_CAPSULE;/capsule->tag = LB_TAG_UNUSED;/' \
	"$temporary/capsules.c"
reject missing-lb-tag-capsule

copy_sources
sed -i 's/CONFIG(DRIVERS_EFI_CAPSULE_RAM_HANDOFF)/CONFIG(DRIVERS_EFI_UPDATE_CAPSULES)/' \
	"$temporary/coreboot_table.c"
reject legacy-gated-publication

copy_sources
sed -i '/select DRIVERS_EFI_CAPSULE_RAM_HANDOFF/d' "$temporary/Kconfig"
reject legacy-wrapper-without-handoff

configure()
{
	name=$1
	shift
	mkdir -p "$temporary/config-$name" "$temporary/obj-$name"
	cp "$root/configs/config.emulation_qemu_x86_q35_smm_tseg" \
		"$temporary/config-$name/.config"
	printf '%s\n' \
		'CONFIG_ANY_TOOLCHAIN=y' \
		'CONFIG_PAYLOAD_NONE=y' \
		'CONFIG_PAYLOAD_SEABIOS=n' \
		'CONFIG_DRIVERS_EFI_VARIABLE_STORE=y' \
		'CONFIG_DRIVERS_EFI_FW_INFO=y' \
		"$@" >> "$temporary/config-$name/.config"
	make -C "$root" UPDATED_SUBMODULES=1 obj="$temporary/obj-$name" \
		DOTCONFIG="$temporary/config-$name/.config" olddefconfig >/dev/null
}

configure handoff \
	'CONFIG_DRIVERS_EFI_CAPSULE_RAM_HANDOFF=y' \
	'CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=n' \
	'CONFIG_SMMSTORE=n'
grep -qx 'CONFIG_DRIVERS_EFI_CAPSULE_RAM_HANDOFF=y' \
	"$temporary/config-handoff/.config"
grep -qx 'CONFIG_SMMSTORE_READ_REGION=y' "$temporary/config-handoff/.config"
grep -qx '# CONFIG_SMMSTORE is not set' "$temporary/config-handoff/.config"
! grep -qx 'CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y' \
	"$temporary/config-handoff/.config"

handoff_object="$temporary/obj-handoff/ramstage/drivers/efi/capsules.o"
handoff_ramstage="$temporary/obj-handoff/cbfs/fallback/ramstage.debug"
make -C "$root" UPDATED_SUBMODULES=1 obj="$temporary/obj-handoff" \
	DOTCONFIG="$temporary/config-handoff/.config" \
	"$temporary/obj-handoff/build.h" >/dev/null
make -C "$root" UPDATED_SUBMODULES=1 obj="$temporary/obj-handoff" \
	DOTCONFIG="$temporary/config-handoff/.config" "$handoff_object" >/dev/null
! nm -u "$handoff_object" | grep -q ' pm_acpi_smi_cmd_port$'
make -C "$root" UPDATED_SUBMODULES=1 obj="$temporary/obj-handoff" \
	DOTCONFIG="$temporary/config-handoff/.config" "$handoff_ramstage" >/dev/null
nm "$handoff_ramstage" > "$temporary/handoff-ramstage.symbols"
for symbol in \
	efi_parse_capsules \
	lb_efi_capsules \
	smmstore_lookup_read_region; do
	grep -q " [Tt] ${symbol}$" "$temporary/handoff-ramstage.symbols"
done
! grep -q ' [Tt] smmstore_lookup_region$' "$temporary/handoff-ramstage.symbols"
! grep -q ' enable_capsule_smi$' "$temporary/handoff-ramstage.symbols"

configure legacy \
	'CONFIG_SMMSTORE=y' \
	'CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y'
grep -qx 'CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y' "$temporary/config-legacy/.config"
grep -qx 'CONFIG_DRIVERS_EFI_CAPSULE_RAM_HANDOFF=y' "$temporary/config-legacy/.config"
grep -qx 'CONFIG_SMMSTORE_FULL_FLASH_ACCESS=y' "$temporary/config-legacy/.config"

legacy_object="$temporary/obj-legacy/ramstage/drivers/efi/capsules_legacy.o"
make -C "$root" UPDATED_SUBMODULES=1 obj="$temporary/obj-legacy" \
	DOTCONFIG="$temporary/config-legacy/.config" \
	"$temporary/obj-legacy/build.h" >/dev/null
make -C "$root" UPDATED_SUBMODULES=1 obj="$temporary/obj-legacy" \
	DOTCONFIG="$temporary/config-legacy/.config" "$legacy_object" >/dev/null
nm -u "$legacy_object" | grep -q ' pm_acpi_smi_cmd_port$'

printf '%s\n' 'EFI capsule RAM handoff tests: PASS'
