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
prepared-advanced-no-grant
prepared-mismatch-generation prepared-mismatch-transaction
prepared-mismatch-current prepared-mismatch-candidate
prepared-mismatch-epoch
prepared-cut-prepare-program-1 prepared-cut-prepare-program-2
prepared-cut-prepare-sync-1 prepared-cut-prepare-sync-2
prepared-cut-reconcile-program prepared-cut-reconcile-sync'

build_and_run()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/payload_mm_fmp_owner_journal_test.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_fmp_state.c" \
		"$root/src/lib/payload_mm_fmp_owner.c" \
		"$root/src/lib/payload_mm_fmp_owner_layout.c" \
		"$root/src/lib/payload_mm_fmp_owner_journal.c" \
		"$root/src/security/tpm/capsule_anchor_grant.c" \
		"$root/src/lib/payload_mm_fmp_checkpoint.c" \
		-o "$temporary/$name"
	for case_name in $cases; do
		"$temporary/$name" "$case_name"
	done
}

build_and_run prepared-o0 -O0
build_and_run prepared-o2 -O2
build_and_run prepared-asan -O1 -fsanitize=address -fno-omit-frame-pointer
build_and_run prepared-ubsan -O1 -fsanitize=undefined -fno-omit-frame-pointer
