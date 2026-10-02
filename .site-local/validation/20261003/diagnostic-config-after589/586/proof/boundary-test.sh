#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -eu
proof=$1
candidate=$2
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
for profile in p0 p1; do
	for optimization in 0 2; do
		for variant in baseline candidate; do
			source="$proof/baseline"
			if test "$variant" = candidate; then source=$candidate; fi
			cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" -g \
				-fshort-wchar -pthread -fno-pie -no-pie \
				-fsanitize=address,undefined -fno-sanitize-recover=all \
				-DCDK2_DIAG_UNIT_TEST -I"$proof/$profile/include" \
				-I"$source/include" \
				-DDIAGNOSTIC_IMPLEMENTATION_SOURCE="\"$source/src/lib/diagnostic.c\"" \
				"$proof/boundary.c" -o "$proof/$profile-$variant-boundary-o$optimization"
			"$proof/$profile-$variant-boundary-o$optimization"
		done
		printf '%s O%s actual private length/decimal/elapsed boundaries PASS\n' \
			"$profile" "$optimization"
	done
done
