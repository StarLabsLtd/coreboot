#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
 '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
 '#define CONFIG_MAX_CPUS 64' \
 '#define CONFIG_SMM_INVOCATION_EVIDENCE 1' \
 '#define CONFIG_SMM_INVOCATION_ENTRY 1' \
 '#define CONFIG_SMM_INVOCATION_FAIL_STOP_PLATFORM 1' \
 '#define CONFIG_SMM_INVOCATION_TOPOLOGY 1' \
 '#define CONFIG_SMM_INVOCATION_LOADER_INSTANCE 1' \
 '#define CONFIG_SMM_INVOCATION_LOADER_COMPOSITION 1' \
 '#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
 '#define CONFIG_SMM_APMC_ROUTE_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE 1' \
 '#define CONFIG_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION 1' \
 '#define CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DMA_POLICY 1' \
 '#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER 1' \
 '#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY 1' \
 '#define CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED 1' \
 > "$temporary/include/config.h"
for flags in '-O0' '-O2' \
 '-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
 '-O1 -g -fsanitize=thread -fno-omit-frame-pointer -Wno-tsan'; do
 # Deliberate compiler flag splitting, no production source rewriting.
 # shellcheck disable=SC2086
 ${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin \
  $flags -D__COREBOOT__ -D__TEST__ -include "$root/src/include/kconfig.h" \
  -include "$root/src/include/rules.h" \
  -include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
  -I"$temporary/include" -I"$root/src" -I"$root/src/include" \
  -I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
  -I"$root/src/arch/x86/include" -I"$root/src/soc/intel/common/block/include" \
  -I"$root/src/mainboard/starlabs/starbook/variants/mtl" -pthread -no-pie \
  "$root/tests/mainboard/starlabs/starbook_mtl_authvar_service_bootstrap_wave_test.c" \
  "$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.c" \
  "$root/src/cpu/x86/smm_invocation_evidence.c" \
  "$root/src/cpu/x86/smm_invocation_evidence_loader.c" \
  "$root/src/cpu/x86/smm_invocation_entry.c" \
  "$root/src/cpu/x86/smm_invocation_loader_instance.c" \
  "$root/src/cpu/x86/smm_invocation_topology.c" -o "$temporary/test"
 for fault in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
  printf 'wave fault %s (%s)\n' "$fault" "$flags"
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
   TSAN_OPTIONS=halt_on_error=1 "$temporary/test" "$fault"
 done
done
echo 'Canonical service genuine all-CPU BOOT wave: PASS'
