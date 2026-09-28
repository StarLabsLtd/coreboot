#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)"
temporary="$(mktemp -d)"
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
source="$root/src/soc/intel/common/block/smm/invocation_adapter_route.c"
test_source="$root/tests/cpu/x86/smm_invocation_adapter_route_test.c"
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION 1' \
	'#define CONFIG_SMM_INVOCATION_INTEL_ADAPTER_PROVIDER 1' \
	> "$temporary/include/config.h"
includes="-include $root/src/include/kconfig.h -include $root/src/include/rules.h
-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
-I$temporary/include -I$root/src/include -I$root/src/commonlib/include
-I$root/src/commonlib/bsd/include -I$root/src/arch/x86/include -I$root/src"
defines="-D__TEST__ -D__COREBOOT__"
warnings="-Wall -Wextra -Werror -Wshadow -Wundef -Wconversion -Wno-sign-conversion -Wvla"

build()
{
	name="$1"
	product="$2"
	shift 2
	cc -std=gnu11 $defines $includes $warnings "$@" \
		"$test_source" "$product" -o "$temporary/$name"
	return 0
}

build_and_run()
{
	name="$1"
	product="$2"
	shift 2
	build "$name" "$product" "$@"
	"$temporary/$name"
}

build_and_run o0 "$source" -O0
build_and_run o2 "$source" -O2
build_and_run sanitize "$source" -O1 -g \
	-fsanitize=address,undefined -fno-omit-frame-pointer

# The production adapter remains freestanding ILP32 and introduces no atomic
# runtime dependency of its own.
# Deliberate normal flag splitting for this strict host harness.
# shellcheck disable=SC2086
cc -std=gnu11 $defines $includes $warnings -m32 -ffreestanding -fno-pic \
	-c "$source" -o "$temporary/route-i686.o"
! nm -u "$temporary/route-i686.o" | grep -Eq '__atomic_|__sync_'

mutant="$temporary/mutant.c"
mutant_must_die()
{
	name="$1"
	if cmp -s "$mutant" "$source"; then
		echo "unchanged route mutant: $name" >&2
		exit 1
	fi
	build "$name" "$mutant" -O2
	if "$temporary/$name" >/dev/null 2>&1; then
		echo "surviving route mutant: $name" >&2
		exit 1
	fi
}

sed 's/if (result != SMM_INVOCATION_TRY_SUCCESS)/if (false)/' \
	"$source" > "$mutant"
mutant_must_die ignored-provider-result

sed 's/topology, ops, authority_policy/topology, NULL, authority_policy/' \
	"$source" > "$mutant"
mutant_must_die substituted-ops

sed 's/) != CB_SUCCESS/) == CB_SUCCESS/' "$source" > "$mutant"
mutant_must_die inverted-route-result

sed 's/return SMM_INVOCATION_TRY_ERROR;/return SMM_INVOCATION_TRY_SUCCESS;/' \
	"$source" > "$mutant"
mutant_must_die ignored-route-error

sed 's/return result;/return SMM_INVOCATION_TRY_ERROR;/' "$source" > "$mutant"
mutant_must_die lost-provider-retry

for argument in session arm slot composition instance evidence topology \
	authority-policy binding verifier receipt proof proof-context; do
	case "$argument" in
	session) expression='s/route_session_provision(session, arm/route_session_provision(session + 1, arm/' ;;
	arm) expression='s/session, arm, slot/session, arm + 1, slot/' ;;
	slot) expression='s/arm, slot,/arm, slot + 1,/' ;;
	composition) expression='s/composition, instance/composition + 1, instance/' ;;
	instance) expression='s/instance, evidence/instance + 1, evidence/' ;;
	evidence) expression='s/evidence, topology/evidence + 1, topology/' ;;
	topology) expression='s/topology, ops/topology + 1, ops/' ;;
	authority-policy) expression='s/ops, authority_policy/ops, authority_policy + 1/' ;;
	binding) expression='s/binding, page_verifier/binding + 1, page_verifier/' ;;
	verifier) expression='s/page_verifier, page_receipt/page_verifier + 1, page_receipt/' ;;
	receipt) expression='s/page_receipt, protected_storage/page_receipt + 1, protected_storage/' ;;
	proof) expression='s/page_receipt, protected_storage/page_receipt, protected_storage ? NULL : protected_storage/' ;;
	proof-context) expression='s/protected_storage_context) !=/protected_storage_context ? NULL : protected_storage_context) !=/' ;;
	esac
	sed "$expression" "$source" > "$mutant"
	mutant_must_die "forward-$argument"
done

sed 's/topology, ops, authority_policy/topology, \&(struct smm_invocation_save_state_ops){ 0 }, authority_policy/' \
	"$source" > "$mutant"
mutant_must_die rebuilt-byte-equal-ops

! grep -Eq '__builtin_alloca|alloca[[:space:]]*\(|\[[^]]*[^]0-9 ][^]]*\]' \
	"$source"

echo "SMM invocation adapter route tests: PASS"
