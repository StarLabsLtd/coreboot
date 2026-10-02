#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"

build_and_run()
{
	name=$1
	shift
	cc -std=gnu11 -Wall -Wextra -Werror -D__TEST__ -D__COREBOOT__ \
		-D__RAMSTAGE__ -fno-builtin \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$temporary/include" "$@" \
		"$root/tests/lib/capsule_write_layout_test.c" \
		"$root/src/lib/capsule_write_layout.c" \
		"$root/src/lib/payload_mm_fmp_owner_layout.c" -o "$temporary/$name"
	"$temporary/$name"
}

build_and_run ordinary
build_and_run optimized -O2
build_and_run strict -O2 -Wconversion -Wsign-conversion
build_and_run asan -O1 -g -fsanitize=address -fno-omit-frame-pointer
build_and_run ubsan -O1 -g -fsanitize=undefined -fno-omit-frame-pointer
printf '%s\n' 'Physical capsule write layout O0/O2/strict/ASan/UBSan: PASS (no owner authority)'
