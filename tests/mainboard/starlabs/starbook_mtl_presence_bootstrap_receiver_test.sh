#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_MAX_CPUS 64' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	'#define CONFIG_SMM_INVOCATION_TOPOLOGY 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_INSTANCE 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_COMPOSITION 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER 1' \
	'#define CONFIG_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION 1' \
	'#define CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_ROUTE_COMPOSITION_OWNER 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY 1' \
	> "$temporary/include/config.h"
for optimization in 0 2; do
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-O"$optimization" -no-pie -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
		"$root/tests/mainboard/starlabs/starbook_mtl_presence_bootstrap_receiver_test.c" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_receiver.c" \
		-o "$temporary/test"
	for scenario in $(seq 0 19); do
		"$temporary/test" "$scenario"
	done
done

# Couple the unchanged receiver to the real Intel save-state adapter and
# evidence claim. A nonzero ECX frame address must survive REQUEST claim;
# the final sender response deliberately clears ECX.
printf '%s\n' '#define CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED 1' \
	'#define CONFIG_SMM_INVOCATION_EVIDENCE 1' \
	'#define CONFIG_SMM_INVOCATION_FAIL_STOP_PLATFORM 1' \
	>> "$temporary/include/config.h"
for optimization in 0 2; do
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-O"$optimization" -no-pie -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
		"$root/tests/mainboard/starlabs/starbook_mtl_presence_bootstrap_packed_claim_test.c" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_receiver.c" \
		"$root/src/cpu/x86/smm_invocation_evidence.c" \
		"$root/src/cpu/x86/smm_invocation_evidence_loader.c" \
		"$root/src/cpu/x86/smm/save_state_geometry.c" \
		"$root/src/soc/intel/common/block/smm/invocation_adapter.c" \
		-o "$temporary/packed-claim"
	for mode in 0 1 2 3; do
		ASAN_OPTIONS=detect_leaks=0 "$temporary/packed-claim" "$mode"
	done
done
echo 'MTL bootstrap actual receiver boundary/AP coexistence/completion-last tests: PASS'
