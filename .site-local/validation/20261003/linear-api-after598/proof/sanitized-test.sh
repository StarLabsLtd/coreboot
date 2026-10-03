#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -eu
proof=$1
candidate=$2
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ulimit -c 0
for profile in p0 p1; do
	for optimization in 0 2; do
		cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" \
			-fshort-wchar -m64 -fno-pie -no-pie \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-I"$proof/$profile/include" -I"$candidate/include" \
			"$proof/representation.c" "$candidate/src/lib/linear_boot.c" \
			-o "$proof/san-$profile-o$optimization"
		"$proof/san-$profile-o$optimization" \
			> "$proof/san-$profile-o$optimization.bin" \
			2> "$proof/san-$profile-o$optimization.layout"
		cmp "$proof/$profile-candidate-o$optimization.bin" "$proof/san-$profile-o$optimization.bin"
		cmp "$proof/$profile-candidate-o$optimization.layout" "$proof/san-$profile-o$optimization.layout"
		cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" \
			-fshort-wchar -m64 -fno-pie -no-pie \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-I"$proof/$profile/include" -I"$candidate/include" \
			"$candidate/tests/linear_boot_test.c" "$candidate/src/lib/linear_boot.c" \
			-o "$proof/linear-san-$profile-o$optimization"
		"$proof/linear-san-$profile-o$optimization"
		printf '%s O%s strict SAN / bytes-layout parity / actual phase595 controls PASS\n' "$profile" "$optimization"
	done
done
