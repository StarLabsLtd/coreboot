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
	source="$2"
	binary="$3"
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -pthread -D__TEST__ -D__COREBOOT__ \
		-DEXECUTOR_SOURCE_INCLUDE="\"$source\"" \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_policy_transaction_test.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" -o "$binary"
}

for optimization in 0 2; do
	binary="$tmp/policy-O$optimization"
	compile "$optimization" "$root/src/lib/payload_mm_authvar_executor.c" "$binary"
	for case in allow reject no-provider bad-install install-before-executor \
		alias-request-result alias-name-data alias-data-result alias-arena \
		alias-name-data-partial alias-name-result alias-result-arena \
		alias-result-executor alias-request-data alias-executor alias-request-name \
		overflow unsupported bad-name preseal-complete preseal-alias \
		preseal-private ready-only runtime-first runtime-then-ready \
		status-invalid-parameter status-unsupported status-device-error \
		status-write-protected status-out-of-resources status-not-found \
		request-control request-name request-data snapshot index index-pointer \
		entries session plan provider-seal phase-seal invalid-kind oversize \
		invalid-delete delete-attributes delete-data zero-write \
		invalid-status recover-reentry transaction-reentry \
		install-reentry lifecycle session-binding store-pressure after-recovery \
		end-failure publication-gate; do
		ASAN_OPTIONS=detect_leaks=1 "$binary" "$case"
	done
	if nm -g "$binary" | rg 'payload_mm_authvar_executor_apply'; then
		echo 'ERROR: raw apply symbol remains exported' >&2
		exit 1
	fi
done

compile_real()
{
	optimization="$1"
	media_source="$2"
	binary="$3"
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-D__SMM__ -DEXECUTOR_REAL_MEDIA \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/lib" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_policy_real_media_test.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$media_source" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" -o "$binary"
}

for optimization in 0 2; do
	binary="$tmp/real-policy-O$optimization"
	compile_real "$optimization" "$root/src/lib/payload_mm_authvar_media.c" "$binary"
	for case in begin program erase end leave-zero leave-wrong leave-token \
		enter-nested transaction-nested; do
		ASAN_OPTIONS=detect_leaks=1 "$binary" "$case"
	done
done

real_mutation()
{
	name="$1"
	expression="$2"
	test_case="$3"
	source="$tmp/media-$name.c"
	sed "$expression" "$root/src/lib/payload_mm_authvar_media.c" > "$source"
	if cmp -s "$source" "$root/src/lib/payload_mm_authvar_media.c"; then
		echo "ERROR: $name media mutation changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/media-$name-O$optimization"
		compile_real "$optimization" "$source" "$binary"
		if ASAN_OPTIONS=detect_leaks=1 "$binary" "$test_case" \
			> "$binary.log" 2>&1; then
			echo "ERROR: $name O$optimization media mutant survived" >&2
			exit 1
		fi
	done
}

real_mutation raw-begin-guard \
	'/^enum payload_mm_authvar_media_result payload_mm_authvar_media_begin(/,/^}/ s/if (provider_reentry())/if (false \&\& provider_reentry())/' begin
real_mutation raw-program-guard \
	'/^enum payload_mm_authvar_media_result payload_mm_authvar_media_program(/,/^}/ s/if (provider_reentry() || callback_reentry()/if (callback_reentry()/' program
real_mutation raw-leave-scope \
	'/^bool payload_mm_authvar_media_provider_leave(/,/^}/ { s/scope->cookie != media.provider_cookie ||/false ||/; s/scope->check != media.provider_check/false/; }' leave-wrong

mutation()
{
	name="$1"
	expression="$2"
	case="$3"
	source="$tmp/$name.c"
	sed "$expression" "$root/src/lib/payload_mm_authvar_executor.c" > "$source"
	if cmp -s "$source" "$root/src/lib/payload_mm_authvar_executor.c"; then
		echo "ERROR: $name mutation changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/$name-O$optimization"
		compile "$optimization" "$source" "$binary"
		if ASAN_OPTIONS=detect_leaks=1 "$binary" "$case" > "$binary.log" 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutation provider-input-seal \
	's/memcmp(before, protected_arena, sealed_size) || !provider_status_valid(status)/false || !provider_status_valid(status)/' request-data
mutation provider-status-gate \
	'/static uint64_t authorize_mutation/,/^uint64_t payload_mm_authvar_executor_recover/ s/if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)/if (false \&\& status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)/' reject
mutation provider-reentry-poison \
	's/__atomic_load_n(\&executor.provider_violation, __ATOMIC_ACQUIRE) ||/false ||/' recover-reentry
mutation provider-output-bound \
	's/if (mutation->data_size > capacity ||/if (false ||/' oversize
mutation mandatory-authorize \
	's/status = authorize_mutation(state);/status = false ? authorize_mutation(state) : PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;/' reject
mutation runtime-monotone \
	's/if (state->request.operation == PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME) {/if (state->request.operation == PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT) {/' lifecycle
mutation arena-scrub \
	's/memset(executor.sealed.arena, 0, executor.sealed.required_size);/memset(state, 0, sizeof(*state));/g' allow
mutation alias-private \
	's/!payload_mm_authvar_buffers_overlap(buffer, size, \&executor,/(true || !payload_mm_authvar_buffers_overlap(buffer, size, \&executor,/; s/sizeof(executor)) \&\&/sizeof(executor))) \&\&/' alias-executor
mutation preseal-completion \
	'/^uint64_t payload_mm_authvar_policy_transaction(/,/^}/ s/if (!request_disjoint(request, completion))/if (true || !request_disjoint(request, completion))/' preseal-complete
mutation zero-nonappend-write \
	's/!mutation->data_size \&\&/false \&\&/' zero-write
mutation delete-timestamp \
	's/memcmp(mutation->timestamp, zero_timestamp, sizeof(zero_timestamp))/(false \&\& memcmp(mutation->timestamp, zero_timestamp, sizeof(zero_timestamp)))/' invalid-delete
early_gate_release='
1a\
extern uint32_t gate_release_observed;
/^uint64_t payload_mm_authvar_policy_transaction(/,/^}/ {
	/__atomic_store_n(&executor.busy, 0, __ATOMIC_RELEASE);/d
	s/memset(executor.sealed.arena, 0, executor.sealed.required_size);/&\
	__atomic_store_n(\&executor.busy, 0, __ATOMIC_RELEASE);\
	while (!__atomic_load_n(\&gate_release_observed, __ATOMIC_ACQUIRE))\
		;/
}'
mutation early-gate-release \
	"$early_gate_release" \
	publication-gate

# Independent public-header negative checks; declarations cannot regain raw apply.
for forbidden in raw-apply media-token mutation-offset; do
	case "$forbidden" in
	raw-apply)
		body='void probe(void) { (void)payload_mm_authvar_executor_apply(0, 0, 0); }' ;;
	media-token)
		body='void probe(struct payload_mm_authvar_policy_view *p) { p->token = 1; }' ;;
	mutation-offset)
		body='void probe(struct payload_mm_authvar_policy_mutation *p) { p->offset = 1; }' ;;
	esac
	printf '%s\n' '#include <boot/payload_mm_authvar_executor.h>' \
		'#include <boot/payload_mm_authvar_policy.h>' "$body" > "$tmp/$forbidden.c"
	if cc -std=gnu11 -Werror -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-c "$tmp/$forbidden.c" -o "$tmp/$forbidden.o" > "$tmp/$forbidden.log" 2>&1; then
		echo "ERROR: $forbidden unexpectedly compiled" >&2
		exit 1
	fi
done

printf '%s\n' 'Payload-MM typed policy transaction tests: PASS'
