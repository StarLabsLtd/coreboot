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

build_coupled()
{
 flags=$1
 private=$2
 receiver=$3
 output=$4
 # Retain the receipt owner's existing strict profile; its historical SHA
 # implementation is not conversion-clean. The wave sources retain -Wconversion.
 # shellcheck disable=SC2086
 ${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -fno-builtin $flags \
  -D__COREBOOT__ -D__TEST__ -include "$root/src/include/kconfig.h" \
  -include "$root/src/include/rules.h" \
  -include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
  -I"$temporary/include" -I"$root/src" -isystem "$root/src/include" \
  -I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
  -c "$root/src/lib/bootmem_reservation_receipt.c" -o "$temporary/receipt.o"
 # shellcheck disable=SC2086
 ${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin \
  $flags -D__COREBOOT__ -D__TEST__ -DCONFIG_PAYLOAD_BOOT_PRIVATE_BUFFER="$private" \
  -include "$root/src/include/kconfig.h" \
  -include "$root/src/include/rules.h" \
  -include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
  -I"$temporary/include" -I"$root/src" -isystem "$root/src/include" \
  -I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
  -I"$root/src/arch/x86/include" -I"$root/src/soc/intel/common/block/include" \
  -I"$root/src/mainboard/starlabs/starbook/variants/mtl" -pthread -no-pie \
  "$root/tests/mainboard/starlabs/starbook_mtl_authvar_service_coupled_wave_test.c" \
  "$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.c" \
  "$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_protected_region.c" \
  "$receiver" "$temporary/receipt.o" \
  "$root/src/soc/intel/common/block/smm/invocation_adapter.c" \
  "$root/src/cpu/x86/smm/save_state_geometry.c" \
  "$root/src/cpu/x86/smm_invocation_evidence.c" \
  "$root/src/cpu/x86/smm_invocation_evidence_loader.c" \
  "$root/src/cpu/x86/smm_invocation_entry.c" \
  "$root/src/cpu/x86/smm_invocation_loader_instance.c" \
  "$root/src/cpu/x86/smm_invocation_loader_composition_gate.c" \
  "$root/src/soc/intel/common/block/smm/invocation_cause.c" \
  "$root/src/cpu/x86/smm_invocation_topology.c" -o "$output"
}
receiver="$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_receiver.c"
coupled_log_clean()
{
	if grep -Eqi '[Aa]ssert|oracle failure|runtime error:|Sanitizer' "$1"; then
		echo 'ERROR: coupled child emitted a masked failure diagnostic' >&2
		return 1
	fi
}
for private in 0 1; do
for flags in '-O0' '-O2' \
 '-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
 '-O1 -g -fsanitize=thread -fno-omit-frame-pointer -Wno-tsan'; do
 build_coupled "$flags" "$private" "$receiver" "$temporary/coupled"
 faults='0 1 2 3 4'
 if [ "$private" = 1 ]; then faults="$faults 5 6 7 8 9"; fi
 for fault in $faults; do
  printf 'coupled wave fault %s (%s)\n' "$fault" "$flags"
  log="$temporary/coupled-$private-$fault.log"
  if ! ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
   TSAN_OPTIONS=halt_on_error=1 "$temporary/coupled" "$fault" > "$log" 2>&1; then
   cat "$log" >&2
   exit 1
  fi
  if ! coupled_log_clean "$log"; then
   cat "$log" >&2
   exit 1
  fi
 done
done
done
build_coupled '-O2' 1 "$receiver" "$temporary/diagnostic-child"
for fault in 10 11 12 13; do
	log="$temporary/injected-diagnostic-$fault.log"
	"$temporary/diagnostic-child" "$fault" > "$log" 2>&1
	if coupled_log_clean "$log" > "$temporary/diagnostic-rejection.log" 2>&1; then
		echo 'ERROR: coupled child diagnostic filter accepted an injected error' >&2
		exit 1
	fi
done
sed 's/if (payload_mm_authvar_service_finalize() != CB_SUCCESS)/if (starbook_mtl_boot_private_lease_prepare(\&slot->boot_private_verifier, \&response.boot_private) != CB_SUCCESS || payload_mm_authvar_service_finalize() != CB_SUCCESS)/' \
 "$receiver" > "$temporary/lease-before-finalize.c"
! cmp -s "$receiver" "$temporary/lease-before-finalize.c"
build_coupled '-O2' 1 "$temporary/lease-before-finalize.c" "$temporary/order-mutant"
if "$temporary/order-mutant" 0 > "$temporary/order-mutant.log" 2>&1; then
 echo 'ERROR: BOOT lease was admitted before actual all-CPU finalization' >&2
 exit 1
fi
grep -q 'finalize_checks == 2' "$temporary/order-mutant.log"
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
