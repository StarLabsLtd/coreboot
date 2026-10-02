#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"

for optimization in 0 2; do
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-Wstrict-prototypes -fno-builtin -O"$optimization" -g \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/capsule_qemu_pflash_test.c" \
		"$root/src/lib/capsule_write_layout.c" -o "$temporary/test-O$optimization"
	for mode in transaction begin-failure begin-mutation unowned end-failure \
		store state crossing overflow alias root-change operation-failure operation-mutation \
		layout-mutation; do
		"$temporary/test-O$optimization" "$mode"
		printf 'Capsule pflash HOST transport O%s %s: PASS\n' "$optimization" "$mode"
	done
done
