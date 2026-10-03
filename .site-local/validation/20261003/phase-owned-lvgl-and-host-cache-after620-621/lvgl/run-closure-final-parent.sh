#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
proof=/home/sean/phase-owned-lvgl-proof.CuEvQK
root=/home/sean/Documents/.cdk2-worktrees/dxe-entry-report-host-closure-after614
configuration=$proof/closure-p1/include
mapfile -t sources < "$proof/actual-sources.txt"
sha256sum "$root/src/boot/Makefile" "$root/src/modules/dxe_core/entry.c" \
	"$root/tests/splash_status_report_test.c" "$configuration/cdk2/config.h" \
	"$proof/run-closure-final-parent.sh" > "$proof/closure-final-before.sha256"
cc -std=c11 -O0 -Wall -Wextra -Werror -fshort-wchar -m64 \
	-ffunction-sections -fdata-sections -fno-pie -no-pie -Wl,--gc-sections \
	-fsanitize=address,undefined -fno-sanitize-recover=all \
	-DCDK2_HOST_TEST -DCDK2_SPLASH_TEST_DEBUG=1 \
	-I"$configuration" -I"$root/include" -I"$root/src/modules/dxe_core" \
	-I"$root/src/lib/tcg_hash/vendor/linux/include" \
	"$root/tests/splash_status_report_test.c" "${sources[@]}" \
	-o "$proof/closure-final-parent"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	"$proof/closure-final-parent"
sha256sum -c "$proof/closure-final-before.sha256"
