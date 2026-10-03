#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
stage=/home/sean/native-default-zero-hotkey-after628.CfqEvm
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
original_config=/home/sean/disk-no-request-hex-producer-after619.pXSGcd/initial9/full.config
core="$stage/native/cdk2-coreboot-image.elf"
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
test "$(cat "$stage/outer.status")" = 0
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test -z "$(git -C "$producer" status --porcelain)"
test "$(grep -c '^CONFIG_PAYLOAD_FILE=' "$original_config")" = 1
grep -qx 'CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=0x001a0009' "$original_config"
mkdir "$stage/initial9"
sed "s|^CONFIG_PAYLOAD_FILE=.*|CONFIG_PAYLOAD_FILE=\"$core\"|" "$original_config" > "$stage/initial9/full.config"
cp -p "$stage/initial9/full.config" "$stage/initial9/input.config"
sha256sum "$core" "$original_config" "$stage/build-producer.sh" "$stage/initial9/input.config" > "$stage/initial9/inputs-before.sha256"
cd "$producer"
git rev-parse HEAD > "$stage/initial9/source-head.txt"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
  xargs -d '\n' sha256sum > "$stage/initial9/source-before.sha256"
make -j2 UPDATED_SUBMODULES=1 obj="$stage/initial9/build" DOTCONFIG="$stage/initial9/full.config" \
  VBOOT_SOURCE=/home/sean/Documents/coreboot/3rdparty/vboot olddefconfig > "$stage/initial9/configure.log" 2>&1
grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED=y' "$stage/initial9/full.config"
grep -qx 'CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=0x001a0009' "$stage/initial9/full.config"
for symbol in DRIVERS_EFI_UPDATE_CAPSULES SMMSTORE SMMSTORE_FULL_FLASH_ACCESS; do
  ! grep -qx "CONFIG_$symbol=y" "$stage/initial9/full.config"
done
/usr/bin/time -o "$stage/initial9/build.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
  make -j2 UPDATED_SUBMODULES=1 obj="$stage/initial9/build" DOTCONFIG="$stage/initial9/full.config" \
  VBOOT_SOURCE=/home/sean/Documents/coreboot/3rdparty/vboot all > "$stage/initial9/build.log" 2>&1
sha256sum -c "$stage/initial9/source-before.sha256" > "$stage/initial9/source-after-check.log"
sha256sum -c "$stage/initial9/inputs-before.sha256" > "$stage/initial9/inputs-after-check.log"
sha256sum "$stage/initial9/build/coreboot.rom" "$stage/initial9/build/util/cbfstool/cbfstool" > "$stage/initial9/outputs.sha256"
