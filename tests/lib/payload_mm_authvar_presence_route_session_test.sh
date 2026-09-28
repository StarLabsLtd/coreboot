#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=${ROUTE_TEST_TEMPORARY:-$(mktemp -d)}
if [ -z "${ROUTE_TEST_TEMPORARY:-}" ]; then
	trap 'rm -rf "$temporary"' EXIT HUP INT TERM
fi
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ARM 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION 1' \
	'#define CONFIG_SMM_APMC_ROUTE_AUTHVAR_PRESENCE 1' \
	> "$temporary/include/config.h"

flags='-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin -pthread'
includes="-include $root/src/include/kconfig.h -include $root/src/include/rules.h
-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
-I$temporary/include -I$root/src -I$root/src/include -I$root/src/lib
-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
-I$root/src/arch/x86/include"

build_and_run()
{
	name=$1
	extra=$2
	# Deliberate host harness flag splitting.
	# shellcheck disable=SC2086
	${CC:-cc} $flags -Wno-conversion $extra -D__TEST__ -D__COREBOOT__ $includes \
		-c "$root/src/lib/bootmem_reservation_receipt.c" \
		-o "$temporary/receipt-$name.o"
	# Deliberate host harness flag splitting.
	# shellcheck disable=SC2086
	${CC:-cc} $flags $extra -D__TEST__ -D__COREBOOT__ $includes \
		"$root/tests/lib/payload_mm_authvar_presence_route_session_test.c" \
		"$root/src/lib/payload_mm_authvar_presence_route_session.c" \
		"$root/src/lib/payload_mm_authvar_presence_arm.c" \
		"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
		"$root/src/lib/payload_mm_authvar_presence_transaction_receiver.c" \
		"$temporary/receipt-$name.o" \
		-o "$temporary/$name"
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1 "$temporary/$name"
}

build_and_run strict-O0 '-O0'
build_and_run strict-O2 '-O2'
build_and_run sanitized \
	'-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all'
build_and_run thread-sanitized \
	'-O1 -g -fno-omit-frame-pointer -fsanitize=thread -fno-sanitize-recover=all -Wno-error=tsan'

build_integration_and_run()
{
	name=$1
	extra=$2
	# Deliberate host harness flag splitting.
	# shellcheck disable=SC2086
	${CC:-cc} $flags -Wno-conversion -Wno-unused-parameter $extra \
		-D__SMM__ -D__COREBOOT__ $includes \
		-c "$root/src/lib/bootmem_reservation_receipt.c" \
		-o "$temporary/integration-receipt-$name.o"
	# Deliberate host harness flag splitting.
	# shellcheck disable=SC2086
	${CC:-cc} $flags -Wno-unused-parameter $extra -D__SMM__ -D__COREBOOT__ \
		$includes \
		"$root/tests/lib/payload_mm_authvar_presence_route_integration_test.c" \
		"$root/src/lib/payload_mm_authvar_presence_route_session.c" \
		"$root/src/lib/payload_mm_authvar_presence_arm.c" \
		"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
		"$root/src/lib/payload_mm_authvar_presence_transaction_receiver.c" \
		"$root/src/lib/payload_mm_authvar_presence.c" \
		"$temporary/integration-receipt-$name.o" \
		"$root/src/cpu/x86/smm_command.c" \
		"$root/src/cpu/x86/smm_invocation_entry.c" \
		"$root/src/cpu/x86/smm_invocation_evidence.c" \
		"$root/src/cpu/x86/smm_invocation_evidence_loader.c" \
		"$root/src/cpu/x86/smm_invocation_loader_composition.c" \
		"$root/src/cpu/x86/smm_invocation_loader_composition_gate.c" \
		"$root/src/cpu/x86/smm_invocation_loader_instance.c" \
		"$root/src/cpu/x86/smm_invocation_topology.c" \
		-o "$temporary/integration-$name"
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1 "$temporary/integration-$name"
}

build_integration_and_run strict-O0 '-O0 -g'
build_integration_and_run strict-O2 '-O2 -g'
build_integration_and_run sanitized \
	'-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all'
build_integration_and_run thread-sanitized \
	'-O1 -g -fno-omit-frame-pointer -fsanitize=thread -fno-sanitize-recover=all -Wno-error=tsan'

# Deliberate host harness flag splitting.
# shellcheck disable=SC2086
${CC:-cc} $flags -Wno-unused-parameter -Os -m32 -ffreestanding \
	-fstack-usage -fcallgraph-info=su \
	-D__SMM__ -D__COREBOOT__ $includes \
	-c "$root/src/lib/payload_mm_authvar_presence_route_session.c" \
	-o "$temporary/route-32.o"
for source in \
	"$root/src/lib/payload_mm_authvar_presence_arm.c" \
	"$root/src/lib/payload_mm_authvar_presence_transaction_receiver.c" \
	"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
	"$root/src/lib/payload_mm_authvar_presence.c" \
	"$root/src/lib/bootmem_reservation_receipt.c" \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	"$root/src/cpu/x86/smm_invocation_entry.c" \
	"$root/src/cpu/x86/smm_invocation_loader_composition.c" \
	"$root/src/cpu/x86/smm_invocation_loader_composition_gate.c" \
	"$root/src/cpu/x86/smm_invocation_loader_instance.c" \
	"$root/src/cpu/x86/smm_invocation_topology.c" \
	"$root/src/cpu/x86/smm_command.c" \
	"$root/src/lib/memcmp.c" \
	"$root/src/arch/x86/memcpy.c" \
	"$root/src/arch/x86/memset.c"; do
	object=$(basename "$source" .c)
	# Deliberate host harness flag splitting.
	# shellcheck disable=SC2086
	${CC:-cc} $flags -Wno-conversion -Wno-unused-parameter -Os -m32 \
		-ffreestanding -fstack-usage \
		-fcallgraph-info=su \
		-D__SMM__ -D__COREBOOT__ $includes -c "$source" \
		-o "$temporary/$object-32.o"
done
if nm -u "$temporary/route-32.o" | grep -Eq '__atomic|libatomic'; then
	printf '%s\n' 'presence route session gained a libatomic dependency' >&2
	exit 1
fi
ld -m elf_i386 -r "$temporary"/*-32.o \
	-o "$temporary/route-integration-32.o"
if nm -u "$temporary/route-integration-32.o" | grep -Eq '__atomic|libatomic'; then
	printf '%s\n' '32-bit presence route composition gained libatomic' >&2
	exit 1
fi
route_stack=$(awk -F '\t' '
	$1 ~ /route_(claim|complete)$/ ||
	$1 ~ /route_session_dispatch_locked$/ { if ($2 > max) max = $2 }
	END { print max + 0 }
' "$temporary/route-32.su")
test "$route_stack" -le 4096

source_contract="$root/tests/lib/payload_mm_authvar_presence_route_source_contract.awk"
route_source="$root/src/lib/payload_mm_authvar_presence_route_session.c"
arm_source="$root/src/lib/payload_mm_authvar_presence_arm.c"
awk -f "$source_contract" "$route_source" "$arm_source"
sed 's/\.prepare = route_prepare,/.prepare = route_abort, \/\* .prepare = route_prepare, *\//' \
	"$route_source" > "$temporary/route-target-decoy.c"
! awk -f "$source_contract" "$temporary/route-target-decoy.c" \
	"$arm_source" >/dev/null 2>&1
sed 's/\.commit = route_commit,/.commit = route_commit, .commit = route_commit,/' \
	"$route_source" > "$temporary/route-duplicate.c"
! awk -f "$source_contract" "$temporary/route-duplicate.c" \
	"$arm_source" >/dev/null 2>&1
sed 's/smm_apmc_command_consume(/smm_apmc_command_consume(smm_apmc_command_consume(/' \
	"$route_source" > "$temporary/route-consume-duplicate.c"
! awk -f "$source_contract" "$temporary/route-consume-duplicate.c" \
	"$arm_source" >/dev/null 2>&1
sed '/status = payload_mm_authvar_presence_transaction_dispatch/i\
\t(void)receipt;' "$route_source" > "$temporary/route-adjacency.c"
! awk -f "$source_contract" "$temporary/route-adjacency.c" \
	"$arm_source" >/dev/null 2>&1
sed 's/\.prepare = wrapped_prepare,/.prepare = wrapped_abort, \/\* .prepare = wrapped_prepare, *\//' \
	"$arm_source" > "$temporary/arm-target-decoy.c"
! awk -f "$source_contract" "$route_source" \
	"$temporary/arm-target-decoy.c" >/dev/null 2>&1
! grep -q 'smm_apmc_command_finish' "$route_source"

build_context_mutant_and_require_failure()
{
	name=$1
	mutant=$2
	# Deliberate host harness flag splitting.
	# shellcheck disable=SC2086
	${CC:-cc} $flags -O0 -D__TEST__ -D__COREBOOT__ $includes \
		"$root/tests/lib/payload_mm_authvar_presence_route_session_test.c" \
		"$mutant" \
		"$root/src/lib/payload_mm_authvar_presence_arm.c" \
		"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
		"$root/src/lib/payload_mm_authvar_presence_transaction_receiver.c" \
		"$temporary/receipt-strict-O0.o" \
		-o "$temporary/$name"
	! "$temporary/$name" >/dev/null 2>&1
}

sed '/return session->authority_policy.context_size ?/,/session->authority_context : NULL;/c\
\t(void)session;\
\treturn NULL;' "$route_source" > "$temporary/route-authority-null.c"
! awk -f "$source_contract" "$temporary/route-authority-null.c" \
	"$arm_source" >/dev/null 2>&1
build_context_mutant_and_require_failure route-authority-null \
	"$temporary/route-authority-null.c"

sed '/return session->authority_policy.context_size ?/,/session->authority_context : NULL;/c\
\treturn session->failure_context;' "$route_source" \
	> "$temporary/route-authority-wrong.c"
! awk -f "$source_contract" "$temporary/route-authority-wrong.c" \
	"$arm_source" >/dev/null 2>&1
build_context_mutant_and_require_failure route-authority-wrong \
	"$temporary/route-authority-wrong.c"

sed 's/protected_storage, protected_storage_context,/protected_storage, NULL,/' \
	"$route_source" > "$temporary/route-proof-null.c"
! awk -f "$source_contract" "$temporary/route-proof-null.c" \
	"$arm_source" >/dev/null 2>&1
build_context_mutant_and_require_failure route-proof-null \
	"$temporary/route-proof-null.c"

sed 's/protected_storage, protected_storage_context,/protected_storage, session,/' \
	"$route_source" > "$temporary/route-proof-wrong.c"
! awk -f "$source_contract" "$temporary/route-proof-wrong.c" \
	"$arm_source" >/dev/null 2>&1
build_context_mutant_and_require_failure route-proof-wrong \
	"$temporary/route-proof-wrong.c"

sed 's/memcmp(policy_snapshot.context, authority_context_snapshot,/memcmp(authority_context_snapshot, authority_context_snapshot,/g' \
	"$route_source" > "$temporary/route-context-recheck-mutant.c"
# Deliberate host harness flag splitting.
# shellcheck disable=SC2086
${CC:-cc} $flags -O0 -D__TEST__ -D__COREBOOT__ $includes \
	"$root/tests/lib/payload_mm_authvar_presence_route_session_test.c" \
	"$temporary/route-context-recheck-mutant.c" \
	"$root/src/lib/payload_mm_authvar_presence_arm.c" \
	"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
	"$root/src/lib/payload_mm_authvar_presence_transaction_receiver.c" \
	"$temporary/receipt-strict-O0.o" \
	-o "$temporary/route-context-recheck-mutant"
! "$temporary/route-context-recheck-mutant" >/dev/null 2>&1

kconfig_dormant_contract()
{
	symbol=$1
	file=$2
	expected_dependencies=$3
	awk -v symbol="$symbol" -v expected="$expected_dependencies" '
		$1 == "config" || $1 == "menuconfig" {
			if ($2 == symbol) {
				declarations++
				active = 1
				next
			}
			active = 0
		}
		!active { next }
		$1 == "bool" {
			bools++
			if (NF != 1)
				bad = 1
			next
		}
		$1 == "default" {
			defaults++
			if (NF != 2 || $2 != "n")
				bad = 1
			next
		}
		$1 == "depends" && $2 == "on" {
			dependency = $0
			sub(/^[[:space:]]*depends[[:space:]]+on[[:space:]]+/, "",
				dependency)
			gsub(/[[:space:]]+/, " ", dependency)
			dependencies = dependencies (dependencies == "" ? "" : ";") \
				dependency
		}
		END {
			exit declarations != 1 || bools != 1 || defaults != 1 || bad ||
				dependencies != expected
		}
	' "$file"
}

reject_route_kconfig_enablers()
{
	! rg -q '^[[:space:]]*(select|imply)[[:space:]]+(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION|SMM_APMC_ROUTE_AUTHVAR_PRESENCE)([[:space:]]|$)' \
		"$@"
}

lib_kconfig="$root/src/lib/Kconfig"
cpu_kconfig="$root/src/cpu/x86/Kconfig"
route_dependencies='PAYLOAD_MM_AUTHVAR_PRESENCE_ARM;SMM_APMC_COMMAND_REGISTRY;SMM_INVOCATION_ENTRY;SMM_INVOCATION_TOPOLOGY;SMM_MODULE_STACK_SIZE >= 0x4000'
kconfig_dormant_contract PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION \
	"$lib_kconfig" "$route_dependencies"
kconfig_dormant_contract SMM_APMC_ROUTE_AUTHVAR_PRESENCE \
	"$cpu_kconfig" 'PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION'
reject_route_kconfig_enablers "$root/src"

sed '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION$/,/^config / {
	/default n/a\
\tdefault y if ARCH_X86
}' "$lib_kconfig" > "$temporary/lib-conditional-default.Kconfig"
! kconfig_dormant_contract PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION \
	"$temporary/lib-conditional-default.Kconfig" "$route_dependencies"
sed '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION$/,/^config / {
	s/^\tbool$/\tbool "unsafe route"/
}' "$lib_kconfig" > "$temporary/lib-prompt.Kconfig"
! kconfig_dormant_contract PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION \
	"$temporary/lib-prompt.Kconfig" "$route_dependencies"
sed '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION$/,/^config / {
	s/^\tdefault n$/\tdefault y/
}' "$lib_kconfig" > "$temporary/lib-default-y.Kconfig"
! kconfig_dormant_contract PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION \
	"$temporary/lib-default-y.Kconfig" "$route_dependencies"
cp "$cpu_kconfig" "$temporary/cpu-select.Kconfig"
printf '\t%s\n' 'select PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION' \
	>> "$temporary/cpu-select.Kconfig"
! reject_route_kconfig_enablers "$temporary/cpu-select.Kconfig"
cp "$cpu_kconfig" "$temporary/cpu-imply.Kconfig"
printf '\t%s\n' 'imply SMM_APMC_ROUTE_AUTHVAR_PRESENCE' \
	>> "$temporary/cpu-imply.Kconfig"
! reject_route_kconfig_enablers "$temporary/cpu-imply.Kconfig"
