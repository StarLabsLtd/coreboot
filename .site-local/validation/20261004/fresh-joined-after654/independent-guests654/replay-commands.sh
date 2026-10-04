#!/usr/bin/env bash
# PREPARED ONLY. Root executes after explicit compiler-lane release and guests0.
set -euo pipefail
firmware=/home/sean/Documents/cdk2
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
stage=/home/sean/fresh-normal-after654.nP6Vda/build
guests=/home/sean/fresh-normal-joined-guest-after654.Vr3XYD
review=/home/sean/fresh-normal-guest-independent-review654.o7hehD
expected=0bb127b503d8c5d973b07d608f564ff0ef2ef6bb
ui=$guests/ui-cancel/lifecycle/ui-cancel
linux=$guests/linux-reset/lifecycle/same-vm
rom=$stage/initial9/build/coreboot.rom
cbfstool=$stage/initial9/build/util/cbfstool/cbfstool
test "$(git -C "$firmware" rev-parse HEAD)" = "$expected"
test -z "$(git -C "$firmware" status --porcelain)"
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test -z "$(git -C "$producer" status --porcelain)"
git -C "$firmware" verify-commit "$expected"
git -C "$producer" verify-commit HEAD
test "$(cat /home/sean/fresh-normal-after654.nP6Vda/aggregate.status)" = 0
test "$(cat "$guests/ui-cancel/outer.status")" = 0
test "$(cat "$guests/linux-reset/outer.status")" = 0
mkdir "$review/replay-output"
output=$review/replay-output
trap 'printf "%s\n" "$?" > "$output/aggregate.status"' EXIT
envbase=(env -i PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
  TMPDIR=/home/sean LANG=C LC_ALL=C COREBOOT_TREE="$producer"
  CDK2_AB_CBFSTOOL="$cbfstool" HOSTCC=/usr/bin/cc)
for lane in ui-cancel linux-reset; do
  for kind in source inputs recipe; do
    sha256sum -c "$guests/$lane/$kind-before.sha256" > "$output/$lane-$kind-before-check.log"
  done
done
sha256sum "$0" "$rom" "$stage/native/cdk2-coreboot-image.elf" "$stage/fresh-build.json" \
  "$ui/pflash.rom" "$linux/pflash.rom" > "$output/replay-inputs-before.sha256"
# Current fresh source path, no historical observer/Kconfig reuse admission.
"${envbase[@]}" python3 -B "$firmware/util/qemu/bin/assert-default-zero-setup-run.py" \
  "$ui" default-zero-control-cancel > "$output/ui-saved.log" 2>&1
"${envbase[@]}" python3 -B "$firmware/util/qemu/bin/assert-default-zero-setup-run.py" \
  "$linux" default-zero-linux-fw-ui-reset > "$output/linux-saved.log" 2>&1
# Complete real source-derived media history + existing hostile mutations.
"${envbase[@]}" CDK2_NORMAL_FWUI_INPUT_RECEIPTS="$output/ui-codec-inputs" \
  bash "$firmware/util/qemu/bin/assert-protected-setup-store.sh" \
  "$rom" "$ui/pflash.rom" "$output/ui-media" --normal-system-fmp-ui-cancel \
  > "$output/ui-fullmedia.log" 2>&1
"${envbase[@]}" bash "$firmware/util/qemu/bin/assert-linux-fw-ui-reset-store.sh" \
  "$rom" "$linux/pflash.rom" "$output/linux-media" > "$output/linux-fullmedia.log" 2>&1
sha256sum -c "$output/replay-inputs-before.sha256" > "$output/replay-inputs-after-check.log"
for lane in ui-cancel linux-reset; do
  for kind in source inputs recipe; do
    sha256sum -c "$guests/$lane/$kind-before.sha256" > "$output/$lane-$kind-after-check.log"
  done
done
test "$(git -C "$firmware" rev-parse HEAD)" = "$expected"
test -z "$(git -C "$firmware" status --porcelain)"
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test -z "$(git -C "$producer" status --porcelain)"
