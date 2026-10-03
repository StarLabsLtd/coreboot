#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
proof=/home/sean/phase-owned-lvgl-proof.CuEvQK/joined-619
root=/home/sean/Documents/.cdk2-worktrees/phase-owned-lvgl-status-after614
mapfile -t sources < /home/sean/phase-owned-lvgl-proof.CuEvQK/actual-sources.txt
sha256sum "$root/src/modules/dxe_core/entry.c" "$root/tests/splash_status_report_test.c" \
	"/home/sean/phase-owned-lvgl-proof.CuEvQK/p1/include/cdk2/config.h" \
	"/home/sean/phase-owned-lvgl-proof.CuEvQK/run-joined-positive-fixture.sh" \
	> "$proof/final-positive-before.sha256"
cc -std=c11 -O0 -Wall -Wextra -Werror -fshort-wchar -m64 \
	-ffunction-sections -fdata-sections -fno-pie -no-pie -Wl,--gc-sections \
	-fsanitize=address,undefined -fno-sanitize-recover=all \
	-DCDK2_HOST_TEST -DCDK2_SPLASH_TEST_DEBUG=1 \
	-I/home/sean/phase-owned-lvgl-proof.CuEvQK/p1/include -I"$root/include" -I"$root/src/modules/dxe_core" \
	-I"$root/src/lib/tcg_hash/vendor/linux/include" \
	"$root/tests/splash_status_report_test.c" "${sources[@]}" \
	-o "$proof/final-positive-fixture"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	"$proof/final-positive-fixture"
sha256sum -c "$proof/final-positive-before.sha256"
