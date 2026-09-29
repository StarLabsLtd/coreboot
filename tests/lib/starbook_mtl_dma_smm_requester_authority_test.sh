#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -D__SMM__ -include $root/src/include/kconfig.h
	-include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -no-pie"
sources="$root/tests/lib/starbook_mtl_dma_smm_requester_authority_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_requester_authority.c"

for flags in '-O0' '-O2' \
	'-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/test"
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/test"
done

${CC:-cc} $common -O2 -fstack-usage -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_requester_authority.c" \
	-o "$temporary/stack.o"
awk -F '\t' \
	'$1 ~ /starbook_mtl_dma_requester_authority_derive$/ && $2 > 512 { exit 1 }' \
	"$temporary/stack.su"
# shellcheck disable=SC2086
${CC:-cc} $common -m32 -O2 -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_requester_authority.c" \
	-o "$temporary/authority-32.o"

grep -q 'dma_smm_requester_authority.c' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/Makefile.mk"
grep -q '^config STARLABS_STARBOOK_MTL_DMA_SMM_REQUESTER_AUTHORITY$' \
	"$root/src/mainboard/starlabs/starbook/Kconfig"

echo 'StarBook MTL SMM requester authority validation: PASS'
