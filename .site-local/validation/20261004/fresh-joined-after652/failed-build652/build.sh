#!/bin/bash
set -euo pipefail
receipt=/home/sean/fresh-normal-after652.JeGj3p
firmware=/home/sean/Documents/cdk2
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
expected=${1:?published signed ready HEAD required}
trap 'printf "%s\n" "$?" > "$receipt/aggregate.status"' EXIT
test "$expected" = 14384fb9609ae158164dcbcc73126da7e568f1db
test "$(git -C "$firmware" rev-parse HEAD)" = "$expected"
git -C "$firmware" verify-commit "$expected" > "$receipt/signature.log" 2>&1
test -z "$(git -C "$firmware" status --porcelain)"
test ! -e "$receipt/build"
printf '%s\n' "$expected" > "$receipt/ready-head.txt"
sha256sum "$0" "$firmware/util/qemu/bin/build-normal-fwui-fresh.sh" > "$receipt/recipe-before.sha256"
/usr/bin/time -o "$receipt/whole-build.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
	bash "$firmware/util/qemu/bin/build-normal-fwui-fresh.sh" \
	"$firmware" "$producer" \
	/home/sean/native-lvgl-normal-after623.zLwvcS/resolved.config \
	/home/sean/disk-no-request-hex-producer-after619.pXSGcd/initial9/full.config \
	"$receipt/build" > "$receipt/whole-build.log" 2>&1
sha256sum -c "$receipt/recipe-before.sha256" > "$receipt/recipe-after-check.log"
test "$(git -C "$firmware" rev-parse HEAD)" = "$expected"
test -z "$(git -C "$firmware" status --porcelain)"
sha256sum "$receipt/build/native/cdk2-coreboot-image.elf" \
	"$receipt/build/initial9/build/coreboot.rom" "$receipt/build/fresh-build.json" \
	> "$receipt/artifacts.sha256"
