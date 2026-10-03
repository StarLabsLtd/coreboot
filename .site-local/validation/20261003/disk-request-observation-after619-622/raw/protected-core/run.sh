#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
stage=/home/sean/disk-request-normal-protected-core-after619.1Zg8yk
source=/home/sean/Documents/.cdk2-worktrees/disk-capsule-os-request-after616
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
ulimit -s 65536
ulimit -c 0
cd "$source"
git rev-parse HEAD > "$stage/source-head.txt"
test "$(cat "$stage/source-head.txt")" = 1baa1dbbf5050b62a9fa27c2b373af84df9b92ce
test -z "$(git status --porcelain)"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
  xargs -d '\n' sha256sum > "$stage/source-before.sha256"
cp /home/sean/disk-capsule-closed-core.E7NMM4/resolved.config "$stage/resolved.config"
sha256sum "$stage/run.sh" /home/sean/disk-capsule-closed-core.E7NMM4/resolved.config > "$stage/inputs-before.sha256"
status=0
/usr/bin/time -o "$stage/build.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
  make -j2 CDK2_BUILD_DIR="$stage" CDK2_CONFIG="$stage/resolved.config" \
  COREBOOT_CONFIG=/home/sean/disk-capsule-producer.5FwfSp/initial9-retry/full.config \
  CDK2_BEARSSL_DIR=/home/sean/Documents/cdk2/3rdparty/bearssl \
  CDK2_LVGL_ROOT=/home/sean/Documents/cdk2/3rdparty/lvgl \
  native-coreboot-image > "$stage/build.log" 2>&1 || status=$?
printf '%s\n' "$status" > "$stage/outer.status"
if test "$status" = 0; then
  grep -qx '#define CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME 1' "$stage/include/cdk2/config.h"
  grep -qx '#define CONFIG_CDK2_NATIVE_SYSTEM_FMP 1' "$stage/include/cdk2/config.h"
  grep -qx '#define CONFIG_CDK2_QEMU_ACCEPTANCE_PROFILE 0' "$stage/include/cdk2/config.h"
fi
sha256sum -c "$stage/source-before.sha256" > "$stage/source-after-check.log"
sha256sum -c "$stage/inputs-before.sha256" > "$stage/inputs-after-check.log"
git status --porcelain > "$stage/source-after.status"
cat "$stage/build.time"
exit "$status"
