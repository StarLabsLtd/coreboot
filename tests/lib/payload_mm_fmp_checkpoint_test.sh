#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

cases='success wide-version bind-authority-mutation bind-workspace-mutation
idempotent commit-failure commit-lie read-failure owner-readback-failure
checkpoint-readback-failure readback-corrupt bad-attributes bad-size bad-reserved
bad-reserved2 bad-sequence bad-present absent sequence-wrap bad-validity
wrong-generation zero-transaction low-version durable-lsv grant-failure
read-input-mutation commit-input-mutation replay-after-failure max-transaction
idempotent-sequence-max outstanding-grant install-no-owner
bound-success bound-sequence bound-record
install-zero-generation install-generation-mismatch install-null install-misaligned install-outside
install-no-proof install-authority-unprotected install-workspace-unprotected
install-authority-mutation-failure install-workspace-mutation-failure'
cases="$cases install-dispatch-not-ready install-broker-not-ready
install-dispatch-overlap install-broker-overlap"
cases="$cases install-checkpoint-full install-checkpoint-leading
install-checkpoint-trailing install-checkpoint-adjacent-before
install-checkpoint-adjacent-after"

run_test()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/payload_mm_fmp_checkpoint_test.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_fmp_state.c" \
		"$root/src/lib/payload_mm_fmp_owner.c" \
		"$root/src/lib/payload_mm_fmp_checkpoint.c" -o "$temporary/$name"
	for test_case in $cases; do
		"$temporary/$name" "$test_case"
	done
}

run_test ordinary
run_test optimized -O2
run_test sanitized -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
printf '%s\n' 'Payload-MM FMP checkpoint O0/O2/ASan+UBSan hostile cases: PASS'
