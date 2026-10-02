#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/mm-state-predicate.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_STATE_PREDICATE_PINNED 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_AUTHORITY_PROVIDER 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_REQUIRE_SELF_SIGNED_PK 0' \
	> "$temporary/include/config.h"
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ulimit -c 0
for optimization in 0 2; do
	compile_test()
	{
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wshadow -Wstrict-prototypes -fno-builtin -fno-pie -no-pie \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-D__TEST__ -D__COREBOOT__ -DEXECUTOR_SERVICE_TRANSACTION \
		-DSTATE_PREDICATE_EXECUTOR_SOURCE=\""$1"\" \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_state_predicate_test.c" \
		"$root/src/lib/payload_mm_authvar_service.c" \
		"$root/src/lib/payload_mm_authvar_service_transaction.c" \
		"$root/src/lib/payload_mm_authvar_coordinator.c" \
		"$root/src/lib/payload_mm_authvar_authority_provider.c" \
		"$root/src/lib/payload_mm_authvar_set_preflight.c" \
		"$root/src/lib/payload_mm_authvar_controlled_mode.c" \
		"$root/src/lib/payload_mm_authvar_view.c" \
		"$root/src/lib/payload_mm_authvar_authority.c" \
		"$root/src/lib/payload_mm_authvar_candidate.c" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
		"$root/src/lib/payload_mm_authvar_mode.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_authvar_route.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" -o "$2"
	}
	compile_test "$root/src/lib/payload_mm_authvar_executor.c" "$temporary/test"
	"$temporary/test" lifecycle
	"$temporary/test" reserved
	"$temporary/test" end-data
	"$temporary/test" end-name
	"$temporary/test" pool
	"$temporary/test" next
	"$temporary/test" persistent-live
	"$temporary/test" persistent-deleted
	for mutation in data-digest name-digest query-free; do
		case "$mutation" in
		data-digest) needle='memcmp(data_digest, volatile_data_digest, sizeof(data_digest))'; scenario=end-data ;;
		name-digest) needle='memcmp(name_digest, volatile_name_digest, sizeof(name_digest))'; scenario=end-name ;;
		query-free) needle='VARIABLE_PREDICATE_DATA_CAPACITY - variable_policies.data_used),'; scenario=pool ;;
		esac
		awk -v needle="$needle" -v mutation="$mutation" '
			index($0, needle) {
				count++;
				position = index($0, needle);
				replacement = mutation == "query-free" ? "VARIABLE_PREDICATE_DATA_CAPACITY)," : "false";
				$0 = substr($0, 1, position - 1) replacement substr($0, position + length(needle));
			}
			{ print }
			END { if (count != 1) exit 1 }
		' "$root/src/lib/payload_mm_authvar_executor.c" > "$temporary/executor.c"
		compile_test "$temporary/executor.c" "$temporary/mutant"
		status=0
		"$temporary/mutant" "$scenario" > "$temporary/mutant.log" 2>&1 || status=$?
		[ "$status" -eq 134 ]
		case "$mutation" in
		*-digest) grep -F 'assertion failed: write_value(predicate_guid' "$temporary/mutant.log" >/dev/null ;;
		query-free) grep -F 'assertion failed: result.maximum_storage' "$temporary/mutant.log" >/dev/null ;;
		esac
		if grep -E 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$temporary/mutant.log"; then
			exit 1
		fi
	done
done
