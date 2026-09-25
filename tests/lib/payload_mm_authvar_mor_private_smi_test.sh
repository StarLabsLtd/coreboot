#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION 1' \
	'#define CONFIG_BOOTMEM_ALIGNED_RESERVATIONS 1' \
	'#define CONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT 1' \
	'#define CONFIG_BOOTMEM_DRAM_PROVENANCE 0' \
	'#define CONFIG_MAX_CPUS 8' > "$temporary/include/config.h"
printf '%s\n' \
	'#include "bootmem_reservation_receipt_internal.h"' > \
	"$temporary/include/payload_mm_authvar_mor_private_smi_test_internal_source.h"

cases='success replay xapic-clobber x2apic-clobber close identity page cookie cpu other-cpu non-owner-race policy-mismatch generation capability receipt padding seal callback-mutation transaction-before-take-failure install-cut-1 install-cut-2 install-cut-3 install-cut-4 install-cut-5 install-cut-6 install-mutate-slot install-mutate-channel'

build_and_run()
{
	flags=$1
	receiver=${2:-$root/src/lib/payload_mm_authvar_mor_private_smi_receiver.c}
	"${CC:-cc}" -std=gnu11 $flags -Wall -Wextra -Werror \
		-Wshadow -Wstrict-prototypes -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-DBOOTMEM_RECEIPT_TEST \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/tests/lib" -I"$root/src/lib" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_mor_private_smi_test.c" \
		"$root/src/lib/payload_mm_authvar_mor_private_smi_sender.c" \
		"$receiver" \
		"$root/src/lib/payload_mm_authvar_mor_seal.c" \
		"$root/src/lib/payload_mm_authvar_mor_grant.c" \
		"$root/src/lib/bootmem_reservation_receipt.c" -o "$temporary/test" ||
		return 1
	for case_name in $cases; do
		ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/test" "$case_name" || return 1
	done
}

for flags in '-O0' '-O2' '-O1 -fsanitize=address' \
	'-O1 -fsanitize=undefined' '-O1 -fsanitize=thread'; do
	build_and_run "$flags"
done

mutant()
{
	name=$1
	expression=$2
	sed "$expression" \
		"$root/src/lib/payload_mm_authvar_mor_private_smi_receiver.c" > \
		"$temporary/$name.c"
	! cmp -s "$root/src/lib/payload_mm_authvar_mor_private_smi_receiver.c" \
		"$temporary/$name.c"
	if build_and_run '-O2' "$temporary/$name.c" >/dev/null 2>&1; then
		echo "ERROR: $name mutation survived" >&2
		exit 1
	fi
}

mutant padding 's/!zero((const uint8_t \*)request + sizeof(candidate),/false \&\& zero((const uint8_t *)request + sizeof(candidate),/'
mutant descriptor-cookie 's/cookie != policy.cookie/false/'
mutant final-slot-recheck 's/!slot_equal(slot, \&slot_snapshot)/false/g'
mutant terminal-slot-scrub 's/scrub(slot, sizeof(\*slot));/scrub(slot, sizeof(*slot)); slot->reserved[0] = 1;/'
mutant terminal-grant-discard '/payload_mm_authvar_mor_grant_discard/,+1c\
\t(void)payload_mm_authvar_mor_grant_close();'
