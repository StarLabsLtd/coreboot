#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"

compile()
{
	optimization="$1"
	executor_source="$2"
	semantics_source="$3"
	binary="$4"
	shim="$binary-source.inc"
	sed '$d' "$root/tests/lib/payload_mm_authvar_read_transaction_source.inc" > "$shim"
	printf '#include "%s"\n' "$executor_source" >> "$shim"
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -pthread \
		-D__TEST__ -D__COREBOOT__ \
		-DEXECUTOR_SOURCE_INCLUDE="\"$shim\"" \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_read_transaction_test.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" "$semantics_source" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" -o "$binary"
}

for optimization in 0 2; do
	binary="$tmp/read-O$optimization"
	compile "$optimization" "$root/src/lib/payload_mm_authvar_executor.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$binary"
	for case in get get-small next next-small query query-unknown not-found \
		invalid runtime dirty-tail next-dirty-tail begin-failure read-failure \
		end-failure end-not-found end-bts publication-order \
		publication-gate \
		reentry provider-reentry provider-callback lifecycle freshness \
		recovery-abort recovery-replay recovery-complete recovery-restore \
		alias alias-matrix unsupported; do
		ASAN_OPTIONS=detect_leaks=1 "$binary" "$case"
	done
done

mutant()
{
	name="$1"
	expression="$2"
	test_case="$3"
	component="${4:-executor}"
	if [ "$component" = executor ]; then
		executor_source="$tmp/$name.c"
		semantics_source="$root/src/lib/payload_mm_authvar_store_semantics.c"
		sed "$expression" "$root/src/lib/payload_mm_authvar_executor.c" > "$executor_source"
		cmp -s "$executor_source" "$root/src/lib/payload_mm_authvar_executor.c" && exit 1
	else
		executor_source="$root/src/lib/payload_mm_authvar_executor.c"
		semantics_source="$tmp/$name.c"
		sed "$expression" "$root/src/lib/payload_mm_authvar_store_semantics.c" > "$semantics_source"
		cmp -s "$semantics_source" "$root/src/lib/payload_mm_authvar_store_semantics.c" && exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/$name-O$optimization"
		compile "$optimization" "$executor_source" "$semantics_source" "$binary"
		if ASAN_OPTIONS=detect_leaks=1 "$binary" "$test_case" \
			> "$binary.log" 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutant overlap-gate \
	'/^static bool read_request_disjoint/,/^}/ s/if (sizes\[j\] \&\& payload_mm_authvar_buffers_overlap/if (false \&\& sizes[j] \&\& payload_mm_authvar_buffers_overlap/' \
	alias-matrix
mutant output-staging \
	'/^uint64_t payload_mm_authvar_read_transaction/,/^uint64_t payload_mm_authvar_policy_transaction/ s/memcpy(arena_at(executor.sealed.data_offset), bytes,/memcpy(copied.result_data, bytes,/' \
	publication-order
mutant skip-recovery \
	'/^uint64_t payload_mm_authvar_read_transaction/,/^uint64_t payload_mm_authvar_policy_transaction/ s/status = recover_session(state);/status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;/' \
	recovery-abort
mutant runtime-visibility \
	'/^uint64_t payload_mm_authvar_read_transaction/,/^uint64_t payload_mm_authvar_policy_transaction/ s/state->at_runtime, \&get/false, \&get/' \
	runtime
mutant arena-scrub \
	'/^uint64_t payload_mm_authvar_read_transaction/,/^uint64_t payload_mm_authvar_policy_transaction/ s/memset(executor.sealed.arena, 0, executor.sealed.required_size);/memset(state, 0, sizeof(*state));/' \
	get
mutant dirty-query \
	's/if (index->dirty_tail_offset)/if (false \&\& index->dirty_tail_offset)/' \
	dirty-tail semantics
mutant early-gate-release \
	'/^uint64_t payload_mm_authvar_read_transaction/,/^uint64_t payload_mm_authvar_policy_transaction/ { /__atomic_store_n(\&executor.busy, 0, __ATOMIC_RELEASE);/d; s/memset(executor.sealed.arena, 0, executor.sealed.required_size);/memset(executor.sealed.arena, 0, executor.sealed.required_size); __atomic_store_n(\&executor.busy, 0, __ATOMIC_RELEASE); sched_yield();/; }' \
	publication-gate

printf '%s\n' 'Payload-MM authenticated-variable read transaction tests: PASS'
