#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 1' \
	> "$tmp/include/config.h"

for optimization in 0 2; do
	binary="$tmp/test-O$optimization"
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_executor_test.c" \
		"$root/src/lib/payload_mm_authvar_executor.c" \
		"$root/src/lib/payload_mm_authvar_candidate.c" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_view.c" \
		"$root/src/lib/payload_mm_authvar_mode.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" -o "$binary"
	for test_case in success binding header source index output; do
		ASAN_OPTIONS=detect_leaks=1 "$binary" "candidate-$test_case"
	done
	ASAN_OPTIONS=detect_leaks=1 "$binary" candidate-uninstalled
	ASAN_OPTIONS=detect_leaks=1 "$binary" candidate-corrupt-policy
	for reject in generation token runtime binding-reserved policy source-used \
		candidate-used count modes mode-setup mode-secure mode-vendor \
		result-reserved source-digest \
		candidate-digest candidate-body primary-media working-media queue-media \
		spare-media; do
		ASAN_OPTIONS=detect_leaks=1 "$binary" "candidate-reject-$reject"
	done
	result_byte=0
	while [ "$result_byte" -lt 120 ]; do
		bit=0
		while [ "$bit" -lt 8 ]; do
			ASAN_OPTIONS=detect_leaks=1 "$binary" \
				"candidate-result-bit-$result_byte-$bit"
			bit=$((bit + 1))
		done
		result_byte=$((result_byte + 1))
	done
	image_byte=0
	while [ "$image_byte" -lt 4096 ]; do
		ASAN_OPTIONS=detect_leaks=1 "$binary" \
			"candidate-image-byte-$image_byte"
		image_byte=$((image_byte + 1))
	done
	callback_count="$(ASAN_OPTIONS=detect_leaks=1 "$binary" candidate-count)"
	for result in device unsupported write-protected; do
		callback=1
		while [ "$callback" -le "$callback_count" ]; do
			ASAN_OPTIONS=detect_leaks=1 "$binary" \
				"candidate-callback-$result-$callback"
			callback=$((callback + 1))
		done
	done
done

mutation_test_file()
{
	name="$1"
	mutant="$2"
	test_case="$3"
	for optimization in 0 2; do
		binary="$tmp/mutant-$name-O$optimization"
		cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
			-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
			-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$tmp/include" -I"$root/src" -I"$root/src/lib" \
			-I"$root/src/include" -I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_executor_test.c" "$mutant" \
			"$root/src/lib/payload_mm_authvar_candidate.c" \
			"$root/src/lib/payload_mm_authvar_bundle.c" \
			"$root/src/lib/payload_mm_authvar_view.c" \
			"$root/src/lib/payload_mm_authvar_mode.c" \
			"$root/src/lib/payload_mm_authvar_format.c" \
			"$root/src/lib/payload_mm_authvar_fv.c" \
			"$root/src/lib/payload_mm_authvar_ftw.c" \
			"$root/src/lib/payload_mm_authvar_store.c" \
			"$root/src/lib/payload_mm_authvar_store_semantics.c" \
			"$root/src/lib/payload_mm_authvar_record.c" \
			"$root/src/lib/payload_mm_authvar_writer.c" -o "$binary"
		if ASAN_OPTIONS=detect_leaks=1 "$binary" "$test_case" \
			>/dev/null 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutation_test()
{
	name="$1"
	expression="$2"
	test_case="$3"
	mutant="$tmp/executor-$name.c"

	sed "$expression" "$root/src/lib/payload_mm_authvar_executor.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_executor.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	mutation_test_file "$name" "$mutant" "$test_case"
}

mutation_test_two()
{
	name="$1"
	first="$2"
	second="$3"
	test_case="$4"
	mutant="$tmp/executor-$name.c"

	sed -e "$first" -e "$second" \
		"$root/src/lib/payload_mm_authvar_executor.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_executor.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	mutation_test_file "$name" "$mutant" "$test_case"
}

mutation_test_three()
{
	name="$1"
	first="$2"
	second="$3"
	third="$4"
	test_case="$5"
	mutant="$tmp/executor-$name.c"

	sed -e "$first" -e "$second" -e "$third" \
		"$root/src/lib/payload_mm_authvar_executor.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_executor.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	mutation_test_file "$name" "$mutant" "$test_case"
}

mutation_test_four()
{
	name="$1"
	first="$2"
	second="$3"
	third="$4"
	fourth="$5"
	test_case="$6"
	mutant="$tmp/executor-$name.c"

	sed -e "$first" -e "$second" -e "$third" -e "$fourth" \
		"$root/src/lib/payload_mm_authvar_executor.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_executor.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	mutation_test_file "$name" "$mutant" "$test_case"
}

mutation_test exact-binding \
	's/memcmp(\&result->binding, \&state->candidate_binding,/memcmp(\&result->binding, \&result->binding,/' \
	candidate-binding
mutation_test exact-header-size \
	's/candidate_index.store_size != state->ftw.variable_store_size/false/' \
	candidate-header
mutation_test volatile-projection \
	's/!payload_mm_authvar_candidate_projection_valid(\&source_index,/payload_mm_authvar_candidate_projection_valid(\&source_index,/' \
	candidate-reject-mode-secure
mutation_test digest-width \
	's/!memcmp(digest, expected, sizeof(digest))/!memcmp(digest, expected, 1U)/' \
	candidate-result-bit-57-0
mutation_test policy-binding \
	's/memcmp(\&result->policy, \&state->policy,/memcmp(\&result->policy, \&result->policy,/' \
	candidate-result-bit-24-0
mutation_test result-reserved-width \
	's/sizeof(result->reserved)/1U/' \
	candidate-result-bit-54-0
mutation_test_two binding-reserved-width \
	's/memcmp(\&result->binding, \&state->candidate_binding,/memcmp(\&result->binding, \&result->binding,/' \
	's/sizeof(result->binding.reserved)/1U/' \
	candidate-result-bit-19-0
mutation_test phase-checkpoint \
	's/if (candidate_commit \&\& current ==/if (false \&\& candidate_commit \&\& current ==/' \
	candidate-image-byte-1
mutation_test full-image-digest \
	's/return matches;/return matches || expected == session()->candidate_image_digest;/' \
	candidate-image-byte-1
mutation_test source-used \
	's/result->source_used_size != state->index.used_size/false/' \
	candidate-reject-source-used
mutation_test_two candidate-used \
	's/result->candidate_used_size < PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE/false/' \
	's/candidate_index.used_size != result->candidate_used_size/false/' \
	candidate-reject-candidate-used
mutation_test_four candidate-count \
	's/!result->candidate_record_count/false/' \
	's/result->candidate_record_count > state->policy.maximum_records/false/' \
	's/candidate_index.record_count != result->candidate_record_count/false/' \
	's/candidate_index.entry_count != result->candidate_record_count/false/' \
	candidate-reject-count
mutation_test_two owner-generation \
	's/memcmp(\&result->binding, \&state->candidate_binding,/memcmp(\&result->binding, \&result->binding,/' \
	's/result->binding.generation != state->generation/false/' \
	candidate-reject-generation
mutation_test_two owner-token \
	's/memcmp(\&result->binding, \&state->candidate_binding,/memcmp(\&result->binding, \&result->binding,/' \
	's/result->binding.token != state->token/false/' \
	candidate-reject-token
mutation_test source-digest \
	's/return matches;/return matches || expected == session()->candidate_result.source_digest;/' \
	candidate-reject-source-digest
mutation_test candidate-digest \
	's/return matches;/return matches || expected == session()->candidate_result.candidate_digest;/' \
	candidate-reject-candidate-digest
mutation_test_three dirty-tail \
	's/candidate_index.entry_count != result->candidate_record_count/false/' \
	's/candidate_index.dirty_tail_offset/false/' \
	's/state->index.entry_count !=/state->index.entry_count >/' \
	candidate-dirty-tail
mutation_test source-match-checkpoint \
	's/if (!source_must_match)/if ((void)source_must_match, true)/' \
	candidate-source-callback
mutation_test source-index-equality \
	's/!exact_index_equal(\&source_index, \&candidate_index)/exact_index_equal(\&source_index, \&candidate_index)/' \
	candidate
mutation_test fresh-ftw-plan \
	's/fresh_ftw.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN/fresh_ftw.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN/' \
	candidate
mutation_test result-pointer \
	's/\&state->candidate_result);/\&prepared);/' \
	candidate
mutation_test candidate-buffer-pointer \
	's/commit_candidate_image(state, candidate_store,/commit_candidate_image(state, candidate_store + 1U,/' \
	candidate
mutation_test candidate-extent \
	's/state->candidate_size = state->ftw.variable_store_size;/state->candidate_size = state->ftw.variable_store_size - 1U;/' \
	candidate

real="$tmp/real-media"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -Wconversion -Wshadow \
	-Wstrict-prototypes -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-builtin -D__TEST__ -D__COREBOOT__ -D__SMM__ -DEXECUTOR_REAL_MEDIA \
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
	"$root/src/lib/payload_mm_authvar_candidate.c" \
	"$root/src/lib/payload_mm_authvar_bundle.c" \
	"$root/src/lib/payload_mm_authvar_view.c" \
	"$root/src/lib/payload_mm_authvar_mode.c" \
	"$root/src/lib/payload_mm_authvar_format.c" \
	"$root/src/lib/payload_mm_authvar_fv.c" \
	"$root/src/lib/payload_mm_authvar_ftw.c" \
	"$root/src/lib/payload_mm_authvar_store.c" \
	"$root/src/lib/payload_mm_authvar_store_semantics.c" \
	"$root/src/lib/payload_mm_authvar_record.c" \
	"$root/src/lib/payload_mm_authvar_writer.c" -o "$real"
reset_count="$(ASAN_OPTIONS=detect_leaks=1 "$real" candidate-count)"
cut=1
while [ "$cut" -le "$reset_count" ]; do
	ASAN_OPTIONS=detect_leaks=1 "$real" "reset-candidate-$cut"
	recovery_count="$(ASAN_OPTIONS=detect_leaks=1 "$real" \
		"candidate-recovery-count-$cut")"
	recovery_cut=1
	while [ "$recovery_cut" -le "$recovery_count" ]; do
		ASAN_OPTIONS=detect_leaks=1 "$real" \
			"reset-candidate-$cut-$recovery_cut"
		recovery_cut=$((recovery_cut + 1))
	done
	cut=$((cut + 1))
done

printf '%s\n' 'Payload-MM authenticated-variable candidate commit tests: PASS'
