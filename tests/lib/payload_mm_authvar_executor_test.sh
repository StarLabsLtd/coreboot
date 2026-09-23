#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"

for optimization in 0 2; do
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_executor_test.c" \
		"$root/src/lib/payload_mm_authvar_executor.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" -o "$tmp/test-O$optimization"
	for case in misaligned-arena second-install invalid-geometry null-arena zero-arena null-limits \
		undersize-arena record-limits geometry-limits overflow-layout \
		direct-add marker-expected-state direct-replace direct-delete end-failure \
		empty-append-existing empty-append-absent reclaim queue-header-mutation \
		queue-record-mutation primary-body-mutation spare-body-mutation \
		spare-suffix-mutation \
		seal-mutation clean \
		record-state-mutation body-readback-mutation \
		final-compare-mutation final-fresh-read-mutation \
		final-ftw-action-mutation \
		initialize workspace-spare-body-mutation \
		workspace-spare-suffix-mutation workspace-working-body-mutation \
		dirty-tail workspace-invalidate-order discard-spare \
		abort-spare-readback replay-spare-readback replay-primary-readback \
		restore-queue-order cleanup-spare-readback single-flight-reentry \
		recovery-nonprogress recovery-limit abort \
		abort-partial replay complete \
		restore malformed read-control-mutation fault; do
		ASAN_OPTIONS=detect_leaks=1 "$tmp/test-O$optimization" "$case"
	done
done

cc -std=gnu11 -O2 -Wall -Wextra -Werror -Wconversion -Wshadow \
	-Wstrict-prototypes -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-builtin -D__TEST__ -D__COREBOOT__ -D__SMM__ \
	-DEXECUTOR_REAL_MEDIA \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" \
	"$root/tests/lib/payload_mm_authvar_executor_test.c" \
	"$root/src/lib/payload_mm_authvar.c" \
	"$root/src/lib/payload_mm_authvar_runtime.c" \
	"$root/src/lib/payload_mm_authvar_media.c" \
	"$root/src/lib/payload_mm_authvar_executor.c" \
	"$root/src/lib/payload_mm_authvar_ftw.c" \
	"$root/src/lib/payload_mm_authvar_store.c" \
	"$root/src/lib/payload_mm_authvar_store_semantics.c" \
	"$root/src/lib/payload_mm_authvar_record.c" \
	"$root/src/lib/payload_mm_authvar_writer.c" -o "$tmp/real-media"
for case in clean initialize; do
	ASAN_OPTIONS=detect_leaks=1 "$tmp/real-media" "$case"
done
reset_count="$(ASAN_OPTIONS=detect_leaks=1 "$tmp/real-media" count-initialize)"
cut=1
while [ "$cut" -le "$reset_count" ]; do
	ASAN_OPTIONS=detect_leaks=1 "$tmp/real-media" "reset-initialize-$cut"
	cut=$((cut + 1))
done

mutation_test()
{
	name="$1"
	expression="$2"
	test_case="$3"
	mutant="$tmp/payload_mm_authvar_executor-$name.c"

	sed "$expression" "$root/src/lib/payload_mm_authvar_executor.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_executor.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/mutant-$name-O$optimization"
		log="$tmp/mutant-$name-O$optimization.log"
		if ! cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
			-Wconversion -Wshadow -Wstrict-prototypes \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-fno-builtin -D__TEST__ -D__COREBOOT__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$tmp/include" -I"$root/src" -I"$root/src/lib" \
			-I"$root/src/include" -I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_executor_test.c" "$mutant" \
			"$root/src/lib/payload_mm_authvar_ftw.c" \
			"$root/src/lib/payload_mm_authvar_store.c" \
			"$root/src/lib/payload_mm_authvar_store_semantics.c" \
			"$root/src/lib/payload_mm_authvar_record.c" \
			"$root/src/lib/payload_mm_authvar_writer.c" -o "$binary" \
			>"$log" 2>&1; then
			echo "ERROR: $name O$optimization mutant did not compile" >&2
			cat "$log" >&2
			exit 1
		fi
		if ASAN_OPTIONS=detect_leaks=1 "$binary" "$test_case" \
			>/dev/null 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutation_test record-state \
	's/if (record\[2\] != PAYLOAD_MM_AUTHVAR_STATE_ERASED)/if (false \&\& record[2] != PAYLOAD_MM_AUTHVAR_STATE_ERASED)/' \
	record-state-mutation
mutation_test snapshot-invariant-poison \
	's/return state->invariant_failure ? poison_session() :/return false ? poison_session() :/g' \
	read-control-mutation
mutation_test record-capacity \
	's/return size <= limits->maximum_record_size \&\&/return (true || size <= limits->maximum_record_size) \&\&/' \
	record-limits
mutation_test contract-ftw-geometry \
	's/payload_mm_authvar_ftw_geometry(\&geometry, contract->store_size,/payload_mm_authvar_ftw_geometry(\&geometry, limits->maximum_store_size,/' \
	invalid-geometry
mutation_test recovery-nonprogress \
	's/if (state->have_previous_ftw \&\&/if (false \&\& state->have_previous_ftw \&\&/' \
	recovery-nonprogress
mutation_test recovery-limit-bound \
	's/state->recovery_count < EXECUTOR_RECOVERY_LIMIT;/state->recovery_count <= EXECUTOR_RECOVERY_LIMIT;/' \
	recovery-limit
mutation_test single-flight \
	'/^uint64_t payload_mm_authvar_executor_recover/,/^uint64_t payload_mm_authvar_policy_transaction/ s/if (!__atomic_compare_exchange_n(\&executor.busy/if (false \&\& !__atomic_compare_exchange_n(\&executor.busy/' \
	single-flight-reentry
mutation_test record-body-marker \
	's/result = program_body(state, media_offset, record, step->size, 2U);/result = checked_program(state, media_offset, record, step->size);/' \
	direct-add
mutation_test body-readback \
	's/result = verify_media(state, offset, bytes, marker_offset);/result = PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;/' \
	body-readback-mutation
mutation_test direct-marker-order \
	'/result = program_body(state, media_offset/,/memcpy(snapshot()/ s/if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/if (false \&\& result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/' \
	body-readback-mutation
mutation_test end-cache-invalidate \
	's/payload_mm_authvar_media_cache_invalidate();/(void)0;/g' \
	end-failure
mutation_test full-spare-suffix \
	's/geometry->spare_size > geometry->variable_size/false/g' \
	spare-suffix-mutation
mutation_test workspace-spare-suffix \
	's/geometry->spare_size > geometry->working_size/false/' \
	workspace-spare-suffix-mutation
mutation_test workspace-spare-marker-order \
	'/geometry->spare_size - geometry->working_size);/,/geometry->spare_offset + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET/ s/if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/if (false \&\& result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/' \
	workspace-spare-body-mutation
mutation_test workspace-working-marker-order \
	'/result = program_body(state, geometry->working_offset, image/,/geometry->working_offset + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET/ s/if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/if (false \&\& result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/' \
	workspace-working-body-mutation
mutation_test reclaim-body-readback \
	'/result = verify_media(state, geometry->spare_offset, image/,/geometry->spare_size > geometry->variable_size/ s/geometry->variable_size);/0U);/' \
	spare-body-mutation
mutation_test queue-header-readback \
	'/result = verify_media(state, queue + 1U, header + 1U/,/result = advance_marker(state, queue/ s/PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE - 1U);/0U);/' \
	queue-header-mutation
mutation_test header-fe-stage \
	's/PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED);/PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);/g' \
	reclaim
mutation_test queue-record-readback \
	'/result = verify_media(state, queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE + 1U/,/result = erase_span(state, geometry->spare_offset/ s/PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE - 1U);/0U);/' \
	queue-record-mutation
mutation_test reclaim-marker-order \
	'/geometry->spare_size - geometry->variable_size);/,/PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE/ s/if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/if (true)/' \
	spare-suffix-mutation
mutation_test primary-readback-before-f9 \
	'/result = verify_media(state, geometry->variable_offset, image/,/result = advance_marker(state, queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE/ s/geometry->variable_size);/0U);/' \
	primary-body-mutation
mutation_test record-f9-stage \
	's/PAYLOAD_MM_AUTHVAR_FTW_RECORD_DESTINATION_COMPLETE);/PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE);/g' \
	reclaim
mutation_test final-full-compare \
	'/result = verify_media(state, 0/,/result = snapshot_read(state);/ s/result = verify_media(state, 0, snapshot(), state->contract.store_size);/result = PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;/' \
	final-compare-mutation
mutation_test bind-before-final-proof \
	'0,/result = verify_media(state, 0, snapshot(), state->contract.store_size);/ s//payload_mm_authvar_media_cache_bind(state->generation, state->token); result = verify_media(state, 0, snapshot(), state->contract.store_size);/' \
	final-compare-mutation
mutation_test final-fresh-snapshot \
	'/result = verify_media(state, 0/,/result = snapshot_read(state);/ s/result = snapshot_read(state);/result = PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;/' \
	final-fresh-read-mutation
mutation_test final-ftw-clean \
	'/result = verify_media(state, 0/,/state->ftw.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN/ s/state->ftw.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN/false/' \
	final-ftw-action-mutation
mutation_test final-logical-proof \
	'/if (!final_data/,/goto end;/ { s/status = poison_session();/status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;/; s/goto end;//; }' \
	final-fresh-read-mutation
mutation_test mandatory-end \
	's/end_result = media_end(state);/end_result = false ? media_end(state) : PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;/g' \
	direct-add
mutation_test end-failure-status \
	'/if (end_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/,/^out:/ s/if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)/if (false \&\& status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)/' \
	end-failure

printf '%s\n' 'Payload-MM authenticated-variable executor recovery tests: PASS'
