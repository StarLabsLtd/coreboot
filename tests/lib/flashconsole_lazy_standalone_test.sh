#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare \
	-D__TEST__ -D__COREBOOT__ -D__SMM__ -fno-builtin \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$root/build" "$root/tests/lib/flashconsole_lazy_standalone_test.c" \
	-o "$temporary/flashconsole-lazy-test"
"$temporary/flashconsole-lazy-test"
printf '%s\n' 'flashconsole lazy standalone tests: PASS'
