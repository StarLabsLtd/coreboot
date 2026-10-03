#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
proof=$1
candidate=$2
for profile in p0 p1; do
	for optimization in 0 2; do
		cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" -fshort-wchar -m64 \
			-fno-pie -no-pie -fsanitize=address,undefined -fno-sanitize-recover=all \
			-fno-omit-frame-pointer \
			-I"/home/sean/coreboot-dma-api-proof.O7TXwY/gates-$profile/include" \
			-I"$candidate/include" -I"$candidate/src/boot" \
			"$candidate/tests/dma_handoff_test.c" \
			"$candidate/src/boot/coreboot_dma_handoff.c" "$candidate/src/boot/coreboot_checksum.c" \
			-o "$proof/$profile-o$optimization"
		ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$proof/$profile-o$optimization"
		printf '%s O%s real DMA fixture strictSAN PASS\n' "$profile" "$optimization"
	done
done
(cd "$candidate" && sha256sum -c "$proof/source-before.sha256")
