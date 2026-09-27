#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

build()
{
	output=$1
	flags=$2
	source=${3:-$root/src/mainboard/starlabs/starbook/variants/mtl/smm_invocation_loader_instance.c}
	# Deliberate normal flag splitting for the strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-pthread $flags -D__COREBOOT__ -D__TEST__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
		"$root/tests/lib/starbook_mtl_loader_instance_test.c" "$source" \
		-o "$temporary/$output"
}

for optimization in 0 2; do
	build "plain-O$optimization" "-O$optimization"
	"$temporary/plain-O$optimization"
	build "sanitize-O$optimization" \
		"-O$optimization -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/sanitize-O$optimization"
done

build tsan "-O1 -g -fsanitize=thread"
TSAN_OPTIONS=halt_on_error=1 "$temporary/tsan"

mutate()
{
	name=$1
	expression=$2
	mutant="$temporary/$name.c"

	sed "$expression" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/smm_invocation_loader_instance.c" \
		> "$mutant"
	if cmp -s "$mutant" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/smm_invocation_loader_instance.c"; then
		printf '%s\n' "mutant did not change source: $name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		build "$name-O$optimization" "-O$optimization" "$mutant"
		if "$temporary/$name-O$optimization" >/dev/null 2>&1; then
			printf '%s\n' "surviving mutant: $name O$optimization" >&2
			exit 1
		fi
	done
}

mutate claim-bypass \
	's/if (!__atomic_compare_exchange_n(&fanout->state, &expected,/if (false \&\& !__atomic_compare_exchange_n(\&fanout->state, \&expected,/'
mutate accept-zero-low 's/!loader_instance_nonce.low/false/'
mutate publish-terminal \
	's/STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTED, false,/STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTING, false,/'
mutate incomplete-scrub \
	's/index < sizeof(\*fanout)/index < offsetof(struct starbook_mtl_loader_instance_fanout, owner_identity)/'
mutate owner-fanout-unbound \
	's/owner->fanout_identity == (uint64_t)(uintptr_t)fanout/true/'
mutate owner-attempt-unbound \
	's/owner->attempt == fanout->owner_attempt/true/'
mutate owner-identity-unbound \
	's/fanout->owner_identity == (uint64_t)(uintptr_t)owner/true/'
mutate owner-range-unchecked \
	's/object_size <= range_size - (object_base - range_base)/true/'
mutate owner-seal-unchecked \
	'0,/!fanout_seal_matches_owner(fanout, owner)/s//false/; 0,/!fanout_seal_matches_owner(fanout, owner)/s//false/'
mutate current-base-unchecked \
	's/protected_base != owner->protected_base/false/'
mutate current-size-unchecked \
	's/protected_size != owner->protected_size/false/'
mutate current-limit-unchecked \
	's/protected_limit != owner->protected_limit/false/'
mutate abort-owner-unchecked \
	's/if (owned_claim_valid(fanout, owner) &&/if (true \&\&/'
mutate commit-cas-bypass \
	's/if (!__atomic_compare_exchange_n(&fanout->state, &expected,/if (false \&\& !__atomic_compare_exchange_n(\&fanout->state, \&expected,/'
mutate requiesce-failure-bypass \
	's/if (requiesce(&protected_limit) != CB_SUCCESS)/if (false)/'

if ! rg -q '__ATOMIC_RELEASE' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/smm_invocation_loader_instance.c" ||
   ! rg -q '__ATOMIC_ACQUIRE' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/smm_invocation_loader_instance.c"; then
	printf '%s\n' 'loader-instance authority lost release/acquire publication' >&2
	exit 1
fi

authority="$root/src/mainboard/starlabs/starbook/variants/mtl/loader_instance_authority.c"
source="$root/src/mainboard/starlabs/starbook/variants/mtl/loader_instance_source.c"
rg -q 'fanout_take_requiesced\(' "$authority"
rg -q 'starbook_mtl_loader_instance_source_ramstage_requiesce\);' "$authority"
rg -q 'struct starbook_mtl_loader_instance_owner owner;' "$authority"
rg -q '&authority_workspace.owner' "$authority"
! rg -q '^[[:space:]]*struct starbook_mtl_loader_instance_owner owner =' \
	"$authority"
rg -q 'snapshot->bus_count != CONFIG_ECAM_MMCONF_BUS_NUMBER' "$source"
rg -q 'quiesce_result = quiesce\(NULL\);' "$source"
rg -q '!quiesce_snapshot_valid\(&pci_snapshot\)' "$source"
test "$(rg -c 'starbook_mtl_loader_instance_source_ramstage_requiesce\(' \
	"$source")" -eq 1

printf '%s\n' 'StarBook MTL loader-instance authority tests: PASS'
