#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include/arch" "$temporary/include/soc" \
	"$temporary/include/cpu/x86" "$temporary/include/device" \
	"$temporary/include/intelblocks"
printf '%s\n' '#pragma once' '#include <stdint.h>' \
	'typedef struct { uint32_t lo, hi; } msr_t;' \
	'msr_t rdmsr(unsigned int index);' > "$temporary/include/cpu/x86/msr.h"
printf '%s\n' '#pragma once' '#include <stdint.h>' \
	'#include <types.h>' '#include <device/pci_type.h>' \
	'uint32_t pci_read_config32(unsigned int device, unsigned int index);' \
	> "$temporary/include/device/pci_ops.h"
printf '%s\n' '#pragma once' '#define TSEG 0xb8' '#define BGSM 0xb4' \
	> "$temporary/include/intelblocks/systemagent.h"
printf '%s\n' '#define SA_DEV_ROOT 0' > "$temporary/include/soc/pci_devs.h"
printf '%s\n' '#include <stdint.h>' 'uint8_t inb(uint16_t port);' \
 'uint32_t inl(uint16_t port);' > "$temporary/include/arch/io.h"
printf '%s\n' '#define APM_STS_BIT 5' '#define ACPI_BASE_ADDRESS 0x1800' \
 '#define SMI_STS 0x34' > "$temporary/include/soc/pm.h"
printf '%s\n' \
 '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
 '#define CONFIG_ROM_SIZE 8388608' \
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
  -I"$temporary/include" -I"$root/src" -isystem "$root/src/include" \
  -I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
  -I"$root/src/arch/x86/include" -I"$root/src/soc/intel/common/block/include" \
  -I"$root/src/mainboard/starlabs/starbook/variants/mtl" -pthread -no-pie \
  "$root/tests/mainboard/starlabs/starbook_mtl_authvar_service_bootstrap_wave_test.c" \
  "$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.c" \
  "$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_protected_region.c" \
  "$root/src/cpu/x86/smm_invocation_evidence.c" \
  "$root/src/cpu/x86/smm_invocation_evidence_loader.c" \
  "$root/src/cpu/x86/smm_invocation_entry.c" \
  "$root/src/cpu/x86/smm_invocation_loader_instance.c" \
  "$root/src/cpu/x86/smm_invocation_loader_composition_gate.c" \
  "$root/src/soc/intel/common/block/smm/invocation_cause.c" \
  "$root/src/cpu/x86/smm_invocation_topology.c" -o "$temporary/test"
 for fault in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29; do
  printf 'wave fault %s (%s)\n' "$fault" "$flags"
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
   TSAN_OPTIONS=halt_on_error=1 "$temporary/test" "$fault"
 done
done

for flags in '-O0' '-O2' \
 '-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
 '-O1 -g -fsanitize=thread -fno-omit-frame-pointer -Wno-tsan'; do
 # shellcheck disable=SC2086
 ${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin \
  $flags -D__COREBOOT__ -D__TEST__ -include "$root/src/include/kconfig.h" \
  -include "$root/src/include/rules.h" \
  -include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
  -I"$temporary/include" -I"$root/src" -isystem "$root/src/include" \
  -I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
  -I"$root/src/arch/x86/include" -I"$root/src/soc/intel/common/block/include" \
  -I"$root/src/mainboard/starlabs/starbook/variants/mtl" -pthread -no-pie \
  "$root/tests/mainboard/starlabs/starbook_mtl_authvar_service_coupled_wave_test.c" \
  "$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.c" \
  "$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_protected_region.c" \
  "$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_receiver.c" \
  "$root/src/soc/intel/common/block/smm/invocation_adapter.c" \
  "$root/src/cpu/x86/smm/save_state_geometry.c" \
  "$root/src/cpu/x86/smm_invocation_evidence.c" \
  "$root/src/cpu/x86/smm_invocation_evidence_loader.c" \
  "$root/src/cpu/x86/smm_invocation_entry.c" \
  "$root/src/cpu/x86/smm_invocation_loader_instance.c" \
  "$root/src/cpu/x86/smm_invocation_loader_composition_gate.c" \
  "$root/src/soc/intel/common/block/smm/invocation_cause.c" \
  "$root/src/cpu/x86/smm_invocation_topology.c" -o "$temporary/coupled"
 for fault in 0 1 2 3 4; do
  printf 'coupled wave fault %s (%s)\n' "$fault" "$flags"
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
   TSAN_OPTIONS=halt_on_error=1 "$temporary/coupled" "$fault"
 done
done
${CC:-cc} -std=gnu11 -Os -m32 -march=i686 -Wall -Wextra -Werror \
	-Wconversion -Wshadow -ffreestanding -fno-builtin -fstack-usage \
	-D__COREBOOT__ -D__TEST__ -include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src" -isystem "$root/src/include" \
	-isystem "$root/src/commonlib/include" -isystem "$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" -I"$root/src/soc/intel/common/block/include" \
	-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
	-c "$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.c" \
	-o "$temporary/dispatch-native.o"
awk -F '\t' '
	NF >= 3 { frames++; if ($2 > 4096) exit 1 }
	END { if (!frames) exit 1 }
' "$temporary/dispatch-native.su"
! nm -u "$temporary/dispatch-native.o" | grep -q '__atomic_'
echo 'Canonical service genuine all-CPU BOOT wave: PASS'
