#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
source=/home/sean/Documents/.cdk2-worktrees/default-zero-timeout-hotkey-after626
stage=/home/sean/native-default-zero-hotkey-after628.CfqEvm
original_config=/home/sean/native-lvgl-normal-after623.zLwvcS/resolved.config
producer_config=/home/sean/disk-no-request-hex-producer-after619.pXSGcd/initial9/full.config
producer_tree=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
bearssl=/home/sean/Documents/cdk2/3rdparty/bearssl
lvgl=/home/sean/Documents/cdk2/3rdparty/lvgl
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean
ulimit -s 65536
ulimit -c 0
cd "$source"
test "$(git rev-parse HEAD)" = d8e49af2ab0c90c6f6542b29026bd80309a9e1da
test -z "$(git status --porcelain)"
git rev-parse HEAD > "$stage/source-head.txt"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
  xargs -d '\n' sha256sum > "$stage/source-before.sha256"
for dependency in "$bearssl" "$lvgl"; do
  dependency_head=$(git -C "$dependency" rev-parse HEAD)
  expected_head=$(git ls-tree HEAD "3rdparty/$(basename "$dependency")" | awk '{print $3}')
  test "$dependency_head" = "$expected_head"
  test -z "$(git -C "$dependency" status --porcelain)"
  printf '%s %s\n' "$dependency" "$dependency_head"
done > "$stage/vendor-heads.txt"
cp -p "$original_config" "$stage/resolved.config"
sha256sum "$stage/build-core.sh" "$original_config" "$producer_config" /usr/bin/cc /usr/bin/make > "$stage/inputs-before.sha256"
status=0
/usr/bin/time -o "$stage/build.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
  make -j2 native-coreboot-image CDK2_BUILD_DIR="$stage" CDK2_CONFIG="$stage/resolved.config" \
  COREBOOT_TREE="$producer_tree" COREBOOT_CONFIG="$producer_config" \
  CDK2_BEARSSL_DIR="$bearssl" CDK2_LVGL_ROOT="$lvgl" \
  > "$stage/build.log" 2>&1 || status=$?
printf '%s\n' "$status" > "$stage/outer.status"
if test "$status" = 0; then
  cmp "$original_config" "$stage/resolved.config"
  grep -qx '#define CONFIG_CDK2_BOOT_TIMEOUT 0' "$stage/include/cdk2/config.h"
  grep -qx '#define CONFIG_CDK2_BUILD_DEBUG 0' "$stage/include/cdk2/config.h"
  for symbol in PROTECTED_VARIABLE_RUNTIME NATIVE_SYSTEM_FMP LVGL_RENDERER LINEAR_SETUP_HOTKEY; do
    grep -qx "#define CONFIG_CDK2_$symbol 1" "$stage/include/cdk2/config.h"
  done
  for symbol in NATIVE_QEMU_TEST_FMP QEMU_ACCEPTANCE_PROFILE; do
    grep -qx "#define CONFIG_CDK2_$symbol 0" "$stage/include/cdk2/config.h"
  done
  sha256sum "$stage/resolved.config" "$stage/include/cdk2/config.h" \
    "$stage/native/cdk2-coreboot-image.elf" > "$stage/outputs.sha256"
fi
sha256sum -c "$stage/source-before.sha256" > "$stage/source-after-check.log"
sha256sum -c "$stage/inputs-before.sha256" > "$stage/inputs-after-check.log"
git status --porcelain > "$stage/source-after.status"
cat "$stage/build.time"
exit "$status"
