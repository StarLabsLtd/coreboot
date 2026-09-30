#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_MAX_CPUS 4' '#define CONFIG_SMM_MODULE_STACK_SIZE 4096' \
	'#define CONFIG_SMM_INVOCATION_TOPOLOGY 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_COMPOSITION 1' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI 1' \
	'#define CONFIG_MSEG_SIZE 0' '#define CONFIG_BIOS_RESOURCE_LIST_SIZE 0' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"

# Compile the exact production functions, not a copied implementation. Only
# rmodule, fanout and transport boundaries are mocked in this host lane.
awk '
 /^static int append_and_check_region\(/ ||
 /^static enum cb_err reserve_authvar_arena\(/ ||
 /^int smm_load_module\(/ { copying = 1; opens = 0; closes = 0 }
 copying {
  print
  line = $0; opens += gsub(/{/, "{", line)
  line = $0; closes += gsub(/}/, "}", line)
  if (opens && opens == closes) copying = 0
 }
' "${CANONICAL_ARENA_LOADER_SOURCE:-$root/src/cpu/x86/smm/smm_module_loader.c}" \
	> "$temporary/actual-arena-loader.c"
awk '/^int region_is_subregion\(/ { copying = 1 }
 copying { print; if ($0 == "}") exit }
' "$root/src/commonlib/region.c" > "$temporary/actual-region.c"
for attested in 0 1; do
printf '#define CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED %s\n' "$attested" \
	> "$temporary/include/attested.h"
for optimization_flags in '-O0' '-O2' '-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all'; do
	flags="-std=gnu11 $optimization_flags -Wall -Wextra -Werror -Wshadow -fno-builtin -no-pie"
	includes="-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -include $temporary/include/attested.h -include $root/src/include/kconfig.h -include $root/src/include/rules.h -include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h -I$temporary/include -I$temporary -I$root/src/include -I$root/src -I$root/src/commonlib/include -I$root/src/commonlib/bsd/include -I$root/src/arch/x86/include"
	# Deliberate compiler flag splitting; all paths are controlled repository paths.
	# shellcheck disable=SC2086
	${CC:-cc} $flags $includes -Dpayload_mm_authvar_smm_arena_reserve=real_arena_reserve \
		-c "$root/src/lib/payload_mm_authvar_smm_loader.c" -o "$temporary/allocator.o"
	# shellcheck disable=SC2086
	${CC:-cc} $flags $includes \
		"$root/tests/lib/payload_mm_authvar_canonical_arena_loader_test.c" \
		"$root/src/cpu/x86/smm_invocation_loader_instance.c" \
		"$root/src/cpu/x86/smm_invocation_topology.c" \
		"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
		"$temporary/allocator.o" -o "$temporary/test"
	for needed in 0 1; do
		for mor in 0 1; do
			"$temporary/test" "$needed" "$mor" 0
			"$temporary/test" "$needed" "$mor" 1
			if test "$needed" = 1 && test "$attested" = 1; then
				for failure in $(seq 2 11); do
					"$temporary/test" "$needed" "$mor" "$failure"
				done
			fi
		done
	done
done
done
echo 'Actual canonical late arena loader / legacy MOR ownership: PASS (host boundaries)'
