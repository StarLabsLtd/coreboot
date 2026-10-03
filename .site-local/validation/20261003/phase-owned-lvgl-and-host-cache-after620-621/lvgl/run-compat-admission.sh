#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
proof=/home/sean/phase-owned-lvgl-proof.CuEvQK
candidate=/home/sean/Documents/.cdk2-worktrees/phase-owned-lvgl-status-after614
header=$proof/compat/include/cdk2/config.h
grep -qx '#define CONFIG_CDK2_LINEAR_SETUP_HOTKEY 1' "$header"
grep -qx '#define CONFIG_CDK2_STRICT_DIRECT_RUNTIME 0' "$header"
mapfile -t sources < "$proof/actual-sources.txt"
# The normal shared HOST recipe selects these two actual implementations only
# when STRICT_DIRECT_RUNTIME is false; the original source list was strict.
sources+=("$candidate/src/modules/dxe_core/fv_protocol.c"
	"$candidate/src/modules/dxe_core/dispatcher.c")
sha256sum "$proof/compat-admission.c" "$proof/run-compat-admission.sh" \
	"$header" "$proof/admission-original.h" > "$proof/compat-before.sha256"
for tree in "$candidate" /home/sean/Documents/.cdk2-worktrees/dxe-entry-report-host-closure-after614; do
	while IFS= read -r -d '' path; do
		test ! -f "$tree/$path" || sha256sum "$tree/$path" >> "$proof/compat-before.sha256"
	done < <(git -C "$tree" ls-files -z)
done
for optimization in 0 2; do
	cc -std=c11 -O"$optimization" -Wall -Wextra -Werror -fshort-wchar -m64 \
		-ffunction-sections -fdata-sections -Wl,--gc-sections -fno-pie -no-pie \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DCDK2_HOST_TEST -DCDK2_SPLASH_TEST_DEBUG=1 \
		-DCDK2_SPLASH_FIXTURE_SOURCE="\"$candidate/tests/splash_status_report_test.c\"" \
		-DCDK2_UI_ADMISSION_BLOCK="\"$proof/admission-original.h\"" \
		-I"$proof/compat/include" -I"$candidate/include" -I"$candidate/src/modules/dxe_core" \
		-I"$candidate/src/lib/tcg_hash/vendor/linux/include" \
		"$proof/compat-admission.c" "${sources[@]}" -o "$proof/compat-admission-o$optimization"
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$proof/compat-admission-o$optimization" > "$proof/compat-admission-o$optimization.log" 2>&1
done
sha256sum -c "$proof/compat-before.sha256"
echo 'Real compatibility dispatcher release / verbatim HOST admission O0/O2/SAN: PASS'
