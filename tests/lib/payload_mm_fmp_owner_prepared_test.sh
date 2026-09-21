#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_CAPSULE_TPM_ANCHOR_GRANT 1' > \
	"$temporary/include/config.h"

cases='prepared-success prepared-before-advance prepared-stale-anchor
prepared-unrelated-torn
prepared-advanced-no-grant
prepared-mismatch-generation prepared-mismatch-transaction
prepared-mismatch-current prepared-mismatch-candidate
prepared-mismatch-epoch
prepared-cut-prepare-program-1 prepared-cut-prepare-program-2
prepared-cut-prepare-sync-1 prepared-cut-prepare-sync-2
prepared-cut-reconcile-program prepared-cut-reconcile-sync'

authorized_cases='prepared-authorized-success prepared-authorized-reentry
prepared-authorized-reconcile prepared-authorized-composite-next
prepared-authorized-gc-copy prepared-authorized-composite-duplicate
prepared-authorized-manifest-ambiguity
prepared-authorized-receipt-v1
prepared-authorized-capsule-size prepared-authorized-capsule-algorithm
prepared-authorized-capsule-digest prepared-authorized-generation
prepared-authorized-transaction prepared-authorized-current-anchor
prepared-authorized-slot-2512 prepared-authorized-slot-2504
prepared-authorized-slot-2511
prepared-authorized-capsule-too-large prepared-authorized-digest-mismatch
prepared-authorized-tail prepared-authorized-tuple prepared-authorized-alias
prepared-authorized-mutation prepared-authorized-hash-input
prepared-authorized-hash-context prepared-authorized-program-input
prepared-authorized-program-context
prepared-authorized-program-before-1 prepared-authorized-program-partial-1
prepared-authorized-program-after-1 prepared-authorized-program-before-2
prepared-authorized-program-partial-2 prepared-authorized-program-after-2
prepared-authorized-program-before-3 prepared-authorized-program-partial-3
prepared-authorized-program-after-3
prepared-authorized-sync-before-1 prepared-authorized-sync-after-1
prepared-authorized-sync-before-2 prepared-authorized-sync-after-2
prepared-authorized-sync-before-3 prepared-authorized-sync-after-3'

platform_cases='prepared-platform-success prepared-platform-slot-528
prepared-platform-slot-520 prepared-platform-receipt-v1
prepared-platform-capsule-size prepared-platform-capsule-algorithm
prepared-platform-capsule-digest prepared-platform-generation
prepared-platform-transaction prepared-platform-current
prepared-platform-candidate prepared-platform-reconcile prepared-platform-next
prepared-platform-gc-copy
prepared-platform-program-before-1 prepared-platform-program-partial-1
prepared-platform-program-after-1 prepared-platform-program-before-2
prepared-platform-program-partial-2 prepared-platform-program-after-2
prepared-platform-program-before-3 prepared-platform-program-partial-3
prepared-platform-program-after-3 prepared-platform-sync-before-1
prepared-platform-sync-after-1 prepared-platform-sync-before-2
prepared-platform-sync-after-2 prepared-platform-sync-before-3
prepared-platform-sync-after-3'

build_and_run()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin \
		-ffunction-sections -fdata-sections -Wl,--gc-sections "$@" \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/3rdparty/vboot/firmware/include" \
		-I"$root/3rdparty/vboot/firmware/2lib/include" \
		-I"$temporary/include" \
		"$root/tests/lib/payload_mm_fmp_owner_journal_test.c" \
		"$root/src/security/tpm/capsule_anchor_authorization.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_fmp_state.c" \
		"$root/src/lib/payload_mm_fmp_owner.c" \
		"$root/src/lib/payload_mm_fmp_owner_layout.c" \
		"$root/src/lib/payload_mm_fmp_owner_journal_format.c" \
		"$root/src/lib/payload_mm_fmp_owner_journal.c" \
		"$root/src/security/tpm/capsule_anchor_grant.c" \
		"$root/src/security/tpm/capsule_anchor.c" \
		"$root/src/security/tpm/capsule_anchor_platform.c" \
		"$root/src/lib/payload_mm_fmp_checkpoint.c" \
		-o "$temporary/$name"
	for case_name in $cases; do
		"$temporary/$name" "$case_name"
	done
	for case_name in $authorized_cases; do
		"$temporary/$name" "$case_name"
	done
	for case_name in $platform_cases; do
		"$temporary/$name" "$case_name"
	done
	for cut in 1 2 3 4 5 6; do
		for kind in before partial after; do
			"$temporary/$name" \
				"prepared-authorized-gc-cut-program-$kind-$cut"
		done
	done
	for cut in 1 2 3 4 5 6 7 8; do
		for kind in before after; do
			"$temporary/$name" \
				"prepared-authorized-gc-cut-sync-$kind-$cut"
		done
	done
}

build_and_run prepared-o0 -O0
build_and_run prepared-o2 -O2
build_and_run prepared-asan -O1 -fsanitize=address -fno-omit-frame-pointer
build_and_run prepared-ubsan -O1 -fsanitize=undefined -fno-omit-frame-pointer
