#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fmd="$root/src/mainboard/starlabs/starfighter/variants/mtl/board.fmd"
overlay="$root/configs/overlays/starlabs_starbook_mtl_spi_flash_console.config"
base="$root/configs/config.starlabs_starbook_mtl"
resolved=$(mktemp)
trap 'rm -f "$resolved"' EXIT

# abuild treats every top-level configs/config.<board>* file as a complete
# configuration.  Keep this opt-in fragment outside that discovery namespace.
if find "$root/configs" -maxdepth 1 \
	-name 'config.starlabs_starbook_mtl*' ! -path "$base" | grep -q .; then
	printf '%s\n' 'error: SPI console overlay collides with abuild config discovery' >&2
	exit 1
fi

grep -Eq 'SMMSTORE@0x30000[[:space:]]+0x80000' "$fmd"
grep -Eq 'CONSOLE@0xB0000[[:space:]]+0x20000' "$fmd"
grep -q '^CONFIG_CONSOLE_SERIAL=y$' "$overlay"
grep -q '^CONFIG_CONSOLE_CBMEM=y$' "$overlay"
grep -q '^CONFIG_CONSOLE_SPI_FLASH=y$' "$overlay"
grep -q '^CONFIG_CONSOLE_SPI_FLASH_BUFFER_SIZE=0x20000$' "$overlay"
grep -q '^CONFIG_BOOTMEDIA_SMM_BWP=n$' "$overlay"
grep -q '^CONFIG_BOOTMEDIA_SMM_BWP_RUNTIME_OPTION=n$' "$overlay"
if grep -q '^CONFIG_CONSOLE_SPI_FLASH=y$' "$base"; then
	printf '%s\n' 'error: normal StarBook MTL profile must keep SPI flash console disabled' >&2
	exit 1
fi

# Resolve the real Kconfig result from the maintained base profile plus the
# opt-in overlay; do not treat the fragment text itself as validation.
cp "$base" "$resolved"
cat "$overlay" >> "$resolved"
make -s -C "$root" olddefconfig KCONFIG_CONFIG="$resolved"
grep -q '^CONFIG_CONSOLE_SERIAL=y$' "$resolved"
grep -q '^CONFIG_CONSOLE_CBMEM=y$' "$resolved"
grep -q '^CONFIG_CONSOLE_SPI_FLASH=y$' "$resolved"
grep -q '^CONFIG_CONSOLE_SPI_FLASH_BUFFER_SIZE=0x20000$' "$resolved"
! grep -q '^CONFIG_BOOTMEDIA_SMM_BWP=y$' "$resolved"
! grep -q '^CONFIG_BOOTMEDIA_SMM_BWP_RUNTIME_OPTION=y$' "$resolved"
grep -q '^CONFIG_BOOT_DEVICE_SPI_FLASH=y$' "$resolved"
grep -q '^CONFIG_BOOT_DEVICE_SPI_FLASH_RW_NOMMAP_EARLY=y$' "$resolved"

printf '%s\n' 'StarBook MTL SPI console layout/profile validation passed'
