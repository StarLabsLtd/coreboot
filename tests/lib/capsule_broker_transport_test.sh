#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

cases='happy execute-revision2 info-happy info-revision1 info-intent-size
info-result-size info-failure info-mutation info-authority-mutation info-close info-reentry
info-replay info-execute-independent buffer-low buffer-unmapped buffer-alternate request-revision request-size request-operation
request-flags intent-size-field result-size-field generation intent-generation
transaction intent-revision intent-size intent-operation intent-flags
intent-zero-size intent-large intent-algorithm intent-digest-size intent-reserved
not-ready failure mutation authority-mutation reentry replay maximum'

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
		"$root/tests/lib/capsule_broker_transport_test.c" \
		"$root/src/lib/capsule_broker_transport.c" -o "$temporary/$name"
	for test_case in $cases; do
		"$temporary/$name" "$test_case"
	done
}

# The public transport header has no address-shaped member.
if sed -n '/^struct capsule_broker_transport_request {/,/^} __aligned(8);/p' \
	"$root/src/include/boot/capsule_broker.h" | \
	grep -E 'address|pointer|uintptr|void[[:space:]]*\*'; then
	printf '%s\n' 'caller-controlled address remains in transport request' >&2
	exit 1
fi

run_test ordinary
run_test optimized -O2
run_test strict -O2 -Wconversion -Wsign-conversion
run_test sanitized -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
printf '%s\n' 'Capsule broker typed transport O0/O2/strict/ASan+UBSan: PASS'
