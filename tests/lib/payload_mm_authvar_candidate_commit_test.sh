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
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" -o "$binary"
	for test_case in success binding header source index output; do
		ASAN_OPTIONS=detect_leaks=1 "$binary" "candidate-$test_case"
	done
	for reject in generation token runtime binding-reserved policy source-used \
		candidate-used count modes mode-setup mode-secure mode-vendor \
		result-reserved source-digest \
		candidate-digest candidate-body primary-media working-media queue-media \
		spare-media; do
		ASAN_OPTIONS=detect_leaks=1 "$binary" "candidate-reject-$reject"
	done
	callback_count="$(ASAN_OPTIONS=detect_leaks=1 "$binary" candidate-count)"
	callback=1
	while [ "$callback" -le "$callback_count" ]; do
		ASAN_OPTIONS=detect_leaks=1 "$binary" "candidate-fault-$callback"
		callback=$((callback + 1))
	done
	ASAN_OPTIONS=detect_leaks=1 "$binary" candidate-image-callback
	for result in unsupported write-protected; do
		for operation in read program erase; do
			ASAN_OPTIONS=detect_leaks=1 "$binary" \
				"candidate-$result-$operation"
		done
	done
done

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
			"$root/src/lib/payload_mm_authvar_format.c" \
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

mutation_test exact-binding \
	's/memcmp(\&result->binding, \&state->candidate_binding,/memcmp(\&result->binding, \&result->binding,/' \
	candidate-binding
mutation_test exact-header-size \
	's/candidate_index.store_size != state->ftw.variable_store_size/false/' \
	candidate-header
mutation_test volatile-projection \
	's/!payload_mm_authvar_candidate_projection_valid(\&source_index,/payload_mm_authvar_candidate_projection_valid(\&source_index,/' \
	candidate-reject-mode-secure

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
	"$root/src/lib/payload_mm_authvar_format.c" \
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
