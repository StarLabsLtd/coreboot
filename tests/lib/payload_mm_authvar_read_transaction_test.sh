#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"
mkdir -p "$tmp/view/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_REQUIRE_SELF_SIGNED_PK 0' \
	> "$tmp/view/include/config.h"

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
		"$root/src/lib/payload_mm_authvar_fv.c" \
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

view_compile()
{
	optimization="$1"
	executor_source="$2"
	binary="$3"
	shim="$binary-source.inc"
	sed '$d' "$root/tests/lib/payload_mm_authvar_read_transaction_source.inc" > "$shim"
	printf '#include "%s"\n' "$executor_source" >> "$shim"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes -ffunction-sections \
		-fdata-sections -Wl,--gc-sections \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-pthread -D__TEST__ -D__COREBOOT__ \
		-DEXECUTOR_SOURCE_INCLUDE="\"$shim\"" \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/view/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_read_transaction_test.c" \
		"$root/src/lib/payload_mm_authvar_coordinator.c" \
		"$root/src/lib/payload_mm_authvar_set_preflight.c" \
		"$root/src/lib/payload_mm_authvar_controlled_mode.c" \
		"$root/src/lib/payload_mm_authvar_view.c" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_authvar_mode.c" \
		"$root/src/lib/payload_mm_authvar_route.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" \
		-Wl,--wrap=payload_mm_authvar_view_get \
		-Wl,--wrap=payload_mm_authvar_view_query -o "$binary"
}

for optimization in 0 2; do
	binary="$tmp/read-view-O$optimization"
	view_compile "$optimization" \
		"$root/src/lib/payload_mm_authvar_executor.c" "$binary"
	for case in view-get-all view-next-all view-small \
		view-reconcile-statuses view-reconcile-end-failure view-boot-drift \
		view-runtime-unsealed view-runtime-reconcile \
		view-first-end-failure view-next-end-failure \
		view-collision-get view-collision-next view-collision-query \
		view-get-malformed view-get-device view-query-out-of-range; do
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

view_mutant()
{
	name="$1"
	expression="$2"
	test_case="$3"
	executor_source="$tmp/$name.c"

	sed "$expression" "$root/src/lib/payload_mm_authvar_executor.c" \
		> "$executor_source"
	cmp -s "$executor_source" "$root/src/lib/payload_mm_authvar_executor.c" &&
		exit 1
	for optimization in 0 2; do
		binary="$tmp/$name-O$optimization"
		view_compile "$optimization" "$executor_source" "$binary"
		if ASAN_OPTIONS=detect_leaks=1 "$binary" "$test_case" \
			> "$binary.log" 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

view_mutant view-source-instead-of-reconcile \
	'/^uint64_t payload_mm_authvar_read_transaction/,/^complete_without_arena:/ s/reconcile_modes = executor.sealed_modes_need_reconcile;/reconcile_modes = false;/' \
	view-runtime-reconcile
view_mutant view-ignore-sealed-source \
	'/^uint64_t payload_mm_authvar_read_transaction/,/^complete_without_arena:/ s/state->at_runtime, executor.sealed_volatile_modes_valid,/state->at_runtime, false,/' \
	view-boot-drift
view_mutant view-skip-initial-seal \
	'/^uint64_t payload_mm_authvar_read_transaction/,/^complete_without_arena:/ s/executor.sealed_volatile_modes_valid = true;/executor.sealed_volatile_modes_valid = false;/' \
	view-first-end-failure
view_mutant view-clear-reconcile-before-end \
	'/^uint64_t payload_mm_authvar_read_transaction/,/^complete_without_arena:/ s/end_result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS/true/' \
	view-reconcile-end-failure
view_mutant view-never-clear-reconcile \
	'/^uint64_t payload_mm_authvar_read_transaction/,/^complete_without_arena:/ s/executor.sealed_modes_need_reconcile = false;/executor.sealed_modes_need_reconcile = true;/' \
	view-reconcile-statuses
view_mutant view-publish-next-before-end \
	'/^uint64_t payload_mm_authvar_read_transaction/,/^complete_without_arena:/ s/memcpy(arena_at(executor.sealed.name_offset), bytes,/memcpy(copied.result_name, bytes,/' \
	view-next-end-failure
view_mutant view-drop-get-invalid \
	'/^uint64_t payload_mm_authvar_read_transaction/,/^complete_without_arena:/ s/status != PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER &&/true \&\&/' \
	view-get-malformed
view_mutant view-drop-get-device \
	'/^uint64_t payload_mm_authvar_read_transaction/,/^complete_without_arena:/ s/status != PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR)/true)/' \
	view-get-device
view_mutant view-bypass-init \
	'/payload_mm_authvar_view_init(\&read_view/,/state->at_runtime) != CB_SUCCESS)/c\
\t    (read_view = (struct payload_mm_authvar_view) { .persistent = \&state->index, .volatile_modes = source_modes, .at_runtime = state->at_runtime }, false)) {' \
	view-collision-query
view_mutant view-accept-query-out-of-range \
	'/payload_mm_authvar_view_query(\&read_view/,/^[[:space:]]*break;/ s/status != PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER/false \&\& status != PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER/' \
	view-query-out-of-range

printf '%s\n' 'Payload-MM authenticated-variable read transaction tests: PASS'
