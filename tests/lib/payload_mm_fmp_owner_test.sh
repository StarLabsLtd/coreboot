#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

cases='ready-before-install seed-success seed-null seed-mismatch seed-identity-mutation
seed-context-mutation seed-backend-mutation seed-control-mutation seed-read-reentry
seed-protect-reentry
sealed-backend-active sealed-backend-copy sealed-control-active sealed-control-copy
sealed-identity-active sealed-identity-copy identity-source-closure
read read-reentry read-context-mutation read-backend-mutation read-control-mutation
read-absent read-legacy source-mutation authority-mutation authority-ranges read-outside read-key read-misaligned
read-failure bad-sequence bad-present bad-attributes bad-size bad-reserved
bad-reserved2 bad-validity bad-absent bad-legacy-tail success
sequence-only
commit-error-candidate commit-error-current commit-success-current
commit-success-other commit-error-other commit-success-torn commit-error-torn
readback-failure same-sequence-distinct commit-identity-mutation
commit-input-mutation readback-identity-mutation reentry state-regression
commit-context-mutation commit-backend-mutation commit-control-mutation
lsv-regression trusted-floor validity-regressions sequence-gap sequence-wrap outside misaligned alias remove remove-state
remove-absent remove-wrap install-no-state install-revision install-size
install-read install-commit install-context-null install-context-zero
install-context-large install-protection install-authority-failure'

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
		"$root/tests/lib/payload_mm_fmp_owner_test.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_fmp_state.c" \
		"$root/src/lib/payload_mm_fmp_owner.c" -o "$temporary/$name"
	for test_case in $cases; do
		"$temporary/$name" "$test_case"
	done
}

run_test ordinary
run_test optimized -O2
run_test sanitized -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
printf '%s\n' 'Payload-MM FMP owner O0/O2/ASan+UBSan hostile cases: PASS'
