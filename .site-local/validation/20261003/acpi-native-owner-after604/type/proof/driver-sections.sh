#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -eu
proof=$1
for profile in p0 p1; do
	for optimization in 0 2 s; do
		for variant in baseline candidate; do
			awk '
				/^Disassembly of section / {
					skip = ($0 == "Disassembly of section .text.install_table:")
				}
				NR > 3 && !skip {print}
			' "$proof/$profile-driver-$variant-o$optimization.disassembly" \
				> "$proof/$profile-driver-$variant-o$optimization.other-sections"
		done
		cmp "$proof/$profile-driver-baseline-o$optimization.other-sections" \
			"$proof/$profile-driver-candidate-o$optimization.other-sections"
		printf '%s O%s derivative disassembly excluding actual install_table adapter: PASS\n' \
			"$profile" "$optimization"
	done
done
