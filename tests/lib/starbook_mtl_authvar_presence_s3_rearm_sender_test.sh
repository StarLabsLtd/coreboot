#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_HAVE_ACPI_RESUME 1' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM 1' \
	> "$temporary/include/config.h"
common="-std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -fno-builtin
	-D__COREBOOT__ -D__TEST__ -D__RAMSTAGE__ -include $root/src/include/kconfig.h
	-include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -no-pie"
sources="$root/tests/lib/starbook_mtl_authvar_presence_s3_rearm_sender_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_s3_rearm_sender.c"
for flags in '-O0' '-O2' '-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/test"
	for case in send cold s3; do
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/test" "$case"
	done
	for case in acpi-s3-authority-cold acpi-cold-authority-s3 authority-error; do
		if ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/test" "$case" >/dev/null 2>&1; then
			echo "sender accepted $case" >&2
			exit 1
		fi
	done
done
echo 'StarBook MTL authenticated-variable S3 rearm sender: PASS'
