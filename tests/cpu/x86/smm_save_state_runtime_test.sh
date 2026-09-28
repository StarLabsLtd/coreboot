#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

extract_runtime()
{
	source=$1
	output=$2
	{
		printf '%s\n' \
			'#include <security/intel/stm/SmmStm.h>' \
			'#include <cpu/x86/smm.h>' \
			'#include <cpu/x86/save_state.h>' \
			'#include <cpu/x86/smm_save_state.h>' \
			'static volatile struct smm_runtime smm_runtime;'
		sed -n '/^void \*smm_get_save_state(int cpu)$/,/^bool smm_region_overlaps_handler(/p' \
			"$source" | sed '$d'
		cat "$root/tests/cpu/x86/smm_save_state_runtime_test.c"
	} > "$output"
}

build_and_run()
{
	name=$1
	stm=$2
	source=$3
	warning_flags=
	if [ "$stm" -eq 0 ]; then
		warning_flags=-Wno-sign-compare
	fi
	mkdir -p "$temporary/$name/include"
	printf '%s\n' \
		'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
		'#define CONFIG_MAX_CPUS 4' \
		"#define CONFIG_STM $stm" \
		> "$temporary/$name/include/config.h"
	for optimization in 0 2; do
		${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
			$warning_flags \
			-O"$optimization" -D__TEST__ -D__COREBOOT__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$temporary/$name/include" -I"$root/src/include" -I"$root/src" \
			-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$source" "$root/src/cpu/x86/smm/save_state_geometry.c" \
			-o "$temporary/$name/test-O$optimization"
		"$temporary/$name/test-O$optimization"
		${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
			$warning_flags \
			-O"$optimization" -fsanitize=address,undefined \
			-fno-omit-frame-pointer -D__TEST__ -D__COREBOOT__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$temporary/$name/include" -I"$root/src/include" -I"$root/src" \
			-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$source" "$root/src/cpu/x86/smm/save_state_geometry.c" \
			-o "$temporary/$name/sanitize-O$optimization"
		ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/$name/sanitize-O$optimization"
	done
}

extract_runtime "$root/src/cpu/x86/smm/smm_module_handler.c" \
	"$temporary/runtime.c"
build_and_run off 0 "$temporary/runtime.c"
build_and_run stm 1 "$temporary/runtime.c"

sed '/smm_save_state_native_span(smm_runtime.save_state_top\[cpu\]/,/STM_PSD_SIZE/ s/STM_PSD_SIZE/0/' \
	"$root/src/cpu/x86/smm/smm_module_handler.c" > "$temporary/no-psd.c"
extract_runtime "$temporary/no-psd.c" "$temporary/no-psd-runtime.c"
if build_and_run no-psd 1 "$temporary/no-psd-runtime.c" >/dev/null 2>&1; then
	printf 'STM exclusion mutant survived\n' >&2
	exit 1
fi

sed 's/SMM_REVISION_OFFSET_FROM_TOP,/SMM_REVISION_OFFSET_FROM_TOP - STM_PSD_SIZE,/' \
	"$root/src/cpu/x86/smm/smm_module_handler.c" > "$temporary/allocation-offset.c"
extract_runtime "$temporary/allocation-offset.c" \
	"$temporary/allocation-offset-runtime.c"
if build_and_run allocation-offset 1 \
	"$temporary/allocation-offset-runtime.c" >/dev/null 2>&1; then
	printf 'allocation-sized revision mutant survived\n' >&2
	exit 1
fi

printf 'SMM runtime save-state STM on/off tests passed\n'
