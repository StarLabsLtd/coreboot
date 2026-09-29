#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_COMPOSITION 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_INSTANCE 1' \
	'#define CONFIG_SMM_INVOCATION_TOPOLOGY 1' \
	'#define CONFIG_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION 1' \
	'#define CONFIG_ECAM_MMCONF_BUS_NUMBER 256' \
	'#define CONFIG_ECAM_MMCONF_BASE_ADDRESS 0xe0000000' \
	'#define CONFIG_ECAM_MMCONF_LENGTH 0x10000000' \
	> "$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -D__SMM__ -include $root/src/include/kconfig.h
	-include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -no-pie"
sources="$root/tests/lib/starbook_mtl_dma_smm_receipt_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_receiver.c"
cases='valid generation generation-rollback recursive-binding output-alias
	wrong-range protected-frame mutated-frame wrong-state
	null-table above-4g huge-table mmio-table smram-table unallowed-dram
	unallowed-frame unallowed-handoff unallowed-tables unallowed-mirror
	unallowed-arena0 unallowed-arena1 unallowed-arena2
	unprotected-dependencies unprotected-dependency-context
	unprotected-dependency-callback dependency-toctou'

for flags in '-O0' '-O2' '-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/test"
	for case_name in $cases; do
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/test" "$case_name"
	done
done

sender_common=$(printf '%s' "$common" | sed 's/-D__SMM__/-D__RAMSTAGE__/')
# shellcheck disable=SC2086
${CC:-cc} $sender_common -O2 \
	"$root/tests/lib/starbook_mtl_dma_smm_receipt_sender_test.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_sender.c" \
	-o "$temporary/sender"
"$temporary/sender" valid
"$temporary/sender" mutated-candidate
"$temporary/sender" high-address
${CC:-cc} $sender_common -O2 -fstack-usage -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_sender.c" \
	-o "$temporary/sender-stack.o"
awk -F '\t' '
	$1 ~ /starbook_mtl_dma_receipt_provision_send$/ { send = $2 }
	$1 ~ /:scrub$/ { scrub = $2 }
	END { if (!send || !scrub || send + scrub > 512) exit 1 }
' "$temporary/sender-stack.su"

# Compile the actual option-on ramstage sources without the test environment.
# This catches stage-dependent declarations and duplicate production headers.
production_common=$(printf '%s' "$common" |
	sed 's/-D__TEST__ //; s/-D__SMM__/-D__RAMSTAGE__/; s/-Wconversion //')
# shellcheck disable=SC2086
${CC:-cc} $production_common -O2 -Wredundant-decls -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_sender.c" \
	-o "$temporary/production-sender.o"
# shellcheck disable=SC2086
${CC:-cc} $production_common -O2 -Wredundant-decls -Wno-unused-parameter \
	-I"$root/src/soc/intel/common/block/include" \
	-I"$root/src/soc/intel/meteorlake/include" -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.c" \
	-o "$temporary/production-platform.o"

${CC:-cc} $common -O2 -fstack-usage -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_receiver.c" \
	-o "$temporary/stack.o"
awk -F '\t' '$1 ~ /starbook_mtl_dma_receipt_provision_receive$/ && $2 > 1024 { exit 1 }' \
	"$temporary/stack.su"

cp "$root/configs/config.starlabs_starbook_mtl" "$temporary/config"
"$root/util/scripts/config" --file "$temporary/config" \
	-e STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION
make -s -C "$root" DOTCONFIG="$temporary/config" obj="$temporary/obj" \
	olddefconfig >/dev/null
! grep -q '^CONFIG_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION=y$' \
	"$temporary/config"
! grep -q '^CONFIG_STARLABS_STARBOOK_MTL_DMA_SMM_REQUESTER_AUTHORITY=y$' \
	"$temporary/config"

grep -A8 '^config STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION$' \
	"$root/src/mainboard/starlabs/starbook/Kconfig" | \
	grep -q 'select STARLABS_STARBOOK_MTL_DMA_SMM_REQUESTER_AUTHORITY'
grep -A8 '^config STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION$' \
	"$root/src/mainboard/starlabs/starbook/Kconfig" | \
	grep -q 'select SOC_INTEL_COMMON_BLOCK_VTD_TRANSLATION_VERIFY'

backend_line=$(grep -n 'starbook_mtl_dma_live_backend_ensure' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_live_handoff.c" |
	cut -d: -f1)
receipt_line=$(grep -n 'starbook_mtl_dma_receipt_provision_send' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_live_handoff.c" |
	cut -d: -f1)
test "$backend_line" -lt "$receipt_line"
grep -q 'ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/Makefile.mk"
grep -q 'smm-$(CONFIG_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/Makefile.mk"

echo 'StarBook MTL SMM DMA receipt provisioning tests: PASS'
