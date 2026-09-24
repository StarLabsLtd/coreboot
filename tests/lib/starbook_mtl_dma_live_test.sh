#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

if grep -q 'quiesced' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_live.h"; then
	printf '%s\n' 'public quiescence proof is forbidden' >&2
	exit 1
fi

for flags in '-O0' '-O2' '-O1 -fsanitize=address,undefined'; do
	cc -std=gnu11 -Wall -Wextra -Werror $flags \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		"$root/tests/lib/starbook_mtl_dma_live_test.c" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_live.c" \
		"$root/src/soc/intel/common/block/vtd/vtd_translation.c" \
		"$root/src/soc/intel/common/block/vtd/vtd_transition.c" \
		-o "$temporary/test"
	for scenario in snapshot-failure clear-failure topology-boundary \
		bme-boundary noncoherent capacity unaligned-size mirror-virtual-misaligned \
		mirror-physical-misaligned mirror-physical-alias mirror-virtual-overlap \
		success active-selected-bme \
		active-unlisted-bme active-topology active-combined-poison; do
		"$temporary/test" "$scenario"
	done
done
printf '%s\n' 'StarBook MTL live DMA tests: PASS'
