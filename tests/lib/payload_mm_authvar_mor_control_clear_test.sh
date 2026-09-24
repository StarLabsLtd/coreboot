#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_MOR_POLICY 1' > \
	"$temporary/include/config.h"

cases='mor-success mor-upper-bits mor-recovery-abort mor-recovery-replay
mor-reclaim mor-missing mor-bad-attributes mor-grant-mismatch mor-no-grant
mor-mutate-record-read mor-mutate-record-grant
mor-mutate-copies-read mor-mutate-copies-grant
mor-mutate-canonical-read mor-mutate-canonical-grant'

build_and_run()
{
	output=$1
	source=$2
	flags=$3
	"${CC:-cc}" -std=gnu11 $flags -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_executor_test.c" "$source" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" \
		"$root/src/lib/payload_mm_authvar_mor_grant.c" \
		"$root/src/lib/payload_mm_authvar_mor_identity.c" -o "$output"
	for case_name in $cases; do
		ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$output" "$case_name"
	done
	for fault in $(seq 1 48); do
		ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$output" "mor-fault-$fault"
	done
}

for flags in '-O0' '-O2' '-O1 -fsanitize=address' \
	'-O1 -fsanitize=undefined'; do
	build_and_run "$temporary/test" \
		"$root/src/lib/payload_mm_authvar_executor.c" "$flags"
done

mutant()
{
	name=$1
	expression=$2
	sed "$expression" "$root/src/lib/payload_mm_authvar_executor.c" > \
		"$temporary/$name.c"
	! cmp -s "$root/src/lib/payload_mm_authvar_executor.c" \
		"$temporary/$name.c"
	if build_and_run "$temporary/$name" "$temporary/$name.c" '-O2' \
		>/dev/null 2>&1; then
		echo "ERROR: $name mutation survived" >&2
		exit 1
	fi
	return 0
}

mutant grant-binding \
	's/state->mor_grant.entry.value != control_value/false/'
mutant upper-bit-preservation \
	's/\*data \& ~1U/0U/'
mutant take-before-write \
	's/if (payload_mm_authvar_mor_grant_take/if (false \&\& payload_mm_authvar_mor_grant_take/'
mutant record-byte-seal \
	's/mor_record_snapshot_digests(state, seal) \&\&/true \&\&/'
mutant copies-byte-seal \
	's/mor_copies_snapshot_digests(state, seal, copies_size) \&\&/true \&\&/'
mutant canonical-byte-seal \
	's/mor_canonical_snapshot_digests(state, seal)/true/'

grep -q '^config PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION$' \
	"$root/src/lib/Kconfig"

echo 'Payload-MM authvar MOR control-clear transaction tests: PASS'
