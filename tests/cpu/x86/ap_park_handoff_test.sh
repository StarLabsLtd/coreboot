#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

build()
{
	bits=$1
	configured=$2
	optimization=$3
	source=$4
	build_name=$5
	flags=$6
	include="$temporary/$configured/include"
	mkdir -p "$include"
	printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
		'#define CONFIG_MAX_CPUS 4' \
		"#define CONFIG_PARALLEL_MP_AP_WORK $configured" > "$include/config.h"
	# Actual CPU translation unit; unrelated CPU sections are not this component's scope.
	# Deliberate splitting of compiler and sanitizer flags.
	# shellcheck disable=SC2086
	${CC:-cc} -m"$bits" -std=gnu11 -O"$optimization" -Wall -Werror \
		-fno-builtin -ffunction-sections -fdata-sections $flags \
		-D__COREBOOT__ -D__TEST__ -D__RAMSTAGE__ -D__ARCH_x86_"$bits"__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$source" "$root/tests/cpu/x86/ap_park_handoff_test.c" \
		-Wl,--gc-sections -o "$temporary/$build_name"
}

for bits in 32 64; do
	for configured in 0 1; do
		for optimization in 0 2; do
			name="park-$bits-$configured-O$optimization"
			build "$bits" "$configured" "$optimization" \
				"$root/src/arch/x86/cpu.c" "$name" ''
			"$temporary/$name"
			build "$bits" "$configured" "$optimization" \
				"$root/src/arch/x86/cpu.c" "$name-sanitize" \
				'-g -fsanitize=address,undefined -fno-sanitize-recover=all'
			ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
				"$temporary/$name-sanitize"
		done
	done
done

# Removing only the admission failure check must fail the exact configured oracle.
sed '/if (mp_park_aps() != CB_SUCCESS)/,+1c\
\tmp_park_aps();' "$root/src/arch/x86/cpu.c" > "$temporary/unchecked.c"
grep -q '^[[:space:]]*mp_park_aps();$' "$temporary/unchecked.c"
build 64 1 2 "$temporary/unchecked.c" unchecked ''
status=0
"$temporary/unchecked" || status=$?
if [ "$status" -ne 3 ]; then
	printf 'unchecked AP admission mutant returned %s, expected 3\n' "$status" >&2
	exit 1
fi

printf '%s\n' 'AP park handoff component tests: PASS (admission only, not completed HLT)'
