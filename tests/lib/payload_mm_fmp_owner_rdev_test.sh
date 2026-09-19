#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

build_and_run()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/payload_mm_fmp_owner_rdev_test.c" \
		"$root/src/lib/payload_mm_fmp_owner_rdev.c" \
		"$root/src/lib/payload_mm_fmp_owner_layout.c" \
		-o "$temporary/$name"
	"$temporary/$name"
}

build_and_run owner-rdev-o0 -O0
build_and_run owner-rdev-o2 -O2
build_and_run owner-rdev-strict -O2 -Wconversion -Wsign-conversion
build_and_run owner-rdev-asan -O1 -g -fsanitize=address
ASAN_OPTIONS=detect_leaks=0 build_and_run owner-rdev-ubsan -O1 -g \
	-fsanitize=undefined

# Keep the adapter proof adjacent to the journal's exhaustive power-cut model.
"$root/tests/lib/payload_mm_fmp_owner_journal_test.sh"
echo 'Payload-MM FMP owner rdev O0/O2/strict/ASan+UBSan: PASS'
