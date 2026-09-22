#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

for flags in '-O0' '-O2' '-O1 -fsanitize=address,undefined'; do
	cc -std=gnu11 -Wall -Wextra -Werror $flags \
		"$root/tests/lib/starbook_mtl_dma_diagnostic_test.c" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_diagnostic.c" \
		-o "$temporary/test"
	"$temporary/test"
done
printf '%s\n' 'StarBook MTL DMA diagnostic tests: PASS'
