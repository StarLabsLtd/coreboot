#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 4' > "$temporary/include/config.h"

build_run()
{
	name=$1
	guid=$2
	version=$3
	lsv=$4
	localversion=$5
	expected_version=$6
	expected_lsv=$7
	invalid=$8
	shift 8
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-DCONFIG_ROM_SIZE=0x1000000 \
		-DCONFIG_DRIVERS_EFI_MAIN_FW_GUID="\"$guid\"" \
		-DCONFIG_DRIVERS_EFI_MAIN_FW_VERSION="$version" \
		-DCONFIG_DRIVERS_EFI_MAIN_FW_LSV="$lsv" \
		-DCONFIG_LOCALVERSION="\"$localversion\"" \
		-DTEST_VERSION="$expected_version" -DTEST_LSV="$expected_lsv" \
		-DTEST_INVALID="$invalid" \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/efi_fw_info_test.c" \
		"$root/src/drivers/efi/fw_info.c" -o "$temporary/$name"
	"$temporary/$name"
}

for optimization in ordinary optimized strict sanitized; do
	case "$optimization" in
	ordinary) flags= ;;
	optimized) flags='-O2' ;;
	strict) flags='-O2 -Wconversion -Wsign-conversion' ;;
	sanitized) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all' ;;
	esac
	# shellcheck disable=SC2086
	build_run "$optimization-fixed" good 0x20000 0x10000 ignored \
		0x20000 0x10000 0 $flags
	# shellcheck disable=SC2086
	build_run "$optimization-derived" good 0 0 coreboot-26.09 \
		0x001a0009 0x001a0009 0 $flags
	# Preserve the existing zero-version table ABI when no version parses.
	# shellcheck disable=SC2086
	build_run "$optimization-zero" good 0 0 unversioned 0 0 0 $flags
	# shellcheck disable=SC2086
	build_run "$optimization-invalid" bad 1 1 ignored 0 0 1 $flags
done
printf '%s\n' 'EFI firmware-info getter O0/O2/strict/ASan+UBSan cases: PASS'
