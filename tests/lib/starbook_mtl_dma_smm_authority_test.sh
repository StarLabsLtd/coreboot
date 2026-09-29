#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	> "$temporary/include/config.h"
common="-std=gnu11 -Wall -Wextra -Werror -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -D__SMM__ -include $root/src/include/kconfig.h
	-include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -no-pie"
sources="$root/tests/lib/starbook_mtl_dma_smm_authority_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_authority.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_requester_authority.c
	$root/src/soc/intel/common/block/vtd/vtd_translation.c
	$root/src/soc/intel/common/block/vtd/vtd_translation_verify.c"
cases='valid primary-mutation mirror-mutation forged-inventory forged-identity
	swapped-arenas swapped-graph-roles generation-rollback generation-advance
	stale-lifecycle topology-drift binding-drift nonce-drift lifecycle-drift
	unprotected-workspace unprotected-receipt retained-underflow receipt-workspace-alias
	unprotected-config-output unprotected-binding-output unprotected-table-output
	aliased-config-output aliased-binding-output aliased-table-output'

for flags in '-O0' '-O2' \
	'-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/test"
	for case_name in $cases; do
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/test" "$case_name"
	done
done

# shellcheck disable=SC2086
${CC:-cc} $common -m32 -O2 -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_authority.c" \
	-o "$temporary/authority-32.o"
! nm -u "$temporary/authority-32.o" | grep -q '__atomic_load_8'
# shellcheck disable=SC2086
${CC:-cc} $common -O2 -fstack-usage -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_authority.c" \
	-o "$temporary/authority-stack.o"
awk -F '\t' \
	'$1 ~ /starbook_mtl_dma_smm_authority_verify$/ && $2 > 1024 { exit 1 }' \
	"$temporary/authority-stack.su"

echo 'StarBook MTL SMM DMA authority integration tests: PASS'
