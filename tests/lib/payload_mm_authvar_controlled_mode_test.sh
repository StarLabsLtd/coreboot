#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-authvar-controlled.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"

compile()
{
	optimization="$1" source="$2" mode_source="$3" output="$4"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_controlled_mode_test.c" \
		"$source" "$mode_source" \
		"$root/src/lib/payload_mm_authvar_store.c" -o "$output"
}

run_source()
{
	name="$1" source="$2" must_fail="$3"
	if [ "$must_fail" = 1 ] &&
	   cmp -s "$source" "$root/src/lib/payload_mm_authvar_controlled_mode.c"; then
		echo "ERROR: $name mutation was not applied" >&2
		exit 1
	fi
	for optimization in 0 2; do
		output="$temporary/$name-O$optimization"
		compile "$optimization" "$source" \
			"$root/src/lib/payload_mm_authvar_mode.c" "$output"
		if [ "$must_fail" = 1 ]; then
			if ASAN_OPTIONS=detect_leaks=1 "$output" >/dev/null 2>&1; then
				echo "ERROR: $name O$optimization survived" >&2
				exit 1
			fi
		else
			ASAN_OPTIONS=detect_leaks=1 "$output"
		fi
	done
}

run_mode_source()
{
	name="$1" mode_source="$2"
	if cmp -s "$mode_source" "$root/src/lib/payload_mm_authvar_mode.c"; then
		echo "ERROR: $name mutation was not applied" >&2
		exit 1
	fi
	for optimization in 0 2; do
		output="$temporary/$name-O$optimization"
		compile "$optimization" \
			"$root/src/lib/payload_mm_authvar_controlled_mode.c" \
			"$mode_source" "$output"
		if ASAN_OPTIONS=detect_leaks=1 "$output" >/dev/null 2>&1; then
			echo "ERROR: $name O$optimization survived" >&2
			exit 1
		fi
	done
}

source="$root/src/lib/payload_mm_authvar_controlled_mode.c"
run_source baseline "$source" 0

mutant="$temporary/guid.c"
sed '0,/PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE/s//PAYLOAD_MM_AUTHVAR_MODE_KEY_CUSTOM_MODE/' "$source" > "$mutant"
run_source guid "$mutant" 1
mutant="$temporary/name.c"
sed '0,/PAYLOAD_MM_AUTHVAR_CONTROLLED_SECURE_BOOT_ENABLE/s//PAYLOAD_MM_AUTHVAR_CONTROLLED_CUSTOM_MODE/' "$source" > "$mutant"
run_source name "$mutant" 1
mutant="$temporary/append.c"
sed 's/if (attributes != MODE_ATTRIBUTES ||/if ((attributes \& ~PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE) != MODE_ATTRIBUTES ||/' "$source" > "$mutant"
run_source append "$mutant" 1
mutant="$temporary/size.c"
sed 's/payload_size != 1U/payload_size < 1U/' "$source" > "$mutant"
run_source size "$mutant" 1
mutant="$temporary/presence.c"
sed 's/return trusted_physical_presence ?/return (trusted_physical_presence || true) ?/' "$source" > "$mutant"
run_source presence "$mutant" 1
mutant="$temporary/range.c"
sed 's/(uintptr_t)pointer <= UINTPTR_MAX - (size - 1U)/true/' \
	"$root/src/lib/payload_mm_authvar_mode.c" > "$mutant"
run_mode_source range "$mutant"

printf '%s\n' 'Payload-MM authenticated-variable controlled-mode tests: PASS'
