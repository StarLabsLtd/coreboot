#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-authvar-mor.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"

compile()
{
	optimization="$1" source="$2" output="$3"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_mor_test.c" "$source" -o "$output"
}

run_source()
{
	name="$1" source="$2" must_fail="$3"
	if [ "$must_fail" = 1 ] &&
	   cmp -s "$source" "$root/src/lib/payload_mm_authvar_mor.c"; then
		echo "ERROR: $name mutation was not applied" >&2
		exit 1
	fi
	for optimization in 0 2; do
		output="$temporary/$name-O$optimization"
		compile "$optimization" "$source" "$output"
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

source="$root/src/lib/payload_mm_authvar_mor.c"
run_source baseline "$source" 0

mutant="$temporary/support.c"
sed 's/snapshot->trusted_platform_support ?/false ?/' \
	"$source" > "$mutant"
run_source support "$mutant" 1
mutant="$temporary/shape.c"
sed 's/request->attributes != MOR_ATTRIBUTES/false/' "$source" > "$mutant"
run_source shape "$mutant" 1
mutant="$temporary/lock.c"
sed 's/state->lock_state != PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED/false/' \
	"$source" > "$mutant"
run_source lock "$mutant" 1
mutant="$temporary/key.c"
sed 's/return !difference;/return true;/' \
	"$source" > "$mutant"
run_source key "$mutant" 1
mutant="$temporary/commit.c"
sed 's/plan->requires_durable_commit \&\& !durable_commit/false/' \
	"$source" > "$mutant"
run_source commit "$mutant" 1
mutant="$temporary/pending.c"
sed 's/!state->supported || !state->entry_clear_pending || state->control_dirty/!state->supported/' \
	"$source" > "$mutant"
run_source pending "$mutant" 1
mutant="$temporary/dirty.c"
sed 's/!state->supported || !state->entry_clear_pending || state->control_dirty/!state->supported || !state->entry_clear_pending/' \
	"$source" > "$mutant"
run_source dirty "$mutant" 1
mutant="$temporary/preserve.c"
sed 's/^[[:space:]]*control_value \& (uint8_t)~1U);/			0U);/' \
	"$source" > "$mutant"
run_source preserve "$mutant" 1
mutant="$temporary/range.c"
sed 's/(uintptr_t)pointer <= UINTPTR_MAX - (size - 1U)/true/' \
	"$source" > "$mutant"
run_source range "$mutant" 1
mutant="$temporary/integrity.c"
sed 's/memcmp(\&expected, plan, sizeof(expected))/false/' "$source" > "$mutant"
run_source integrity "$mutant" 1
mutant="$temporary/generation.c"
sed 's/state->generation == UINT64_MAX/false/' "$source" > "$mutant"
run_source generation "$mutant" 1
mutant="$temporary/lock-enum-lower.c"
sed 's/state->lock_state < PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED/false/' \
	"$source" > "$mutant"
run_source lock-enum-lower "$mutant" 1
mutant="$temporary/phase-enum-lower.c"
sed 's/snapshot->phase < PAYLOAD_MM_AUTHVAR_MOR_INIT_END_OF_DXE/false/' \
	"$source" > "$mutant"
run_source phase-enum-lower "$mutant" 1
mutant="$temporary/key-destroy-result.c"
sed 's/return PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_PUBLISHED_KEY_DESTROYED;/return PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_REJECTED;/' \
	"$source" > "$mutant"
run_source key-destroy-result "$mutant" 1
mutant="$temporary/ready-failure-result.c"
sed 's/return PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_PUBLISHED_READY_CLEAR_FAILED;/return PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_REJECTED;/' \
	"$source" > "$mutant"
run_source ready-failure-result "$mutant" 1
mutant="$temporary/observed-current.c"
sed 's/plan->observed_control_value = control_value;/plan->observed_control_value = 0U;/' \
	"$source" > "$mutant"
run_source observed-current "$mutant" 1

printf '%s\n' 'Payload-MM authenticated-variable MOR tests: PASS'
