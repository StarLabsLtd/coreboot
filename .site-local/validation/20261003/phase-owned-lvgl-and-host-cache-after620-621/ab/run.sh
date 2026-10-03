#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
stage=/home/sean/splash-status-object-ab.IDrurC
baseline=/home/sean/Documents/.cdk2-worktrees/splash-status-baseline-after620
candidate=/home/sean/Documents/.cdk2-worktrees/splash-status-ephemeral-objects-after620
export PATH="$stage/tools:/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin"
export KEEP_SPLASH_STATUS_TMP=1
ulimit -c 0

run_gate()
{
	local variant=$1 profile=$2 source=$3 configuration=$4
	local output="$stage/$variant-p$profile"
	mkdir -p "$output/tmp"
	cp -- "$configuration" "$output/resolved.config"
	git -C "$source" rev-parse HEAD > "$output/source-head.txt"
	git -C "$source" diff --binary > "$output/source.patch"
	sha256sum "$output/resolved.config" "$stage/tools/sh" "$stage/run.sh" \
		/usr/bin/cc /bin/sh > "$output/inputs-before.sha256"
	while IFS= read -r -d '' path; do
		if test -f "$source/$path"; then
			sha256sum "$source/$path"
		fi
	done < <(git -C "$source" ls-files -z) > "$output/source-before.sha256"
	local status=0
	TMPDIR="$output/tmp" /usr/bin/time -o "$output/gate.time" \
		-f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
		make -j1 -C "$source" native-splash-status-report-test \
		CDK2_CONFIG="$output/resolved.config" CDK2_BUILD_DIR="$output" \
		COREBOOT_TREE=/home/sean/Documents/.coreboot-worktrees/authvar-store-native-offset-after387 \
		COREBOOT_CONFIG=/home/sean/q35-e8-scope-full.9sM2hH/full.config \
		CDK2_BEARSSL_DIR=/home/sean/Documents/.cdk2-worktrees/fmp-owner-native-state-after-update/3rdparty/bearssl \
		CDK2_LVGL_ROOT=/home/sean/Documents/.cdk2-worktrees/fmp-owner-native-state-after-update/3rdparty/lvgl \
		> "$output/gate.log" 2>&1 || status=$?
	printf '%s\n' "$status" > "$output/outer.status"
	sha256sum -c "$output/source-before.sha256" > "$output/source-after-check.log"
	sha256sum -c "$output/inputs-before.sha256" > "$output/inputs-after-check.log"
	printf '%s P%s ACTUAL_STATUS=%s\n' "$variant" "$profile" "$status"
	cat "$output/gate.time"
	test "$status" = 0
}

run_gate baseline 0 "$baseline" /home/sean/phase-owned-lvgl-proof.CuEvQK/final-p0/resolved.config
run_gate candidate 0 "$candidate" /home/sean/phase-owned-lvgl-proof.CuEvQK/final-p0/resolved.config
run_gate baseline 1 "$baseline" /home/sean/phase-owned-lvgl-proof.CuEvQK/p1/resolved.config
run_gate candidate 1 "$candidate" /home/sean/phase-owned-lvgl-proof.CuEvQK/p1/resolved.config
