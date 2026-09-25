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

cases='optional provision-concurrent-terminal success replay'
cases="$cases tombstone-mismatch"
cases="$cases tombstone-publishing-dispatch installed-concurrent terminal-concurrent"
cases="$cases installed-barrier install-dispatch-barrier abort-dispatch-barrier"
cases="$cases xapic-clobber x2apic-clobber close identity page cookie cpu"
cases="$cases other-cpu non-owner-race policy-mismatch generation capability"
cases="$cases receipt padding seal callback-mutation"
cases="$cases transaction-before-take-failure install-cut-1 install-cut-2"
cases="$cases install-cut-3 install-cut-4 install-cut-5 install-cut-6"
cases="$cases install-mutate-slot install-mutate-channel bootstrap-success"
cases="$cases bootstrap-close bootstrap-replay bootstrap-after-install-dispatch"
cases="$cases bootstrap-failure bootstrap-failure-replay bootstrap-reentry"
cases="$cases bootstrap-concurrent bootstrap-identity bootstrap-other-cpu"
cases="$cases bootstrap-generation bootstrap-capability bootstrap-receipt"
cases="$cases bootstrap-padding bootstrap-callback-header bootstrap-callback-padding"
cases="$cases bootstrap-slot-cpus bootstrap-slot-cookie bootstrap-slot-reserved"

build_and_run()
{
	flags=$1
	receiver=${2:-$root/src/lib/payload_mm_authvar_mor_private_smi_receiver.c}
	"${CC:-cc}" -std=gnu11 $flags -pthread -Wall -Wextra -Werror \
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
mutant receiver-claim 's/if (!__atomic_compare_exchange_n(\&receiver.phase, \&expected,/if (false \&\& !__atomic_compare_exchange_n(\&receiver.phase, \&expected,/'
mutant tombstone-rewrite 's/return state == 1 \&\& identity ==/__atomic_store_n(\&receiver.tombstone_identity, identity, __ATOMIC_RELEASE); return state == 1 \&\& identity ==/'
mutant tombstone-nonzero-valid 's/__atomic_load_n(\&receiver.tombstone_valid, __ATOMIC_ACQUIRE) != 1/!__atomic_load_n(\&receiver.tombstone_valid, __ATOMIC_ACQUIRE)/g'
