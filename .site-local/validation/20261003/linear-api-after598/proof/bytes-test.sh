#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -eu
proof=$1
candidate=$2
for profile in p0 p1; do
	for optimization in 0 2 s; do
		for variant in baseline candidate; do
			source="$proof/baseline"
			if test "$variant" = candidate; then source="$candidate"; fi
			cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" \
				-fno-pie -no-pie -fshort-wchar -m64 \
				-I"$proof/$profile/include" -I"$source/include" \
				"$proof/representation.c" "$source/src/lib/linear_boot.c" \
				-o "$proof/$profile-$variant-o$optimization"
			"$proof/$profile-$variant-o$optimization" \
				> "$proof/$profile-$variant-o$optimization.bin" \
				2> "$proof/$profile-$variant-o$optimization.layout"
		done
		cmp "$proof/$profile-baseline-o$optimization.bin" "$proof/$profile-candidate-o$optimization.bin"
		cmp "$proof/$profile-baseline-o$optimization.layout" "$proof/$profile-candidate-o$optimization.layout"
		printf '%s O%s all256/high64/native padding/full1112 HOB+real boot-path hash PASS\n' "$profile" "$optimization"
	done
done
