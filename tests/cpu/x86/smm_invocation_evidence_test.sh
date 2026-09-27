#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_FAIL_STOP_PLATFORM 1' \
	'#define CONFIG_SMM_INVOCATION_EVIDENCE 1' \
	> "$temporary/include/config.h"

build()
{
	build_name=$1
	build_flags=$2
	build_source=${3:-$root/src/cpu/x86/smm_invocation_evidence.c}
	# Deliberate normal flag splitting for this strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-pthread $build_flags -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/cpu/x86/smm_invocation_evidence_test.c" \
		"$build_source" \
		"$root/src/cpu/x86/smm_invocation_evidence_loader.c" \
		-o "$temporary/$build_name"
}

for optimization in 0 2; do
	build "plain-O$optimization" "-O$optimization"
	"$temporary/plain-O$optimization"
	build "sanitize-O$optimization" \
		"-O$optimization -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/sanitize-O$optimization"
done

if build tsan "-O1 -g -fsanitize=thread" >"$temporary/tsan-build.log" 2>&1; then
	TSAN_OPTIONS=halt_on_error=1 "$temporary/tsan"
else
	cat "$temporary/tsan-build.log" >&2
	exit 1
fi

generation_publication_atomic()
{
	source=$1
	grep -A1 -F 'generation = __atomic_add_fetch(&evidence->generation, 1U,' \
		"$source" | grep -q '__ATOMIC_RELEASE);'
	! grep -Eq '(^|[^[:alnum:]_])evidence->generation\+\+' "$source"
}
generation_publication_atomic \
	"$root/src/cpu/x86/smm_invocation_evidence.c"
sed 's/generation = __atomic_add_fetch(&evidence->generation, 1U,/generation = ++evidence->generation; \/\*/' \
	"$root/src/cpu/x86/smm_invocation_evidence.c" > \
	"$temporary/generation-publication-mutant.c"
! generation_publication_atomic \
	"$temporary/generation-publication-mutant.c"

${CC:-cc} -m32 -march=i686 -Os -fstack-usage -std=gnu11 \
	-Wall -Wextra -Werror \
	-Wconversion -Wshadow -ffreestanding -fno-builtin -D__TEST__ \
	-D__COREBOOT__ -include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" -c \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	-o "$temporary/smm-invocation-evidence-32.o"
if nm -u "$temporary/smm-invocation-evidence-32.o" | \
	grep -q '__atomic_'; then
	printf '%s\n' '32-bit SMM object gained a libatomic dependency' >&2
	exit 1
fi
ld -m elf_i386 -r "$temporary/smm-invocation-evidence-32.o" \
	-o "$temporary/smm-invocation-evidence-32-linked.o"
stack_file="$temporary/smm-invocation-evidence-32.su"
stack_value()
{
	stack_name=$1
	awk -F '\t' -v name="$stack_name" \
		'$1 ~ (":" name "$") { print $2; found = 1 }
		 END { if (!found) exit 1 }' "$stack_file"
}
strong_stack=$(stack_value smm_invocation_evidence_publish_and_request_close)
geometry_stack=$(stack_value claimed_geometry_valid)
build_token_stack=$(stack_value build_token)
strong_chain=$((strong_stack + geometry_stack + build_token_stack))
if [ "$strong_chain" -gt 1024 ]; then
	printf 'strong completion stack chain exceeds 1 KiB: %s\n' \
		"$strong_chain" >&2
	exit 1
fi

mutation()
{
	mutation_name=$1
	mutation_expression=$2
	mutation_source="$temporary/$mutation_name.c"
	sed "$mutation_expression" \
		"$root/src/cpu/x86/smm_invocation_evidence.c" > \
		"$mutation_source"
	if cmp -s "$root/src/cpu/x86/smm_invocation_evidence.c" \
		"$mutation_source"; then
		printf 'mutation changed nothing: %s\n' "$mutation_name" >&2
		exit 1
	fi
	for mutation_optimization in 0 2; do
		mutation_binary="$mutation_name-O$mutation_optimization"
		build "$mutation_binary" "-O$mutation_optimization" \
			"$mutation_source"
		if [ ! -x "$temporary/$mutation_binary" ]; then
			printf 'mutant binary missing: %s\n' "$mutation_binary" >&2
			exit 1
		fi
		set +e
		timeout -k 2 20 "$temporary/$mutation_binary" >/dev/null 2>&1
		mutation_status=$?
		set -e
		if [ "$mutation_status" -eq 0 ]; then
			printf 'surviving mutant: %s O%s\n' "$mutation_name" \
				"$mutation_optimization" >&2
			exit 1
		fi
		if [ "$mutation_status" -eq 124 ] || \
		   [ "$mutation_status" -eq 125 ] || \
		   [ "$mutation_status" -eq 126 ] || \
		   [ "$mutation_status" -eq 127 ]; then
			printf 'mutant execution failed: %s status %s\n' \
				"$mutation_binary" "$mutation_status" >&2
			exit 1
		fi
	done
}

mutation exact-one 's/matches != 1U/matches == 0U/'
mutation linked-fail-stop \
	's/smm_invocation_platform_fail_stop();/__builtin_trap();/'
mutation terminal-scrub-fail-stop \
	'/phase == SMM_INVOCATION_TERMINAL_SCRUBBING/,/continue;/{s/invocation_fail_stop();/__builtin_trap();/}'
mutation exact-bsp 's/initiator != evidence->bsp_cpu/false/'
mutation sentinel-command 's/(uint8_t)sentinel != command/false/'
mutation exact-apic \
	'0,/apic_id != evidence->participant_apic_ids\[cpu\]/{s//false/}'
mutation exact-generation \
	'/smm_invocation_evidence_depart/,/return CB_ERR/{s/generation != evidence->generation/(generation == evidence->generation \&\& false)/}'
mutation full-rendezvous 's/evidence->expected_cpus ||/0 ||/'
mutation invalid-match \
	's/(match != SMM_INVOCATION_NOT_MATCHED \&\&/(false \&\&/'
mutation reentry-owner \
	'/smm_invocation_evidence_claim/,/^}/{0,/callback_reentered(evidence)/{s//false/}}'
mutation topology-proof \
	'/static void build_token/,/^}/{s/evidence->participant_apic_ids\[cpu\]/0/}'
mutation loader-instance-low-digest \
	'/static void build_token/,/^}/{s/evidence->loader_instance_nonce.low/0/g;}'
mutation loader-instance-high-digest \
	'/static void build_token/,/^}/{s/evidence->loader_instance_nonce.high/0/g;}'
mutation shutdown-boundary \
	'0,/smm_invocation_evidence_shutdown_requested(evidence)/{s//false/}'
mutation arrival-ownership \
	'0,/if (!admission_reserve(evidence,/{s//if (false \&\& !admission_reserve(evidence,/}'
mutation departure-publish-order \
	'/if ((old | (1ULL << cpu)) != evidence->expected_cpus)/,/return SMM_INVOCATION_TRY_SUCCESS;/{s/__atomic_fetch_sub(\&evidence->departure_writers, 1U,/if (false) __atomic_fetch_sub(\&evidence->departure_writers, 1U,/; s/TEST_HOOK(54);/TEST_HOOK(54); __atomic_fetch_sub(\&evidence->departure_writers, 1U, __ATOMIC_RELEASE);/}'
mutation claim-publication-cas \
	'0,/!phase_claim(evidence, SMM_INVOCATION_CLAIMING,/{s//false \&\& !phase_claim(evidence, SMM_INVOCATION_CLAIMING,/}'
mutation restore-binding 's/return unchanged;/return true;/'
mutation departure-admission \
	'/smm_invocation_evidence_depart_try/,/^}/{0,/!state_claim_exact(evidence, state,/{s//false \&\& !state_claim_exact(evidence, state,/}}'
mutation invalid-participant-poison \
	'/cpu >= evidence->active_cpus/,/participant =/{s/(void)poison_owned(evidence, SMM_INVOCATION_COLLECTING);/(void)0;/}'
mutation admission-success-phase \
	'/static bool invocation_progress_phase/,/^}/{s/{/{ if (1) return true;/}'
mutation admission-arrive-completion \
	'/smm_invocation_evidence_arrive_try/,/smm_invocation_evidence_rendezvous_ready/{s/!admission_completed/(admission_completed \&\& false)/}'
mutation admission-arrival-writer-early-publication \
	'/TEST_HOOK(67);/i\
\t__atomic_fetch_sub(&evidence->arrival_writers, 1U, __ATOMIC_RELEASE);'
mutation admission-arrive-consumed \
	'/SMM_INVOCATION_ADMISSION_ARRIVE,/{:a;N;/completed_control & (ADMISSION_CONSUMED |/!ba; s/ADMISSION_CONSUMED |/0 |/;}'
mutation admission-arm-consumed \
	'/SMM_INVOCATION_ADMISSION_ARM,/{:a;N;/completed_control & (ADMISSION_CONSUMED |/!ba; s/ADMISSION_CONSUMED |/0 |/;}'
mutation admission-arm-completion \
	'/smm_invocation_evidence_require_rendezvous_ack_try/,/smm_invocation_evidence_admission_fail/{s/!admission_completed/(admission_completed \&\& false)/}'
mutation admission-arm-phase \
	'/SMM_INVOCATION_ADMISSION_ARM,/{:a;N;/SMM_INVOCATION_READY)/!ba; s/(completed_control & STATE_PHASE_MASK) != SMM_INVOCATION_READY/false/;}'
mutation admission-arm-required-phase-ping-pong-error \
	'/TEST_HOOK(65);/,/return SMM_INVOCATION_TRY_RETRY;/{s/return SMM_INVOCATION_TRY_RETRY;/return SMM_INVOCATION_TRY_ERROR;/;}'
mutation admission-arm-zero-phase-ping-pong-error \
	'/TEST_HOOK(64);/,/return SMM_INVOCATION_TRY_RETRY;/{s/return SMM_INVOCATION_TRY_RETRY;/return SMM_INVOCATION_TRY_ERROR;/;}'
mutation admission-arm-reserve-ping-pong-error \
	'/TEST_HOOK(66);/,/return SMM_INVOCATION_TRY_RETRY;/{s/return SMM_INVOCATION_TRY_RETRY;/return SMM_INVOCATION_TRY_ERROR;/;}'
mutation admission-arm-required-phase-ping-pong-poison \
	'/TEST_HOOK(65);/,/return SMM_INVOCATION_TRY_RETRY;/{s/return SMM_INVOCATION_TRY_RETRY;/(void)poison_owned(evidence, SMM_INVOCATION_READY); return SMM_INVOCATION_TRY_RETRY;/;}'
mutation admission-arm-zero-phase-ping-pong-poison \
	'/TEST_HOOK(64);/,/return SMM_INVOCATION_TRY_RETRY;/{s/return SMM_INVOCATION_TRY_RETRY;/(void)poison_owned(evidence, SMM_INVOCATION_READY); return SMM_INVOCATION_TRY_RETRY;/;}'
mutation admission-arm-reserve-ping-pong-poison \
	'/TEST_HOOK(66);/,/return SMM_INVOCATION_TRY_RETRY;/{s/return SMM_INVOCATION_TRY_RETRY;/(void)poison_owned(evidence, SMM_INVOCATION_READY); return SMM_INVOCATION_TRY_RETRY;/;}'
mutation admission-arm-stale-zero-progress-error \
	'/TEST_HOOK(73);/,/return SMM_INVOCATION_TRY_RETRY;/{s/return SMM_INVOCATION_TRY_RETRY;/return SMM_INVOCATION_TRY_ERROR;/;}'
mutation admission-arm-stale-zero-unbound \
	'/TEST_HOOK(73);/,/if (expected)/{s/if (phase == SMM_INVOCATION_OPENING ||/if (true ||/;}'
mutation admission-arm-stale-zero-arming-error \
	'/TEST_HOOK(73);/,/if (expected)/{s/phase == SMM_INVOCATION_ACK_ARMING)/false)/;}'
mutation admission-arm-stale-one-reread-removed \
	'/TEST_HOOK(74);/,/TEST_HOOK(75);/{s/expected = __atomic_load_n(/expected = 1U; (void)__atomic_load_n(/;}'
mutation admission-arm-stale-one-transition-success \
	'/TEST_HOOK(76);/,/return SMM_INVOCATION_TRY_RETRY;/{s/return SMM_INVOCATION_TRY_RETRY;/return SMM_INVOCATION_TRY_SUCCESS;/;}'
mutation admission-arm-stale-one-transition-error \
	'/TEST_HOOK(76);/,/return SMM_INVOCATION_TRY_RETRY;/{s/return SMM_INVOCATION_TRY_RETRY;/return SMM_INVOCATION_TRY_ERROR;/;}'
mutation admission-ack-consumed \
	'/SMM_INVOCATION_ADMISSION_ACK,/{:a;N;/completed_control & (ADMISSION_CONSUMED |/!ba; s/ADMISSION_CONSUMED |/0 |/;}'
mutation admission-ack-completion \
	'/smm_invocation_evidence_rendezvous_ack_try/,/smm_invocation_evidence_publish_and_request_close/{s/!admission_completed/(admission_completed \&\& false)/}'
mutation admission-ack-shutdown \
	'/SMM_INVOCATION_ADMISSION_ACK,/{:a;N;/INVOCATION_SHUTDOWN_REQUESTED/!ba; s/INVOCATION_SHUTDOWN_REQUESTED/0/;}'
mutation admission-ack-phase-ping-pong-error \
	'/TEST_HOOK(55);/,/phase != SMM_INVOCATION_COLLECTING/{s/return SMM_INVOCATION_TRY_RETRY;/return SMM_INVOCATION_TRY_ERROR;/;}'
mutation admission-ack-reserve-ping-pong-error \
	'/TEST_HOOK(57);/,/admission_token_fill/{s/return SMM_INVOCATION_TRY_RETRY;/return SMM_INVOCATION_TRY_ERROR;/;}'
mutation admission-ack-ping-pong-poison \
	'/TEST_HOOK(57);/,/admission_token_fill/{s/return SMM_INVOCATION_TRY_RETRY;/(void)poison_owned(evidence, SMM_INVOCATION_COLLECTING); return SMM_INVOCATION_TRY_RETRY;/;}'
mutation admission-arrive-phase-ping-pong-error \
	'/TEST_HOOK(59);/,/phase != SMM_INVOCATION_COLLECTING/{s/return SMM_INVOCATION_TRY_RETRY;/return SMM_INVOCATION_TRY_ERROR;/;}'
mutation admission-arrive-arm-owner-error \
	's/phase == SMM_INVOCATION_ACK_ARMING ||/false ||/'
mutation admission-arrive-ready-ping-pong-error \
	'/TEST_HOOK(72);/,/return SMM_INVOCATION_TRY_RETRY;/{s/return SMM_INVOCATION_TRY_RETRY;/return SMM_INVOCATION_TRY_ERROR;/;}'
mutation admission-arrive-ready-ping-pong-poison \
	'/TEST_HOOK(72);/,/return SMM_INVOCATION_TRY_RETRY;/{s/return SMM_INVOCATION_TRY_RETRY;/(void)poison_owned(evidence, SMM_INVOCATION_COLLECTING); return SMM_INVOCATION_TRY_RETRY;/;}'
mutation admission-arrive-reserve-ping-pong-error \
	'/TEST_HOOK(60);/,/admission_token_fill/{s/return SMM_INVOCATION_TRY_RETRY;/return SMM_INVOCATION_TRY_ERROR;/;}'
mutation admission-arrive-ping-pong-poison \
	'/TEST_HOOK(60);/,/admission_token_fill/{s/return SMM_INVOCATION_TRY_RETRY;/(void)poison_owned(evidence, SMM_INVOCATION_COLLECTING); return SMM_INVOCATION_TRY_RETRY;/;}'
mutation strong-callback-phase \
	'/smm_invocation_evidence_publish_and_request_close/,/^}/{0,/invocation_fail_stop();/{s/invocation_fail_stop();/return CB_ERR;/}}'
mutation strong-acquisition-cas-loss \
	'/TEST_HOOK(49);/,/TEST_HOOK(45);/{s/invocation_fail_stop();/return CB_ERR;/}'
mutation strong-canonical-busy \
	'/smm_invocation_evidence_publish_and_request_close/,/^}/{s/ADMISSION_BUSY | ADMISSION_CONSUMED |/0 | 0 |/}'
mutation strong-canonical-latch \
	'/smm_invocation_evidence_publish_and_request_close/,/^}/{s/INVOCATION_LATCH_MASK/0/}'
mutation strong-canonical-nonce \
	'/smm_invocation_evidence_publish_and_request_close/,/^}/{s/!(state >> ADMISSION_NONCE_SHIFT)/false/}'
mutation strong-canonical-kind \
	'/smm_invocation_evidence_publish_and_request_close/,/^}/{s/kind != expected_kind/(kind != expected_kind \&\& false)/}'
mutation strong-second-phase-sample \
	'/TEST_HOOK(51);/,/SMM_INVOCATION_CLAIMED)/{s/if (callback_owns_phase/if (false \&\& callback_owns_phase/}'
mutation strong-evidence-token-overlap \
	'/smm_invocation_evidence_publish_and_request_close/,/^}/{s/ranges_overlap(evidence, sizeof(\*evidence), token, sizeof(\*token))/false/}'
mutation strong-context-overlap \
	'/smm_invocation_evidence_publish_and_request_close/,/^}/{s/(ops_snapshot.context_size \&\&/(false \&\& ops_snapshot.context_size \&\&/}'
mutation strong-geometry \
	'/static bool claimed_geometry_valid/,/^}/{s/^\([[:space:]]*\)struct smm_invocation_token expected;/\1if (1) return true;\
&/;}'
mutation strong-ack-snapshot \
	'/static bool claimed_geometry_valid/,/^}/{s/ack_control != expected_ack_required/false/}'
mutation strong-close-request \
	'/smm_invocation_evidence_publish_and_request_close/,/^}/{s/ | INVOCATION_CLOSE_REQUESTED |/ |/}'
mutation strong-final-cas \
	'/TEST_HOOK(48);/,/^}/{s/if (!__atomic_compare_exchange_n(\&evidence->state, \&state,/if ((state = state, false) \&\& !__atomic_compare_exchange_n(\&evidence->state, \&state,/}'
mutation strong-post-write-guard \
	'/ops_snapshot.write_rax/,/ops_snapshot.read_rax/{s/!claimed_geometry_valid/(false \&\& !claimed_geometry_valid/; s/ack_required))/ack_required)))/}'
mutation strong-final-geometry \
	'/TEST_HOOK(48);/,/TEST_HOOK(53);/{s/!claimed_geometry_valid/(false \&\& !claimed_geometry_valid/; s/ack_required))/ack_required)))/}'

if rg -n '__builtin_trap|(^|[^[:alnum:]_])abort[[:space:]]*\(|\bhlt\b' \
	"$root/src/cpu/x86/smm_invocation_entry.c" \
	"$root/src/cpu/x86/smm_invocation_evidence.c"; then
	printf '%s\n' 'SMM invocation production code gained a local fail-stop' >&2
	exit 1
fi

grep -q '^config SMM_INVOCATION_EVIDENCE$' "$root/src/cpu/x86/Kconfig"
grep -q '^smm-$(CONFIG_SMM_INVOCATION_EVIDENCE) += smm_invocation_evidence.c$' \
	"$root/src/cpu/x86/Makefile.mk"
if rg -q 'select[[:space:]]+SMM_INVOCATION_EVIDENCE' "$root/src"; then
	printf '%s\n' 'SMM invocation evidence became selected' >&2
	exit 1
fi
if rg -q 'smm_invocation_evidence_(provision|arrive|claim|publish|complete|abort|depart|shutdown|publish_and_request_close)' \
	"$root/src" -g '!src/cpu/x86/smm_invocation_evidence.c' \
	-g '!src/cpu/x86/smm_invocation_evidence_loader.c' \
	-g '!src/cpu/x86/smm_invocation_loader_composition.c' \
	-g '!src/cpu/x86/smm_invocation_entry.c' \
	-g '!src/include/cpu/x86/smm_invocation_evidence.h'; then
	printf '%s\n' 'dormant invocation evidence gained a production callsite' >&2
	exit 1
fi
if rg -q 'CONFIG_MAX_CPUS|boot_cpu\(|mp_run_on_all_cpus|apmc_node\(' \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	"$root/src/include/cpu/x86/smm_invocation_evidence.h"; then
	printf '%s\n' 'forbidden CPU or first-match evidence dependency' >&2
	exit 1
fi
git -C "$root" diff --check
patch="$temporary/checkpatch.patch"
{
	git -C "$root" diff --binary HEAD --
	git -C "$root" ls-files --others --exclude-standard | sort | \
	while read -r file; do
		git -C "$root" diff --no-index -- /dev/null "$file" || true
	done
} > "$patch"
if [ ! -s "$patch" ]; then
	git -C "$root" show --format= --binary HEAD -- > "$patch"
fi
"$root/util/lint/checkpatch.pl" --no-tree --show-types "$patch" \
	> "$temporary/checkpatch.log" 2>&1 || true
grep -q '^total: 0 errors, 0 warnings, ' "$temporary/checkpatch.log"
if grep -Eq '^(ERROR|WARNING):' "$temporary/checkpatch.log"; then
	cat "$temporary/checkpatch.log" >&2
	exit 1
fi

printf '%s\n' 'SMM invocation evidence tests: PASS'
