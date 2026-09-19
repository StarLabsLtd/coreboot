#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 cdk2-source-tree" >&2
	exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
cdk2=$(CDPATH= cd -- "$1" && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
test -f "$cdk2/src/boot/coreboot_dma_handoff.c"

Q35_DMA_HANDOFF_FIXTURE_OUTPUT="$temporary/q35.bin" \
	"$root/tests/lib/dma_handoff_standalone_test.sh"
test "$(wc -c < "$temporary/q35.bin")" -eq 156

"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror \
	-ffunction-sections -fdata-sections \
	-I"$cdk2/include" -I"$cdk2/src/boot" \
	"$root/tests/lib/q35-dma-cdk2-consumer-test.c" \
	"$cdk2/src/boot/coreboot_dma_handoff.c" \
	-Wl,--gc-sections -o "$temporary/consume"
"$temporary/consume" "$temporary/q35.bin"
