#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow
	-D__COREBOOT__ -D__TEST__ -D__RAMSTAGE__
	-include $root/src/include/kconfig.h -include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -no-pie"
sources="$root/tests/lib/starbook_mtl_dma_live_mirror_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_live_mirror.c"

for flags in '-O0' '-O2' '-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/test"
	for case_name in cold s3 missing wrong-size moved zero-size invalid-lifecycle; do
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/test" "$case_name"
	done
done

echo 'StarBook MTL retained DMA mirror tests: PASS'
