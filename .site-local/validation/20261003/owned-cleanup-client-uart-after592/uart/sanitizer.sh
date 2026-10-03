#!/bin/sh
set -eu
source=/home/sean/Documents/.cdk2-worktrees/validated-uart-after591
receipt=/home/sean/validated-uart-gate.INSDAY
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
for profile in p0 p1; do
	config=/home/sean/unused-native-config-proof.c01WSF/$profile-candidate/include
	for optimization in 0 2; do
		cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" -g \
			-fshort-wchar -pthread -fno-pie -no-pie \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-DCDK2_DIAG_UNIT_TEST -I"$config" -I"$source/include" \
			"$source/tests/diagnostic_test.c" "$source/src/lib/diagnostic.c" \
			-o "$receipt/diagnostic-$profile-o$optimization"
		"$receipt/diagnostic-$profile-o$optimization"
		printf '%s O%s diagnostic ASan/UBSan PASS\n' "$profile" "$optimization"
	done
done
