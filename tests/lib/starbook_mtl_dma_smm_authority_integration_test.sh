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
	> "$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -D__SMM__
	-DSTARBOOK_MTL_DMA_SMM_TEST_ECAM_BUSES=2
	-include $root/src/include/kconfig.h -include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include -I$root/src/soc/intel/common/block/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -no-pie"
sources="$root/tests/lib/starbook_mtl_dma_smm_authority_integration_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_receiver.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_policy.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_authority.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_requester_authority.c
	$root/src/soc/intel/common/block/vtd/vtd_translation.c
	$root/src/soc/intel/common/block/vtd/vtd_translation_verify.c"
cases='valid valid-s3 epoch-s3 forged-graph table-drift ecam-drift stale-s3'

for flags in '-O0' '-O2' \
	'-O1 -g -fsanitize=undefined -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/test"
	for case_name in $cases; do
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/test" "$case_name"
	done
done

# shellcheck disable=SC2086
${CC:-cc} $common -O2 -fstack-usage -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_authority.c" \
	-o "$temporary/authority-stack.o"
awk -F '\t' \
	'$1 ~ /starbook_mtl_dma_smm_authority_verify$/ && $2 > 1024 { exit 1 }' \
	"$temporary/authority-stack.su"

# The composed SMM path is 32-bit. Keep strict conversions on the new
# authority/policy/verifier sources; receiver's native stage build below is
# the guard for its common headers, which are not -Wconversion clean.
strict="$common -Wconversion -m32"
for source in \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_authority.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_policy.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_requester_authority.c" \
	"$root/src/soc/intel/common/block/vtd/vtd_translation.c" \
	"$root/src/soc/intel/common/block/vtd/vtd_translation_verify.c"; do
	# shellcheck disable=SC2086
	${CC:-cc} $strict -O2 -c "$source" \
		-o "$temporary/$(basename "$source").o"
done

# shellcheck disable=SC2086
${CC:-cc} $common -O2 -fstack-usage -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_receiver.c" \
	-o "$temporary/receiver-stack.o"
awk -F '\t' '
	$1 ~ /starbook_mtl_dma_receipt_provision_receive$/ && $2 > 1024 { exit 1 }
	$1 ~ /starbook_mtl_dma_smm_binding_get$/ && $2 > 256 { exit 1 }
' "$temporary/receiver-stack.su"
# shellcheck disable=SC2086
${CC:-cc} $common -O2 -fstack-usage -c \
	"$root/src/soc/intel/common/block/vtd/vtd_translation_verify.c" \
	-o "$temporary/verifier-stack.o"
awk -F '\t' \
	'$1 ~ /vtd_translation_verify$/ && $2 > 256 { exit 1 }' \
	"$temporary/verifier-stack.su"

echo 'StarBook MTL protected DMA authority integration tests: PASS'
