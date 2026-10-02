#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
proof=$1
candidate=$2
for diagnostic in 0 1; do
	for optimization in 0 2 s; do
		for variant in baseline candidate; do
			source="$proof/baseline"
			if test "$variant" = candidate; then source=$candidate; fi
			cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" \
				-I"$proof/pci-$diagnostic" -I"$source/include" \
				-I"$source/src/boot" -c "$source/tests/pci_dma_arena_test.c" \
				-o "$proof/pci-$diagnostic-$variant-o$optimization.o"
		done
		cmp "$proof/pci-$diagnostic-baseline-o$optimization.o" \
			"$proof/pci-$diagnostic-candidate-o$optimization.o"
		printf 'actual isolated PCI diagnostic=%s O%s whole object PASS\n' \
			"$diagnostic" "$optimization"
	done
done
