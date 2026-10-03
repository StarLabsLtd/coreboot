#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
stage=/home/sean/normal-efi-disk-producers-after625.ZBl2DT
core_stage=/home/sean/normal-efi-disk-core-final.TJMaS8
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
original_config=/home/sean/disk-no-request-hex-producer-after619.pXSGcd/initial9/full.config
core="$core_stage/native/cdk2-coreboot-image.elf"
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean
test "$(cat "$core_stage/outer.status")" = 0
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test -z "$(git -C "$producer" status --porcelain)"
test "$(grep -c '^CONFIG_PAYLOAD_FILE=' "$original_config")" = 1
grep -qx 'CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=0x001a0009' "$original_config"
sha256sum -c "$core_stage/outputs.sha256" > "$stage/pre-producer-core-check.log"
for pair in initial9:0x001a0009 targetA:0x001a000a; do
  name=${pair%:*}
  version=${pair#*:}
  mkdir "$stage/$name"
  sed -e "s|^CONFIG_PAYLOAD_FILE=.*|CONFIG_PAYLOAD_FILE=\"$core\"|" \
    -e "s/^CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=.*/CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=$version/" \
    "$original_config" > "$stage/$name/full.config"
  cp -p "$stage/$name/full.config" "$stage/$name/input.config"
  sha256sum "$core" "$original_config" "$stage/build-producers.sh" \
    "$stage/$name/input.config" > "$stage/$name/inputs-before.sha256"
  cd "$producer"
  git rev-parse HEAD > "$stage/$name/source-head.txt"
  git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
    xargs -d '\n' sha256sum > "$stage/$name/source-before.sha256"
  make -j2 UPDATED_SUBMODULES=1 obj="$stage/$name/build" DOTCONFIG="$stage/$name/full.config" \
    VBOOT_SOURCE=/home/sean/Documents/coreboot/3rdparty/vboot olddefconfig > "$stage/$name/configure.log" 2>&1
  grep -qx "CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=$version" "$stage/$name/full.config"
  for symbol in DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT DRIVERS_EFI_CAPSULE_DELIVERY_POLICY PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED; do
    grep -qx "CONFIG_$symbol=y" "$stage/$name/full.config"
  done
  for symbol in DRIVERS_EFI_UPDATE_CAPSULES SMMSTORE SMMSTORE_FULL_FLASH_ACCESS; do
    ! grep -qx "CONFIG_$symbol=y" "$stage/$name/full.config"
  done
  /usr/bin/time -o "$stage/$name/build.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
    make -j2 UPDATED_SUBMODULES=1 obj="$stage/$name/build" DOTCONFIG="$stage/$name/full.config" \
    VBOOT_SOURCE=/home/sean/Documents/coreboot/3rdparty/vboot all > "$stage/$name/build.log" 2>&1
  sha256sum -c "$stage/$name/source-before.sha256" > "$stage/$name/source-after-check.log"
  sha256sum -c "$stage/$name/inputs-before.sha256" > "$stage/$name/inputs-after-check.log"
  sha256sum "$stage/$name/build/coreboot.rom" "$stage/$name/build/util/cbfstool/cbfstool" > "$stage/$name/outputs.sha256"
done
