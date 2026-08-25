#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fmd="$root/src/mainboard/starlabs/starfighter/variants/mtl/board.fmd"
overlay="$root/configs/config.starlabs_starbook_mtl.spi_flash_console"
base="$root/configs/config.starlabs_starbook_mtl"

grep -Eq 'SMMSTORE@0x30000[[:space:]]+0x80000' "$fmd"
grep -Eq 'CONSOLE@0xB0000[[:space:]]+0x20000' "$fmd"
grep -q '^CONFIG_CONSOLE_SERIAL=y$' "$overlay"
grep -q '^CONFIG_CONSOLE_CBMEM=y$' "$overlay"
grep -q '^CONFIG_CONSOLE_SPI_FLASH=y$' "$overlay"
grep -q '^CONFIG_CONSOLE_SPI_FLASH_BUFFER_SIZE=0x20000$' "$overlay"
if grep -q '^CONFIG_CONSOLE_SPI_FLASH=y$' "$base"; then
	printf '%s\n' 'error: normal StarBook MTL profile must keep SPI flash console disabled' >&2
	exit 1
fi

printf '%s\n' 'StarBook MTL SPI console layout/profile validation passed'
