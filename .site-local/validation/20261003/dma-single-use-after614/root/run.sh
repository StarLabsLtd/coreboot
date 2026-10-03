#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
proof=$1
candidate=$2
baseline=$3
for profile in p0 p1; do
	configuration=/home/sean/coreboot-dma-api-proof.O7TXwY/gates-$profile/include
	for optimization in 0 2 s; do
		for source in baseline candidate; do
			tree=$candidate
			if test "$source" = baseline; then tree=$baseline; fi
			cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" -fshort-wchar -m64 \
				-I"$configuration" -I"$tree/include" -I"$tree/src/boot" \
				-c "$tree/src/boot/coreboot_dma_handoff.c" \
				-o "$proof/$profile-$source-o$optimization.o"
		done
		objdump -dr "$proof/$profile-baseline-o$optimization.o" > \
			"$proof/$profile-baseline-o$optimization.dis"
		objdump -dr "$proof/$profile-candidate-o$optimization.o" > \
			"$proof/$profile-candidate-o$optimization.dis"
		if cmp -s "$proof/$profile-baseline-o$optimization.o" \
		    "$proof/$profile-candidate-o$optimization.o"; then
			printf '%s O%s whole object equal\n' "$profile" "$optimization"
		else
			printf '%s O%s whole object differs; source equivalence and tests required\n' \
				"$profile" "$optimization"
		fi
	done
	for optimization in 0 2; do
		cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" -fshort-wchar -m64 \
			-fno-pie -no-pie -fsanitize=address,undefined -fno-sanitize-recover=all \
			-fno-omit-frame-pointer -I"$configuration" -I"$candidate/include" \
			-I"$candidate/src/boot" "$candidate/tests/dma_handoff_test.c" \
			"$candidate/src/boot/coreboot_dma_handoff.c" \
			"$candidate/src/boot/coreboot_checksum.c" \
			-o "$proof/$profile-test-o$optimization"
		ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$proof/$profile-test-o$optimization"
	done
done
sha256sum -c "$proof/inputs-before.sha256"
