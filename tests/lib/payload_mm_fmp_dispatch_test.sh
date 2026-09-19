#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_CAPSULE_BROKER_CONTRACT 1' > \
	"$temporary/include/config.h"

cases='success write intent-check intent-set intent-direct intent-snapshot intent-busy intent-replay intent-max intent-distinct intent-after-close intent-current intent-generation intent-operation intent-revision intent-size intent-flags intent-zero-transaction intent-zero-image intent-digest intent-digest-size intent-reserved intent-message-short intent-message-large intent-adjacent-before intent-adjacent-after intent-overlap-before intent-overlap-after adjacent-before adjacent-after close max-transaction request-before request-misaligned request-generation
request-flags message-before message-misaligned message-short message-large
message-request-alias message-request-overlap-before message-request-overlap-after
message-revision current-null-size current-no-size
current-workspace-alias current-misaligned current-noncanonical current-outside install-no-state install-null
install-misaligned install-outside install-no-proof install-unprotected
install-workspace-unprotected install-proof-mutation install-authority-mutation'

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
		"$root/tests/lib/payload_mm_fmp_dispatch_test.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_fmp_state.c" \
		"$root/src/lib/payload_mm_fmp_dispatch.c" -o "$temporary/$name"
	for test_case in $cases; do
		"$temporary/$name" "$test_case"
	done
}

run_test ordinary
run_test optimized -O2
run_test strict -O2 -Wconversion -Wsign-conversion
run_test sanitized -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
printf '%s\n' 'Payload-MM FMP dispatch O0/O2/strict/ASan+UBSan hostile cases: PASS'
