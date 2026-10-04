#!/usr/bin/env bash
# One genuine Linux-origin request/reset VM on unchanged normal628 firmware.
set -euo pipefail
output=/home/sean/linux-fwui-reset-dual-qmp-native.wevWm6
source=/home/sean/Documents/.cdk2-worktrees/linux-origin-fwui-qmp-topology-aftere4cf
build=/home/sean/native-default-zero-hotkey-after628.CfqEvm
firmware=/home/sean/Documents/.cdk2-worktrees/default-zero-timeout-hotkey-after626
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
requester=/home/sean/linux-fwui-requester-recovery-after959f.u4JpHs/linux-fwui.efi
bundle=/home/sean/Documents/cdk2-validation/qemu/pr331-627e3bbe/qemu/fixtures
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean
unset PYTHONOPTIMIZE
ulimit -c 0
cd "$source"
test "$(git rev-parse HEAD)" = 4bb6c07b3091edceb86acbca3a793dcb8453a812
test -z "$(git status --porcelain=v1)"
git verify-commit HEAD > "$output/signature.log" 2>&1
git rev-parse HEAD > "$output/head-before.txt"
git status --porcelain=v1 > "$output/status-before.txt"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
 xargs -d '\n' sha256sum > "$output/source-before.sha256"
sha256sum "$output/run-native.sh" "$requester" "$requester.manifest" \
 "$build/native/cdk2-coreboot-image.elf" "$build/initial9/build/coreboot.rom" \
 > "$output/recipe-assets-before.sha256"
for tool in python3 bash git sha256sum awk xargs qemu-system-x86_64 cc perl ld as mcopy objcopy cpio; do
 sha256sum "$(command -v "$tool")"
done > "$output/tools-before.sha256"
sha256sum /usr/bin/time >> "$output/tools-before.sha256"
for tool in cc1 as ld; do
 sha256sum "$(command -v "$(cc -print-prog-name="$tool")")" >> "$output/tools-before.sha256"
done
finish()
{
 status=$?
 trap - EXIT
 set +e
 sha256sum -c "$output/source-before.sha256" > "$output/source-after-check.log" || status=1
 sha256sum -c "$output/recipe-assets-before.sha256" > "$output/recipe-assets-after-check.log" || status=1
 sha256sum -c "$output/tools-before.sha256" > "$output/tools-after-check.log" || status=1
 git rev-parse HEAD > "$output/head-after.txt"
 git status --porcelain=v1 > "$output/status-after.txt"
 cmp "$output/head-before.txt" "$output/head-after.txt" || status=1
 cmp "$output/status-before.txt" "$output/status-after.txt" || status=1
 printf '%s\n' "$status" > "$output/outer.status"
 exit "$status"
}
trap finish EXIT
# Outer420 covers immutable artifact admission, actual codec and cleanup.
# The one guest retains original180; neither endpoint submits a HOST reset.
/usr/bin/time -f 'WALL=%e USER=%U SYS=%S EXIT=%x' -o "$output/native.time" \
 python3 -B util/qemu/bin/run-deadline.py --kill-after 3 420 -- \
 python3 -B util/qemu/bin/run-linux-boot-to-fw-ui.py "$build" "$firmware" "$producer" \
 "$requester" "$bundle" "$output/lifecycle" > "$output/native.log" 2>&1
