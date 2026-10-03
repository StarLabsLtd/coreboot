#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
repository=$1
output=$2
configuration=$3
profile=$4
case "$output" in /home/sean/*) ;; *) exit 2;; esac
case "$profile" in p0|p1) ;; *) exit 2;; esac
test ! -e "$output"
mkdir -p "$output"
cp "$configuration" "$output/resolved.config"
set -- native-linear-boot-test native-coreboot-test \
	native-splash-status-report-test native-protected-variable-boot-gate-test \
	native-capsule-ram-window-core-test
if test "$profile" = p1; then
	set -- "$@" native-capsule-certified-refusal-test
fi
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean
cd "$repository"
make "$@" CDK2_CONFIG="$output/resolved.config" \
	CDK2_CONFIG_HEADER="$output/include/cdk2/config.h" \
	CDK2_BUILD_DIR="$output" COREBOOT_CONFIG= \
	CDK2_BEARSSL_DIR=/home/sean/Documents/.cdk2-worktrees/fmp-owner-native-state-after-update/3rdparty/bearssl \
	CDK2_LVGL_ROOT=/home/sean/Documents/.cdk2-worktrees/fmp-owner-native-state-after-update/3rdparty/lvgl
