#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

configure()
{
	name=$1
	shift
	mkdir -p "$temporary/config-$name" "$temporary/build-$name"
	cp "$root/configs/config.emulation_qemu_x86_q35_smm_tseg" \
		"$temporary/config-$name/.config"
	printf '%s\n' "$@" >> "$temporary/config-$name/.config"
	make -C "$root" obj="$temporary/build-$name" \
		DOTCONFIG="$temporary/config-$name/.config" olddefconfig >/dev/null
}

configure disabled '# CONFIG_SMMSTORE is not set'
grep -qx '# CONFIG_SMMSTORE is not set' "$temporary/config-disabled/.config"
! grep -q '^CONFIG_SMMSTORE_READ_REGION=' "$temporary/config-disabled/.config"
! grep -q '^CONFIG_SMMSTORE_\(SIZE\|BLOCK_SIZE\|FULL_FLASH_ACCESS\)=' \
	"$temporary/config-disabled/.config"

configure writable 'CONFIG_SMMSTORE=y'
grep -qx 'CONFIG_SMMSTORE=y' "$temporary/config-writable/.config"
grep -qx 'CONFIG_SMMSTORE_READ_REGION=y' "$temporary/config-writable/.config"
grep -qx 'CONFIG_SMMSTORE_SIZE=0x80000' "$temporary/config-writable/.config"
grep -qx 'CONFIG_SMMSTORE_BLOCK_SIZE=65536' "$temporary/config-writable/.config"

printf '%s\n' 'SMMSTORE read-only Kconfig matrix tests: PASS'
