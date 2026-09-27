#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
ledger="$temporary/execution-ledger"
: > "$ledger"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_FAIL_STOP_PLATFORM 1' \
	'#define CONFIG_SMM_INVOCATION_EVIDENCE 1' \
	'#define CONFIG_SMM_INVOCATION_ENTRY 1' \
	'#define CONFIG_SMM_INVOCATION_INTEL_ADAPTER 1' \
	> "$temporary/include/config.h"

run_deadline()
(
	seconds=$1
	shift
	marker=$(mktemp "$temporary/deadline.XXXXXX")
	rm -f "$marker"
	setsid "$@" &
	command_pid=$!
	(
		sleep "$seconds"
		if kill -0 "$command_pid" 2>/dev/null; then
			: > "$marker"
			/bin/kill -TERM -- "-$command_pid" 2>/dev/null || true
			sleep 1
			/bin/kill -KILL -- "-$command_pid" 2>/dev/null || true
		fi
	) &
	watchdog_pid=$!
	set +e
	wait "$command_pid"
	status=$?
	set -e
	kill "$watchdog_pid" 2>/dev/null || true
	wait "$watchdog_pid" 2>/dev/null || true
	for ignored in 1 2 3 4 5; do
		if ! /bin/kill -0 -- "-$command_pid" 2>/dev/null; then
			break
		fi
		sleep 1
	done
	if /bin/kill -0 -- "-$command_pid" 2>/dev/null; then
		printf 'deadline left process group %s alive\n' "$command_pid" >&2
		return 125
	fi
	if [ -e "$marker" ]; then
		rm -f "$marker"
		return 124
	fi
	rm -f "$marker"
	return "$status"
)

set +e
run_deadline 1 sh -c 'trap "" TERM; while :; do :; done' \
	>/dev/null 2>&1
watchdog_timeout_status=$?
run_deadline 5 sh -c 'exit 125' >/dev/null 2>&1
watchdog_infrastructure_status=$?
set -e
if [ "$watchdog_timeout_status" -ne 124 ] ||
   [ "$watchdog_infrastructure_status" -ne 125 ]; then
	printf 'deadline classifier self-test failed: timeout=%s infra=%s\n' \
		"$watchdog_timeout_status" "$watchdog_infrastructure_status" >&2
	exit 1
fi
printf '%s\n' 'watchdog-timeout command=self-test actual=124 expected=124 class=timeout' >> "$ledger"
printf '%s\n' 'watchdog-infrastructure command=self-test actual=125 expected=125 class=infrastructure' >> "$ledger"

run_success()
(
	name=$1
	shift
	if [ ! -x "$1" ] && [ "$1" != env ]; then
		printf 'baseline binary missing: %s (%s)\n' "$name" "$1" >&2
		exit 1
	fi
	set +e
	run_deadline 30 "$@"
	status=$?
	set -e
	printf '%s command=%s binary_exists=yes actual=%s expected=0 class=baseline\n' \
		"$name" "$*" "$status" >> "$ledger"
	[ "$status" -eq 0 ]
)

build()
(
	output_name=$1
	flags=$2
	adapter=${3:-$root/src/soc/intel/common/block/smm/invocation_adapter.c}
	entry=${4:-$root/src/cpu/x86/smm_invocation_entry.c}
	evidence=${5:-$root/src/cpu/x86/smm_invocation_evidence.c}
	evidence_loader=${6:-$root/src/cpu/x86/smm_invocation_evidence_loader.c}
	# Deliberate normal flag splitting for this strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-pthread $flags -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/cpu/x86/smm_invocation_entry_adapter_test.c" \
		"$entry" "$evidence" "$evidence_loader" "$adapter" \
		-o "$temporary/$output_name"
)

mutate_component()
(
	mutant_name=$1
	component=$2
	expression=$3
	mutant="$temporary/$mutant_name.c"
	sed "$expression" "$component" > "$mutant"
	if cmp -s "$component" "$mutant"; then
		printf 'mutant did not change source: %s\n' "$mutant_name" >&2
		exit 1
	fi
	adapter=$root/src/soc/intel/common/block/smm/invocation_adapter.c
	entry=$root/src/cpu/x86/smm_invocation_entry.c
	evidence=$root/src/cpu/x86/smm_invocation_evidence.c
	evidence_loader=$root/src/cpu/x86/smm_invocation_evidence_loader.c
	case "$component" in
	*/invocation_adapter.c) adapter=$mutant ;;
	*/smm_invocation_entry.c) entry=$mutant ;;
	*/smm_invocation_evidence.c) evidence=$mutant ;;
	*/smm_invocation_evidence_loader.c) evidence_loader=$mutant ;;
	esac
	for optimization in 0 2; do
		binary="$mutant_name-O$optimization"
		build "$binary" "-O$optimization" "$adapter" \
			"$entry" "$evidence" "$evidence_loader"
		if [ ! -x "$temporary/$binary" ]; then
			printf 'mutant binary missing: %s O%s\n' "$mutant_name" \
				"$optimization" >&2
			exit 1
		fi
		if run_deadline 5 "$temporary/$binary" >/dev/null 2>&1; then
			printf 'surviving mutant: %s O%s\n' "$mutant_name" \
				"$optimization" >&2
			exit 1
		else
			status=$?
		fi
		if [ "$status" -eq 125 ] || [ "$status" -eq 126 ] ||
		   [ "$status" -eq 127 ]; then
			printf 'mutant did not execute: %s O%s (%s)\n' \
				"$mutant_name" "$optimization" "$status" >&2
			exit 1
		fi
		printf 'killed mutant: %s O%s (exit %s)\n' "$mutant_name" \
			"$optimization" "$status"
		printf '%s-O%s command=%s source_changed=yes binary_exists=yes actual=%s expected=nonzero class=mutant-kill\n' \
			"$mutant_name" "$optimization" "$temporary/$binary" \
			"$status" >> "$ledger"
	done
)

for optimization in 0 2; do
	build "plain-O$optimization" "-O$optimization"
	run_success "plain-O$optimization" "$temporary/plain-O$optimization"
	build "sanitize-O$optimization" \
		"-O$optimization -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
	run_success "sanitize-O$optimization" env ASAN_OPTIONS=detect_leaks=1 \
		UBSAN_OPTIONS=halt_on_error=1 "$temporary/sanitize-O$optimization"
done

if build tsan "-O1 -g -fsanitize=thread" >"$temporary/tsan-build.log" 2>&1; then
	run_success tsan env TSAN_OPTIONS=halt_on_error=1 "$temporary/tsan"
else
	cat "$temporary/tsan-build.log" >&2
	exit 1
fi

for source in \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	"$root/src/cpu/x86/smm_invocation_entry.c" \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c"; do
	object="$temporary/$(basename "$source" .c)-32.o"
	${CC:-cc} -m32 -march=i686 -std=gnu11 -Wall -Wextra -Werror \
		-Wshadow -ffreestanding -fno-builtin -D__TEST__ \
		-D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -c "$source" -o "$object"
	if nm -u "$object" | grep -q '__atomic_'; then
		printf '%s gained a 32-bit libatomic dependency\n' "$source" >&2
		exit 1
	fi
done
ld -m elf_i386 -r "$temporary"/*-32.o -o "$temporary/entry-adapter-32.o"

for source in smm_invocation_evidence smm_invocation_entry; do
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -c \
		"$root/src/cpu/x86/$source.c" -o "$temporary/$source.o"
	[ "$(nm -u "$temporary/$source.o" | \
		awk '$2 == "smm_invocation_platform_fail_stop" { count++ } END { print count + 0 }')" \
		-eq 1 ]
done
printf '%s\n' 'int main(void) { return 0; }' > "$temporary/fail-stop-main.c"
if ${CC:-cc} "$temporary/fail-stop-main.c" \
	"$temporary/smm_invocation_evidence.o" \
	"$temporary/smm_invocation_entry.o" -o "$temporary/no-provider" \
	>"$temporary/no-provider.log" 2>&1; then
	printf '%s\n' 'SMM invocation objects linked without fail-stop provider' >&2
	exit 1
fi
grep -q 'smm_invocation_platform_fail_stop' "$temporary/no-provider.log"
printf '%s\n' \
	'#include <cpu/x86/smm_invocation_fail_stop.h>' \
	'void smm_invocation_platform_fail_stop(void) { __builtin_trap(); }' \
	> "$temporary/fail-stop-provider.c"
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -D__COREBOOT__ \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
	-I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" \
	-c "$temporary/fail-stop-provider.c" \
	-o "$temporary/fail-stop-provider.o"
${CC:-cc} "$temporary/fail-stop-main.c" \
	"$temporary/smm_invocation_evidence.o" \
	"$temporary/smm_invocation_entry.o" \
	"$temporary/fail-stop-provider.o" -o "$temporary/one-provider"
if ${CC:-cc} "$temporary/fail-stop-main.c" \
	"$temporary/smm_invocation_evidence.o" \
	"$temporary/smm_invocation_entry.o" \
	"$temporary/fail-stop-provider.o" "$temporary/fail-stop-provider.o" \
	-o "$temporary/two-providers" >"$temporary/two-providers.log" 2>&1; then
	printf '%s\n' 'duplicate SMM fail-stop providers linked' >&2
	exit 1
fi
grep -q 'multiple definition.*smm_invocation_platform_fail_stop' \
	"$temporary/two-providers.log"

for bits in 32 64; do
	arch=x86_$bits
	${CC:-cc} -m$bits -std=gnu11 -ffreestanding -fno-builtin \
		-D__COREBOOT__ -D__SMM__ -D__ARCH_${arch}__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-c "$root/src/cpu/x86/smm/smm_stub.S" \
		-o "$temporary/smm-stub-entry-$bits.o"
	objdump -dr "$temporary/smm-stub-entry-$bits.o" > \
		"$temporary/smm-stub-entry-$bits.dis"
	grep -Eq 'cmp.*\(%[er]bx,%[er]cx,4\),%eax' \
		"$temporary/smm-stub-entry-$bits.dis"
done
grep -Eq 'push[[:space:]]+%esi' "$temporary/smm-stub-entry-32.dis"
grep -Eq 'push[[:space:]]+%rsi' "$temporary/smm-stub-entry-64.dis"

mkdir -p "$temporary/off/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_EVIDENCE 1' \
	'#define CONFIG_SMM_INVOCATION_ENTRY 0' \
	'#define CONFIG_SMM_INVOCATION_INTEL_ADAPTER 0' \
	> "$temporary/off/include/config.h"
fixture="$root/tests/cpu/x86/fixtures/smm_stub_config_off.S"
fixture_hash=$(sha256sum "$fixture" | cut -d' ' -f1)
if [ "$fixture_hash" != \
	3377a45d16b4b9c7dcf7ac54dabd5d27a9fe7f06433d2b42d628d74cff5577aa ]; then
	printf '%s\n' 'config-off SMM stub fixture changed unexpectedly' >&2
	exit 1
fi
for bits in 32 64; do
	arch=x86_$bits
	for version in base current; do
		source="$root/src/cpu/x86/smm/smm_stub.S"
		[ "$version" = base ] && source="$fixture"
		${CC:-cc} -m$bits -std=gnu11 -ffreestanding -fno-builtin \
			-D__COREBOOT__ -D__SMM__ -D__ARCH_${arch}__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$temporary/off/include" -I"$root/src/include" \
			-I"$root/src" -I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" -c "$source" \
			-o "$temporary/off/smm-stub-$version-$bits.o"
	done
	cmp "$temporary/off/smm-stub-base-$bits.o" \
		"$temporary/off/smm-stub-current-$bits.o"
done

mutate()
(
	mutant_name=$1
	expression=$2
	mutant="$temporary/$mutant_name.c"
	sed "$expression" \
		"$root/src/soc/intel/common/block/smm/invocation_adapter.c" > "$mutant"
	if cmp -s "$root/src/soc/intel/common/block/smm/invocation_adapter.c" \
		"$mutant"; then
		printf 'mutant did not change source: %s\n' "$mutant_name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$mutant_name-O$optimization"
		build "$binary" "-O$optimization" "$mutant"
		if [ ! -x "$temporary/$binary" ]; then
			printf 'mutant binary missing: %s O%s\n' "$mutant_name" \
				"$optimization" >&2
			exit 1
		fi
		if run_deadline 5 "$temporary/$binary" >/dev/null 2>&1; then
			printf 'surviving mutant: %s O%s\n' "$mutant_name" \
				"$optimization" >&2
			exit 1
		else
			status=$?
		fi
		if [ "$status" -eq 125 ] || [ "$status" -eq 126 ] ||
		   [ "$status" -eq 127 ]; then
			printf 'mutant did not execute: %s O%s (%s)\n' \
				"$mutant_name" "$optimization" "$status" >&2
			exit 1
		fi
		printf 'killed mutant: %s O%s (exit %s)\n' "$mutant_name" \
			"$optimization" "$status"
		printf '%s-O%s command=%s source_changed=yes binary_exists=yes actual=%s expected=nonzero class=mutant-kill\n' \
			"$mutant_name" "$optimization" "$temporary/$binary" \
			"$status" >> "$ledger"
	done
)

mutate immediate-out \
	's/#define APMC_OUT_DX_BYTE_IO_MISC .*/#define APMC_OUT_DX_BYTE_IO_MISC 0x00b20083U/'
mutate low-port-alias 's/first.io_misc != APMC_OUT_DX_BYTE_IO_MISC/(first.io_misc \& 0x00ffffffU) != APMC_OUT_DX_BYTE_IO_MISC/'
mutate revision-binding '0,/tuple->revision == node->revision/{s//true/}'
mutate sealed-read \
	'0,/!sealed_tuple(adapter, cpu, \&first)/{s//false/}'
mutate_component ack-before-claim \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_claim/,/phase_claim/{s/__atomic_load_n(\&evidence->rendezvous_ack_required/false \&\& __atomic_load_n(\&evidence->rendezvous_ack_required/}'
mutate_component ack-arm-reload \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_require_rendezvous_ack/,/^}/{s/if (expected == 1U)/if (expected == 1U \&\& false)/}'
mutate_component eos-generation \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	's/generation != evidence->closed_generation/(generation != evidence->closed_generation \&\& false)/'
mutate_component eos-lifecycle \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	's/lifecycle != evidence->closed_lifecycle/(lifecycle != evidence->closed_lifecycle \&\& false)/'
mutate_component eos-one-use \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_eos_consume/,/^}/{s/\&expected, 1U, false/\&expected, expected, false/}'
mutate_component policy-source-stability \
	"$root/src/cpu/x86/smm_invocation_entry.c" \
	's/!memcmp(policy, policy_snapshot, sizeof(\*policy_snapshot))/(!memcmp(policy, policy_snapshot, sizeof(*policy_snapshot)) || true)/'
mutate_component ticket-command-publication \
	"$root/src/cpu/x86/smm_invocation_entry.c" \
	's/ticket_snapshot.command = snapshot.command;/ticket_snapshot.command = 0U;/'
mutate_component ticket-reserved-validation \
	"$root/src/cpu/x86/smm_invocation_entry.c" \
	's/!memcmp(ticket->reserved, zero, sizeof(zero))/(!memcmp(ticket->reserved, zero, sizeof(zero)) || true)/'
mutate_component linked-fail-stop \
	"$root/src/cpu/x86/smm_invocation_entry.c" \
	's/smm_invocation_platform_fail_stop();/__builtin_trap();/'
mutate_component depart-precommit-ticket \
	"$root/src/cpu/x86/smm_invocation_entry.c" \
	'/smm_invocation_entry_depart/,/return CB_SUCCESS;/{s/if (memcmp(ticket, \&snapshot, sizeof(snapshot)))/if (false)/}'
mutate_component eos-precommit-ticket \
	"$root/src/cpu/x86/smm_invocation_entry.c" \
	'/smm_invocation_entry_eos_ready/,/return true;/{s/if (memcmp(ticket, \&snapshot, sizeof(snapshot)))/if (false)/}'
mutate_component ack-phase-ownership \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_rendezvous_ack_try(/,/^}/{s/SMM_INVOCATION_ACK_ADMITTING))/SMM_INVOCATION_COLLECTING))/}'
mutate_component arm-phase-ownership \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_require_rendezvous_ack_try(/,/^}/{s/SMM_INVOCATION_ACK_ARMING))/SMM_INVOCATION_READY))/}'
mutate_component eos-phase-ownership \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_eos_consume(/,/^}/{s/SMM_INVOCATION_EOS_ADMITTING))/SMM_INVOCATION_READY))/}'
mutate_component admission-fail-request-latch \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_admission_fail(/,/^}/{s/\&evidence->rendezvous_fail_requested, 1U/\&evidence->rendezvous_fail_requested, 0U/}'
mutate_component terminal-fail-marker \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/static void scrub_fields/,/^}/{s/\&evidence->rendezvous_fail_requested, 1U/\&evidence->rendezvous_fail_requested, 0U/}'
mutate_component fail-phase-linearization \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_rendezvous_fail/,/^}/{s/SMM_INVOCATION_COLLECTING))/SMM_INVOCATION_CLAIMING))/}'
mutate_component arm-retry-classification \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/TEST_HOOK(19)/,/if (!resampled)/{s/SMM_INVOCATION_TRY_RETRY/SMM_INVOCATION_TRY_ERROR/}'
mutate_component arrival-retry-classification \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/TEST_HOOK(20)/,/if (!resampled)/{s/SMM_INVOCATION_TRY_RETRY/SMM_INVOCATION_TRY_ERROR/}'
mutate_component ack-retry-classification \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/TEST_HOOK(21)/,/if (!resampled)/{s/SMM_INVOCATION_TRY_RETRY/SMM_INVOCATION_TRY_ERROR/}'
mutate_component admission-instance-binding \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/token->evidence_identity != (uintptr_t)evidence/{s/token->evidence_identity != (uintptr_t)evidence/false/}'
mutate_component poison-admitting-handoff \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/static bool poison_admit/,/^}/{s/SMM_INVOCATION_POISON_ADMITTING))/SMM_INVOCATION_POISONING))/}'
mutate_component admission-token-consume \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_admission_fail(/,/^}/{s/| target |$/| target |\/\* mutant \*\//; s/ADMISSION_CONSUMED;/0U;/}'
mutate_component bounded-depart \
	"$root/src/cpu/x86/smm_invocation_entry.c" \
	'/smm_invocation_entry_depart(/,/^}/{s/poll + 1U == snapshot.max_polls/false/}'
mutate_component admission-nonce-no-wrap \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/static bool admission_reserve/,/^}/{s/!nonce || nonce > ADMISSION_NONCE_MAX/false/}'
mutate_component arm-lost-close-cleanup \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/TEST_HOOK(9)/,/terminal_scrub(evidence)/{/admission_complete(evidence,/{N;d;}}'
mutate_component retry-token-resnapshot \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/static bool admission_retry_token/,/^}/{s/invocation_generation, control);/invocation_generation, (token->attempt_nonce << ADMISSION_NONCE_SHIFT) | kind | ADMISSION_BUSY);/}'
mutate_component packed-admission-phase \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/static bool admission_reserve/,/^}/{s/ADMISSION_BUSY | owned_phase/ADMISSION_BUSY | expected_phase | (owned_phase \& 0U)/}'
mutate_component departure-final-arbitration \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_depart_try(/,/^}/{s/SMM_INVOCATION_DEPARTURE_COMMITTING))/SMM_INVOCATION_CLOSING))/}'
mutate_component departure-fail-handoff \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_ticket_fail(/,/^}/{s/phase_publish(evidence, SMM_INVOCATION_POISONING)/phase_publish(evidence, SMM_INVOCATION_CLOSING)/}'
mutate_component admission-fail-phase-only \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_admission_fail(/,/^}/{s/if (__atomic_compare_exchange_n(\&evidence->state,/if (phase_claim(evidence, phase, target) || __atomic_compare_exchange_n(\&evidence->state,/}'
mutate_component shutdown-side-word \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_shutdown(/,/^}/{s/if (!shutdown_request(evidence, phase))/if ((evidence->shutdown_reserved = 1U, false))/}'
mutate_component reentry-side-word \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/static void record_reentry/,/^}/{s/TEST_HOOK(34);/TEST_HOOK(34); evidence->reentry_reserved = 1U;/}'
mutate_component admission-complete-one-try \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/static bool admission_complete/,/^}/{s/attempt < 4U/attempt < 1U/}'
mutate adapter-active-bound \
	'/intel_smm_invocation_adapter_ops(/,/^}/{s/snapshot.active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS/false/}'
mutate adapter-snapshot-stability \
	's/if (memcmp(\&snapshot, adapter, sizeof(snapshot)))/if (false)/'
mutate adapter-nonce-sticky \
	's/adapter->reserved = 1U;/adapter->reserved = 0U;/'
mutate_component packed-close-owner \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/static enum cb_err close_owned/,/^}/{s/uint32_t state;/uint32_t state; evidence->close_reserved = 1U;/; s/INVOCATION_CLOSE_REQUESTED |/0U |/}'
mutate_component provisioning-latch-preservation \
	"$root/src/cpu/x86/smm_invocation_evidence_loader.c" \
	'/EVIDENCE_LOADER_TEST_HOOK(36);/a\
\t__atomic_store_n(\&evidence->state, SMM_INVOCATION_PROVISIONING, __ATOMIC_RELEASE);'
mutate_component admission-latch-preservation \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	's/if (state \& INVOCATION_LATCH_MASK)/if (state \& INVOCATION_LATCH_MASK \&\& false)/g; s/control \& (ADMISSION_BUSY | INVOCATION_LATCH_MASK)/control \& ADMISSION_BUSY/; s/(control \& INVOCATION_LATCH_MASK) |/(control \& 0U) |/'
mutate_component terminal-scrub-fail-stop \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/phase == SMM_INVOCATION_TERMINAL_SCRUBBING/,/continue;/{s/invocation_fail_stop();/__builtin_trap();/}'
mutate_component terminal-scrub-bounded \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/phase == SMM_INVOCATION_TERMINAL_SCRUBBING/,/continue;/{s/if (++spins == 10000000U)/if (false)/}'
mutate_component provision-dirty-empty \
	"$root/src/cpu/x86/smm_invocation_evidence_loader.c" \
	's/uint32_t expected = SMM_INVOCATION_EMPTY;/uint32_t expected = __atomic_load_n(\&evidence->state, __ATOMIC_ACQUIRE);/'
mutate_component cleanup-latch-retry \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/static bool cleanup_claim/,/^}/{s/attempt < INVOCATION_LATCH_CAS_ATTEMPTS/attempt < 1U/}'
mutate_component phase-claim-latch-retry \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/static bool phase_claim/,/^}/{s/attempt < INVOCATION_LATCH_CAS_ATTEMPTS/attempt < 1U/}'
mutate_component ack-arm-published-retry \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/if (expected == 1U)/,/if (expected)/{s/if (phase == SMM_INVOCATION_ACK_ARMING)/if (false)/}'
mutate_component departure-published-retry \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	'/smm_invocation_evidence_depart_try(/,/^}/{s/phase == SMM_INVOCATION_DEPARTURE_ADMITTING ||/phase == SMM_INVOCATION_DEPARTURE_ADMITTING \&\&/}'

if rg -q 'smm_invocation_entry_(arrive|depart|eos_ready)' "$root/src" \
	-g '!src/cpu/x86/smm_invocation_entry.c' \
	-g '!src/include/cpu/x86/smm_invocation_entry.h'; then
	printf '%s\n' 'dormant entry helper gained a production callsite' >&2
	exit 1
fi
if rg -q 'select[[:space:]]+SMM_INVOCATION_(ENTRY|INTEL_ADAPTER)' \
	"$root/src"; then
	printf '%s\n' 'dormant entry or adapter became selected' >&2
	exit 1
fi
fail_stop_selectors=$(rg -l \
	'select[[:space:]]+SMM_INVOCATION_FAIL_STOP_PLATFORM' "$root/src" | sort)
expected_fail_stop_selectors=$(printf '%s\n' \
	"$root/src/mainboard/emulation/qemu-q35/Kconfig" \
	"$root/src/mainboard/starlabs/starbook/Kconfig" | sort)
if [ "$fail_stop_selectors" != "$expected_fail_stop_selectors" ]; then
	printf '%s\n' 'unexpected SMM invocation fail-stop selector set' >&2
	exit 1
fi
if rg -q 'reset_context|fail_context|smm_invocation_(entry_reset|fail_stop)_fn' \
	"$root/src/include/cpu/x86/smm_invocation_entry.h" \
	"$root/src/include/cpu/x86/smm_invocation_evidence.h" \
	"$root/src/cpu/x86/smm_invocation_entry.c" \
	"$root/src/cpu/x86/smm_invocation_evidence.c"; then
	printf '%s\n' 'SMM invocation state regained fail-stop pointer transport' >&2
	exit 1
fi
if rg -n '__builtin_trap|(^|[^[:alnum:]_])abort[[:space:]]*\(|\bhlt\b' \
	"$root/src/cpu/x86/smm_invocation_entry.c" \
	"$root/src/cpu/x86/smm_invocation_evidence.c"; then
	printf '%s\n' 'SMM invocation production code gained a local fail-stop' >&2
	exit 1
fi
if rg -q '__weak.*smm_invocation_platform_fail_stop' "$root/src"; then
	printf '%s\n' 'SMM invocation fail-stop provider became weak' >&2
	exit 1
fi
fail_stop_providers=$(rg -l '^void smm_invocation_platform_fail_stop\(void\)$' \
	"$root/src" | sort)
expected_fail_stop_providers=$(printf '%s\n' \
	"$root/src/mainboard/emulation/qemu-q35/smm_invocation_fail_stop.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/smm_invocation_fail_stop.c" | sort)
if [ "$fail_stop_providers" != "$expected_fail_stop_providers" ]; then
	printf '%s\n' 'unexpected SMM invocation fail-stop provider set' >&2
	exit 1
fi
grep -q '^smm-$(CONFIG_SMM_INVOCATION_INTEL_ADAPTER) += invocation_adapter.c$' \
	"$root/src/soc/intel/common/block/smm/Makefile.mk"
grep -q 'cmp.*(%ebx, %ecx, 4), %eax' "$root/src/cpu/x86/smm/smm_stub.S"
grep -q 'push.*%rsi.*initial_apic_id' "$root/src/cpu/x86/smm/smm_stub.S"

git -C "$root" diff --check
git -C "$root" ls-files --others --exclude-standard |
while IFS= read -r file; do
	set +e
	git -C "$root" diff --no-index --check -- /dev/null "$file"
	status=$?
	set -e
	if [ "$status" -gt 1 ]; then
		printf 'untracked diff-check failed: %s (exit %s)\n' \
			"$file" "$status" >&2
		exit 1
	fi
done

ledger_lines=$(wc -l < "$ledger")
if [ "$ledger_lines" -ne 109 ]; then
	printf 'execution ledger incomplete: got %s expected 109\n' \
		"$ledger_lines" >&2
	exit 1
fi
cat "$ledger"
