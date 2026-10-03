#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
output=$1
candidate=$2
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean
mkdir -p "$output/p0" "$output/p1"
cp /home/sean/linear-boot-api-gate.f3YlpW/resolved.config "$output/p0/resolved.config"
cp /home/sean/validated-uart-debug-gate.GrO7wr/resolved.config "$output/p1/resolved.config"
cd "$candidate"
git diff --name-only | xargs sha256sum > "$output/source-before.sha256"
for profile in p0 p1; do
	/usr/bin/time -o "$output/$profile/gates.time" \
		make native-acpi-table-test native-acpi-table-diagnostic-parity \
		native-tpm2-acpi-table-test \
		CDK2_CONFIG="$output/$profile/resolved.config" \
		CDK2_CONFIG_HEADER="$output/$profile/include/cdk2/config.h" \
		CDK2_BUILD_DIR="$output/$profile" \
		COREBOOT_CONFIG=/home/sean/q35-e8-scope-full.9sM2hH/full.config \
		CDK2_BEARSSL_DIR=/home/sean/Documents/.cdk2-worktrees/fmp-owner-native-state-after-update/3rdparty/bearssl \
		CDK2_LVGL_ROOT=/home/sean/Documents/.cdk2-worktrees/fmp-owner-native-state-after-update/3rdparty/lvgl \
		> "$output/$profile/gates.log" 2>&1
done
sha256sum -c "$output/source-before.sha256"
