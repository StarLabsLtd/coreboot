#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
output=/home/sean/disk-no-request-producer-after619.AZSX8M
root=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
core=/home/sean/disk-request-normal-core-after619.3g1W3I/native/cdk2-coreboot-image.elf
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
ulimit -c 0
for pair in initial9:1703945 targetA:1703946; do
  name=${pair%:*}
  version=${pair#*:}
  mkdir "$output/$name"
  sed -e "s|^CONFIG_PAYLOAD_FILE=.*|CONFIG_PAYLOAD_FILE=\"$core\"|" \
    -e "s/^CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=.*/CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=$version/" \
    /home/sean/disk-capsule-closed-core.E7NMM4/producer.config > "$output/$name/full.config"
  cp "$output/$name/full.config" "$output/$name/input.config"
  git -C "$root" rev-parse HEAD > "$output/$name/source-head.txt"
  sha256sum "$core" "$output/build.sh" "$output/$name/input.config" > "$output/$name/inputs-before.sha256"
  cd "$root"
  git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
    xargs -d '\n' sha256sum > "$output/$name/source-before.sha256"
  make -j2 UPDATED_SUBMODULES=1 obj="$output/$name/build" DOTCONFIG="$output/$name/full.config" \
    VBOOT_SOURCE=/home/sean/Documents/coreboot/3rdparty/vboot olddefconfig > "$output/$name/configure.log" 2>&1
  grep -qx 'CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=y' "$output/$name/full.config"
  grep -qx 'CONFIG_DRIVERS_EFI_CAPSULE_DELIVERY_POLICY=y' "$output/$name/full.config"
  for symbol in DRIVERS_EFI_UPDATE_CAPSULES SMMSTORE SMMSTORE_FULL_FLASH_ACCESS; do
    ! grep -qx "CONFIG_$symbol=y" "$output/$name/full.config"
  done
  /usr/bin/time -o "$output/$name/build.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
    make -j2 UPDATED_SUBMODULES=1 obj="$output/$name/build" DOTCONFIG="$output/$name/full.config" \
    VBOOT_SOURCE=/home/sean/Documents/coreboot/3rdparty/vboot all > "$output/$name/build.log" 2>&1
  sha256sum -c "$output/$name/source-before.sha256" > "$output/$name/source-after-check.log"
  sha256sum -c "$output/$name/inputs-before.sha256" > "$output/$name/inputs-after-check.log"
  sha256sum "$output/$name/build/coreboot.rom" "$output/$name/build/util/cbfstool/cbfstool" > "$output/$name/outputs.sha256"
done
