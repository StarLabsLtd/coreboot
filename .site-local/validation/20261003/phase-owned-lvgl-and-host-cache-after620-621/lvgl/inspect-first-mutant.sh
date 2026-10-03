#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
proof=/home/sean/phase-owned-lvgl-proof.CuEvQK
root=/home/sean/Documents/.cdk2-worktrees/phase-owned-lvgl-status-after614
mapfile -t sources < "$proof/actual-sources.txt"
cc -std=c11 -O0 -Wall -Wextra -Werror -fshort-wchar -ffunction-sections \
	-fdata-sections -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-pie -no-pie -Wl,--gc-sections -DCDK2_HOST_TEST -DCDK2_SPLASH_TEST_DEBUG=0 \
	-DCDK2_SPLASH_ENTRY_SOURCE="\"$proof/coreboot-only-inspect.c\"" \
	-I"$proof/p1/include" -I"$root/include" -I"$root/src/modules/dxe_core" \
	-I"$root/src/lib/tcg_hash/vendor/linux/include" \
	"$root/tests/splash_status_report_test.c" "${sources[@]}" -o "$proof/coreboot-only-inspect"
ulimit -c 0
status=0
ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
	"$proof/coreboot-only-inspect" > "$proof/coreboot-only-inspect.log" 2>&1 || status=$?
echo "$status"
cat "$proof/coreboot-only-inspect.log"
