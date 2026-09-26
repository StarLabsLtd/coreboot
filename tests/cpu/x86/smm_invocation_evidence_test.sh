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
	'#define CONFIG_SMM_INVOCATION_EVIDENCE 1' \
	> "$temporary/include/config.h"

build()
{
	name=$1
	flags=$2
	source=${3:-$root/src/cpu/x86/smm_invocation_evidence.c}
	# Deliberate normal flag splitting for this strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-pthread $flags -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/cpu/x86/smm_invocation_evidence_test.c" "$source" \
		-o "$temporary/$name"
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

${CC:-cc} -m32 -march=i686 -std=gnu11 -Wall -Wextra -Werror \
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

mutation()
{
	name=$1
	expression=$2
	mutant="$temporary/$name.c"
	sed "$expression" "$root/src/cpu/x86/smm_invocation_evidence.c" > "$mutant"
	for optimization in 0 2; do
		build "$name-O$optimization" "-O$optimization" "$mutant"
		if "$temporary/$name-O$optimization" >/dev/null 2>&1; then
			printf 'surviving mutant: %s O%s\n' "$name" "$optimization" >&2
			exit 1
		fi
	done
}

mutation exact-one 's/matches != 1U/matches == 0U/'
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
	'0,/callback_reentered(evidence) ||/{s//false ||/}'
mutation topology-proof \
	'0,/proof\[0\] = mix64(proof\[0\] \^ participant);/{s//proof[0] = mix64(proof[0]);/}'
mutation shutdown-boundary \
	'0,/__atomic_load_n(\&evidence->shutdown_requested, __ATOMIC_ACQUIRE)/{s//false/}'
mutation arrival-ownership \
	'0,/!phase_claim(evidence, SMM_INVOCATION_READY,/{s//false \&\& !phase_claim(evidence, SMM_INVOCATION_READY,/}'
mutation claim-publication-cas \
	'0,/!phase_claim(evidence, SMM_INVOCATION_CLAIMING,/{s//false \&\& !phase_claim(evidence, SMM_INVOCATION_CLAIMING,/}'
mutation depart-quiescence \
	'/static void close_finish_if_quiescent/,/^}/{s/__atomic_load_n(\&evidence->departure_writers, __ATOMIC_ACQUIRE)/0/}'
mutation restore-binding 's/return unchanged;/return true;/'
mutation arrival-admission \
	'0,/!phase_claim(evidence, SMM_INVOCATION_COLLECTING,/{s//false \&\& !phase_claim(evidence, SMM_INVOCATION_COLLECTING,/}'
mutation departure-admission \
	'0,/!phase_claim(evidence, SMM_INVOCATION_CLOSING,/{s//false \&\& !phase_claim(evidence, SMM_INVOCATION_CLOSING,/}'
mutation invalid-participant-poison \
	'0,/(void)poison_owned(evidence, SMM_INVOCATION_COLLECTING);/{s//(void)0;/}'
mutation arrival-failure-latch \
	'0,/__atomic_load_n(\&evidence->arrival_failed, __ATOMIC_ACQUIRE)/{s//false/}'
mutation arrival-post-claim-reload \
	's/TEST_HOOK(8);/TEST_HOOK(8); if (phase_load(evidence) != SMM_INVOCATION_COLLECTING) return CB_ERR;/'

grep -q '^config SMM_INVOCATION_EVIDENCE$' "$root/src/cpu/x86/Kconfig"
grep -q '^smm-$(CONFIG_SMM_INVOCATION_EVIDENCE) += smm_invocation_evidence.c$' \
	"$root/src/cpu/x86/Makefile.mk"
if rg -q 'select[[:space:]]+SMM_INVOCATION_EVIDENCE' "$root/src"; then
	printf '%s\n' 'SMM invocation evidence became selected' >&2
	exit 1
fi
if rg -q 'smm_invocation_evidence_(provision|arrive|claim|publish|complete|abort|depart|shutdown)' \
	"$root/src" -g '!src/cpu/x86/smm_invocation_evidence.c' \
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
"$root/util/lint/checkpatch.pl" --no-tree --show-types "$patch" \
	> "$temporary/checkpatch.log" 2>&1 || true
grep -q '^total: 0 errors, 0 warnings, ' "$temporary/checkpatch.log"
if grep -Eq '^(ERROR|WARNING):' "$temporary/checkpatch.log"; then
	cat "$temporary/checkpatch.log" >&2
	exit 1
fi

printf '%s\n' 'SMM invocation evidence tests: PASS'
