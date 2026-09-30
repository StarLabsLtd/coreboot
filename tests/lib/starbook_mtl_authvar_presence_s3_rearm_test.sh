#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	> "$temporary/include/config.h"
common="-std=gnu11 -Wall -Wextra -Werror -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -D__SMM__
	-include $root/src/include/kconfig.h -include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -no-pie"
sources="$root/tests/lib/starbook_mtl_authvar_presence_s3_rearm_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_s3_rearm.c"
for profile in o0 o2 ia32-o0 ia32-o2 asan ubsan tsan; do
	case "$profile" in
	o0) flags=-O0 ;;
	o2) flags=-O2 ;;
	ia32-o0) flags='-O0 -m32 -march=i686' ;;
	ia32-o2) flags='-O2 -m32 -march=i686' ;;
	asan) flags='-O1 -g -fsanitize=address -fno-omit-frame-pointer' ;;
	ubsan) flags='-O1 -g -fsanitize=undefined -fno-omit-frame-pointer' ;;
	tsan) flags='-O1 -g -fsanitize=thread -fno-omit-frame-pointer -Wno-tsan' ;;
	esac
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/$profile"
	for case_name in 0 1 2 3 4 5 6; do
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
		TSAN_OPTIONS=halt_on_error=1 "$temporary/$profile" "$case_name"
	done
done
echo 'StarBook MTL authenticated-variable S3 rearm owner O0/O2/IA32/sanitizer/TSan tests: PASS'
