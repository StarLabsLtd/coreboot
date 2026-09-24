#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-authvar-coordinator.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_AUTHORITY_PROVIDER 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_REQUIRE_SELF_SIGNED_PK 0' \
	> "$temporary/include/config.h"

for optimization in 0 2; do
	output="$temporary/test-O$optimization"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_executor_test.c" \
		"$root/src/lib/payload_mm_authvar_executor.c" \
		"$root/src/lib/payload_mm_authvar_coordinator.c" \
		"$root/src/lib/payload_mm_authvar_authority_provider.c" \
		"$root/src/lib/payload_mm_authvar_set_preflight.c" \
		"$root/src/lib/payload_mm_authvar_view.c" \
		"$root/src/lib/payload_mm_authvar_authority.c" \
		"$root/src/lib/payload_mm_authvar_candidate.c" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
		"$root/src/lib/payload_mm_authvar_mode.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_authvar_route.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" -o "$output"
	for case in \
		coordinator-success \
		coordinator-private-atomic \
		coordinator-private-provider \
		coordinator-preflight-consumers \
		coordinator-native-ordinary \
		coordinator-native-collision coordinator-native-invalid \
		coordinator-native-post-collision coordinator-native-reclaim \
		coordinator-native-copy \
		coordinator-status-invalid coordinator-status-busy \
		coordinator-status-no-memory coordinator-status-malformed \
		coordinator-status-rejected coordinator-status-unsupported \
		coordinator-status-internal coordinator-status-changed \
		coordinator-status-out-of-range coordinator-noop \
		coordinator-not-found coordinator-attack-owner \
		coordinator-attack-alternate coordinator-attack-owner-dirty \
		coordinator-attack-descriptor coordinator-attack-request \
		coordinator-attack-name coordinator-attack-data \
		coordinator-attack-context coordinator-attack-internal-context \
		coordinator-attack-result \
		coordinator-context-max coordinator-context-over \
		coordinator-runtime-pk-delete coordinator-runtime-missing-enable \
		coordinator-vendor-reconcile coordinator-native-reconcile \
		coordinator-end-failure \
		coordinator-runtime-end-failure coordinator-preexisting-crypto; do
		# Keep the admission matrix separate for a precise untouched-output oracle.
		ASAN_OPTIONS=detect_leaks=1 "$output" "$case"
	done
	ASAN_OPTIONS=detect_leaks=1 "$output" coordinator-admission
	ASAN_OPTIONS=detect_leaks=1 "$output" coordinator-policy-race
	ASAN_OPTIONS=detect_leaks=1 "$output" coordinator-output-alias
	for case in result-read result-program result-erase result-end \
		owner-program owner-read alternate-erase alternate-read context-read; do
		ASAN_OPTIONS=detect_leaks=1 "$output" "coordinator-media-$case"
	done
	ASAN_OPTIONS=detect_leaks=1 "$output" coordinator-callback-trace
	for case in result context owner alternate; do
		ASAN_OPTIONS=detect_leaks=1 "$output" "coordinator-begin-$case"
	done
	callback_count="$(ASAN_OPTIONS=detect_leaks=1 "$output" \
		coordinator-callback-count)"
	callback=1
	while [ "$callback" -le "$callback_count" ]; do
		for result in device unsupported write-protected invalid; do
			ASAN_OPTIONS=detect_leaks=1 "$output" \
				"coordinator-callback-$result-$callback"
		done
		callback=$((callback + 1))
	done
	for case in bundle-invalid bundle-internal bundle-changed \
		bundle-unsupported bundle-rejected outcome-none notfound-success \
		bundle-outcome-none bundle-outcome-range noop-notfound \
		mutation-notfound candidate-invalid candidate-security \
		candidate-no-memory; do
		ASAN_OPTIONS=detect_leaks=1 "$output" "coordinator-force-$case"
	done
	for case in abort replay complete restore; do
		ASAN_OPTIONS=detect_leaks=1 "$output" "coordinator-recovery-$case"
		recovery_count="$(ASAN_OPTIONS=detect_leaks=1 "$output" \
			"coordinator-recovery-count-$case")"
		recovery_callback=2
		while [ "$recovery_callback" -le "$recovery_count" ]; do
			ASAN_OPTIONS=detect_leaks=1 "$output" \
				"coordinator-recovery-fault-$case-$recovery_callback"
			recovery_callback=$((recovery_callback + 1))
		done
	done
done

compile_mutant()
{
	mutant_source="$1"
	mutant_output="$2"
	mutant_optimization="$3"
	${HOSTCC:-cc} -std=gnu11 -O"$mutant_optimization" -Wall -Wextra -Werror \
		-Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_executor_test.c" \
		"$mutant_source" "$root/src/lib/payload_mm_authvar_coordinator.c" \
		"$root/src/lib/payload_mm_authvar_authority_provider.c" \
		"$root/src/lib/payload_mm_authvar_set_preflight.c" \
		"$root/src/lib/payload_mm_authvar_view.c" \
		"$root/src/lib/payload_mm_authvar_authority.c" \
		"$root/src/lib/payload_mm_authvar_candidate.c" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
		"$root/src/lib/payload_mm_authvar_mode.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_authvar_route.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" -o "$mutant_output"
}

for optimization in 0 2; do
	for mutant in timestamp deletion external-source pre-view post-view \
		reconcile-clear; do
		mutant_source="$temporary/executor-$mutant-O$optimization.c"
		mutant_output="$temporary/mutant-$mutant-O$optimization"
		case "$mutant" in
		timestamp)
			sed '/state->source.data_size = state->request.data_size;/a\
\tstate->source.timestamp[0] = 1U;' \
				"$root/src/lib/payload_mm_authvar_executor.c" > "$mutant_source"
			mutant_case=coordinator-native-ordinary
			;;
		deletion)
			sed 's/deletion = set_plan.kind == PAYLOAD_MM_AUTHVAR_SET_ORDINARY_DELETE;/deletion = set_plan.kind != PAYLOAD_MM_AUTHVAR_SET_ORDINARY_DELETE;/' \
				"$root/src/lib/payload_mm_authvar_executor.c" > "$mutant_source"
			mutant_case=coordinator-native-ordinary
			;;
		external-source)
			sed '/memcpy(state->source.vendor_guid, state->request.vendor_guid,/,/state->source.data_size =/ s/state->request\./request->/g' \
				"$root/src/lib/payload_mm_authvar_executor.c" > "$mutant_source"
			mutant_case=coordinator-native-copy
			;;
		pre-view)
			awk 'BEGIN { in_policy = 0; changed = 0 } \
				/uint64_t payload_mm_authvar_policy_transaction/ { in_policy = 1 } \
				{ if (in_policy && !changed && \
				      /state->at_runtime\) != CB_SUCCESS/) { \
					sub(/!= CB_SUCCESS/, "== CB_SUCCESS"); changed = 1 } \
				  print }' \
				"$root/src/lib/payload_mm_authvar_executor.c" > "$mutant_source"
			mutant_case=coordinator-native-collision
			;;
		post-view)
			sed 's/if (payload_mm_authvar_view_init(&current_view/if (false \&\& payload_mm_authvar_view_init(\&current_view/' \
				"$root/src/lib/payload_mm_authvar_executor.c" > "$mutant_source"
			mutant_case=coordinator-native-post-collision
			;;
		reconcile-clear)
			sed '/uint64_t payload_mm_authvar_policy_transaction/,/^}/ s/if (reconcile_modes \&\& modes_validated/if (false \&\& reconcile_modes \&\& modes_validated/' \
				"$root/src/lib/payload_mm_authvar_executor.c" > "$mutant_source"
			mutant_case=coordinator-native-reconcile
			;;
		esac
		if cmp -s "$root/src/lib/payload_mm_authvar_executor.c" "$mutant_source"; then
			echo "ERROR: $mutant O$optimization mutation was not applied" >&2
			exit 1
		fi
		compile_mutant "$mutant_source" "$mutant_output" "$optimization"
		if ASAN_OPTIONS=detect_leaks=1 "$mutant_output" "$mutant_case" \
			>/dev/null 2>&1; then
			echo "ERROR: $mutant O$optimization mutant survived" >&2
			exit 1
		fi
	done
done

for real_optimization in 0 2; do
real_output="$temporary/test-real-O$real_optimization"
${HOSTCC:-cc} -std=gnu11 -O"$real_optimization" -Wall -Wextra -Werror -Wshadow \
	-Wstrict-prototypes -fsanitize=address,undefined \
	-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
	-D__SMM__ -DEXECUTOR_REAL_MEDIA \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
	-I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	"$root/tests/lib/payload_mm_authvar_executor_test.c" \
	"$root/src/lib/payload_mm_authvar.c" \
	"$root/src/lib/payload_mm_authvar_runtime.c" \
	"$root/src/lib/payload_mm_authvar_media.c" \
	"$root/src/lib/payload_mm_authvar_executor.c" \
	"$root/src/lib/payload_mm_authvar_coordinator.c" \
	"$root/src/lib/payload_mm_authvar_authority_provider.c" \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" \
	"$root/src/lib/payload_mm_authvar_view.c" \
	"$root/src/lib/payload_mm_authvar_authority.c" \
	"$root/src/lib/payload_mm_authvar_candidate.c" \
	"$root/src/lib/payload_mm_authvar_bundle.c" \
	"$root/src/lib/payload_mm_authvar_certdb.c" \
	"$root/src/lib/payload_mm_authvar_mode.c" \
	"$root/src/lib/payload_mm_authvar_format.c" \
	"$root/src/lib/payload_mm_authvar_route.c" \
	"$root/src/lib/payload_mm_authvar_fv.c" \
	"$root/src/lib/payload_mm_authvar_ftw.c" \
	"$root/src/lib/payload_mm_authvar_store.c" \
	"$root/src/lib/payload_mm_authvar_store_semantics.c" \
	"$root/src/lib/payload_mm_authvar_record.c" \
	"$root/src/lib/payload_mm_authvar_writer.c" -o "$real_output"
reset_count="$(ASAN_OPTIONS=detect_leaks=1 "$real_output" \
	coordinator-reset-count)"
cut=1
while [ "$cut" -le "$reset_count" ]; do
	ASAN_OPTIONS=detect_leaks=1 "$real_output" "coordinator-reset-$cut"
	cut=$((cut + 1))
done
for operation in add remove; do
	private_reset_count="$(ASAN_OPTIONS=detect_leaks=1 "$real_output" \
		"coordinator-private-$operation-reset-count")"
	private_cut=1
	while [ "$private_cut" -le "$private_reset_count" ]; do
		ASAN_OPTIONS=detect_leaks=1 "$real_output" \
			"coordinator-private-$operation-reset-$private_cut"
		private_cut=$((private_cut + 1))
	done
done
native_reset_count="$(ASAN_OPTIONS=detect_leaks=1 "$real_output" \
	coordinator-native-reset-count)"
native_cut=1
while [ "$native_cut" -le "$native_reset_count" ]; do
	ASAN_OPTIONS=detect_leaks=1 "$real_output" \
		"coordinator-native-reset-$native_cut"
	native_cut=$((native_cut + 1))
done
done
