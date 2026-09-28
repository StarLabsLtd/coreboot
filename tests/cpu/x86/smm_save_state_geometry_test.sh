#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_INTEL_ADAPTER 1' \
	'#define CONFIG_SMM_SAVE_STATE_GEOMETRY 1' \
	> "$temporary/include/config.h"

for optimization in 0 2; do
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-O"$optimization" -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/cpu/x86/smm_save_state_geometry_test.c" \
		"$root/src/cpu/x86/smm/save_state_geometry.c" \
		"$root/src/soc/intel/common/block/smm/invocation_adapter.c" \
		-o "$temporary/test-O$optimization"
	"$temporary/test-O$optimization"
done

sed '/span->base > ~(uintptr_t)0 - span->size/d' \
	"$root/src/cpu/x86/smm/save_state_geometry.c" > "$temporary/overflow.c"
if cmp -s "$temporary/overflow.c" \
	"$root/src/cpu/x86/smm/save_state_geometry.c"; then
	printf 'overflow mutant did not change source\n' >&2
	exit 1
fi
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
	-O2 -fsanitize=address,undefined -fno-omit-frame-pointer \
	-D__TEST__ -D__COREBOOT__ -include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" \
	"$root/tests/cpu/x86/smm_save_state_geometry_test.c" \
	"$temporary/overflow.c" \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c" \
	-o "$temporary/overflow-mutant"
if ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
	"$temporary/overflow-mutant" >/dev/null 2>&1; then
	printf 'revision-span overflow mutant survived\n' >&2
	exit 1
fi

printf 'SMM save-state geometry tests passed\n'
