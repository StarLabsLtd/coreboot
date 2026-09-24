#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"
mkdir -p "$tmp/candidate/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_RECOVERY 1' \
	> "$tmp/candidate/include/config.h"

for optimization in 0 2; do
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-D__SMM__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_executor_acceptance_test.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_authvar_media.c" \
		"$root/src/lib/payload_mm_authvar_executor.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" \
		-Wl,--wrap=payload_mm_authvar_media_fail_closed \
		-Wl,--wrap=payload_mm_authvar_media_cache_invalidate \
		-Wl,--wrap=payload_mm_authvar_media_cache_bind \
		-o "$tmp/acceptance-O$optimization"
	if [ "${ACCEPTANCE_DEFAULT_RECOVERY_ONLY:-0}" = 1 ]; then
		:
	elif [ "${ACCEPTANCE_FAULT_ONLY:-0}" = 1 ]; then
		ASAN_OPTIONS=detect_leaks=1 "$tmp/acceptance-O$optimization" fault-only
	else
		ASAN_OPTIONS=detect_leaks=1 "$tmp/acceptance-O$optimization"
	fi
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-D__SMM__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/candidate/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_executor_acceptance_test.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_authvar_media.c" \
		"$root/src/lib/payload_mm_authvar_executor.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" \
		"$root/src/lib/payload_mm_authvar_default_store.c" \
		"$root/src/lib/payload_mm_authvar_candidate.c" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
		"$root/src/lib/payload_mm_authvar_view.c" \
		"$root/src/lib/payload_mm_authvar_mode.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		-Wl,--wrap=payload_mm_authvar_media_fail_closed \
		-Wl,--wrap=payload_mm_authvar_media_cache_invalidate \
		-Wl,--wrap=payload_mm_authvar_media_cache_bind \
		-Wl,--wrap=payload_mm_authvar_default_store_compose \
		-o "$tmp/acceptance-candidate-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 \
		"$tmp/acceptance-candidate-O$optimization" default-recovery
	if [ "${ACCEPTANCE_DEFAULT_RECOVERY_ONLY:-0}" = 1 ]; then
		continue
	fi
	if [ "${ACCEPTANCE_CANDIDATE_CHECKPOINT_ONLY:-0}" = 1 ]; then
		:
	else
		ASAN_OPTIONS=detect_leaks=1 \
			"$tmp/acceptance-candidate-O$optimization" golden-only
		ASAN_OPTIONS=detect_leaks=1 \
			"$tmp/acceptance-candidate-O$optimization" candidate-only
		ASAN_OPTIONS=detect_leaks=1 \
			"$tmp/acceptance-candidate-O$optimization" candidate-mutations
	fi
done

if [ "${ACCEPTANCE_DEFAULT_RECOVERY_ONLY:-0}" = 1 ]; then
	printf '%s\n' \
		'Payload-MM authenticated-variable default recovery acceptance: PASS'
	exit 0
fi

instrumented="$tmp/executor-checkpoint-base.c"
sed -e '/#include "payload_mm_authvar_internal.h"/a\
extern void payload_mm_authvar_candidate_checkpoint_test_hook(uint8_t *image, unsigned int phase, bool before);' \
	-e 's/if (candidate_commit && current == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/if (candidate_commit \&\& current == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {\
\t\tpayload_mm_authvar_candidate_checkpoint_test_hook((uint8_t *)image, (unsigned int)phase, true);/' \
	-e 's/return candidate_checkpoint(state, image, phase, source_must_match);/current = candidate_checkpoint(state, image, phase, source_must_match);\
\t\tpayload_mm_authvar_candidate_checkpoint_test_hook((uint8_t *)image, (unsigned int)phase, false);\
\t\treturn current;\
\t}/' \
	"$root/src/lib/payload_mm_authvar_executor.c" > "$instrumented"

for phase in BASE SPARE_COMPLETE PRIMARY_ERASE PRIMARY_IMAGE \
	DESTINATION_COMPLETE JOURNAL_COMPLETE SPARE_CLEAN; do
	mutant="$tmp/executor-checkpoint-$phase.c"
	if [ "$phase" = BASE ]; then
		mutant="$instrumented"
	else
		sed "s/current = candidate_checkpoint(state, image, phase, source_must_match);/current = phase == CANDIDATE_PHASE_$phase ? current : candidate_checkpoint(state, image, phase, source_must_match);/" \
			"$instrumented" > "$mutant"
		if cmp -s "$mutant" "$instrumented"; then
			echo "ERROR: $phase checkpoint mutant changed nothing" >&2
			exit 1
		fi
	fi
	for optimization in 0 2; do
		binary="$tmp/checkpoint-$phase-O$optimization"
		cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
			-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
			-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
			-D__SMM__ -include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$tmp/candidate/include" -I"$root/src" -I"$root/src/lib" \
			-I"$root/src/include" \
			-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_executor_acceptance_test.c" \
			"$root/src/lib/payload_mm_authvar.c" \
			"$root/src/lib/payload_mm_authvar_runtime.c" \
			"$root/src/lib/payload_mm_authvar_media.c" "$mutant" \
			"$root/src/lib/payload_mm_authvar_fv.c" \
			"$root/src/lib/payload_mm_authvar_ftw.c" \
			"$root/src/lib/payload_mm_authvar_store.c" \
			"$root/src/lib/payload_mm_authvar_store_semantics.c" \
			"$root/src/lib/payload_mm_authvar_record.c" \
			"$root/src/lib/payload_mm_authvar_writer.c" \
			"$root/src/lib/payload_mm_authvar_default_store.c" \
			"$root/src/lib/payload_mm_authvar_candidate.c" \
			"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
			"$root/src/lib/payload_mm_authvar_view.c" \
			"$root/src/lib/payload_mm_authvar_mode.c" \
			"$root/src/lib/payload_mm_authvar_format.c" \
			-Wl,--wrap=payload_mm_authvar_media_fail_closed \
			-Wl,--wrap=payload_mm_authvar_media_cache_invalidate \
			-Wl,--wrap=payload_mm_authvar_media_cache_bind \
			-Wl,--wrap=payload_mm_authvar_default_store_compose -o "$binary"
		if [ "$phase" = BASE ]; then
			ASAN_OPTIONS=detect_leaks=1 "$binary" candidate-checkpoints
		elif ASAN_OPTIONS=detect_leaks=1 "$binary" candidate-checkpoints \
			>/dev/null 2>&1; then
			echo "ERROR: $phase checkpoint mutant survived O$optimization" >&2
			exit 1
		fi
	done
done

printf '%s\n' 'Payload-MM authenticated-variable executor integrated acceptance: PASS'
