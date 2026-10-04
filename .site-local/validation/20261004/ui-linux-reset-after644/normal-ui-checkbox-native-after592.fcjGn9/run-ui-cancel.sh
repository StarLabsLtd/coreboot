#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Exactly one fresh canonical-Linux guest on unchanged normal628 firmware.
# Checkbox/Height widget semantics require separate actual-frame review.
set -euo pipefail
stage=/home/sean/normal-ui-checkbox-native-after592.fcjGn9
source=/home/sean/Documents/.cdk2-worktrees/normal-ui-checkbox-native-after42044
firmware=/home/sean/Documents/.cdk2-worktrees/default-zero-timeout-hotkey-after626
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
build=/home/sean/native-default-zero-hotkey-after628.CfqEvm
bundle=/home/sean/Documents/cdk2-validation/qemu/pr331-627e3bbe/qemu/fixtures
unused_request_fixture=/home/sean/normal-fwui-native-aftere63.BRRN8B/requester
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean
unset PYTHONOPTIMIZE
ulimit -c 0
cd "$source"
test "$(git rev-parse HEAD)" = 592023e20bd7e84b69a789efcffc3ef7d9b0696d
test -z "$(git status --porcelain)"
git verify-commit HEAD > "$stage/signature.log" 2>&1
git rev-parse HEAD > "$stage/head-before.txt"
git status --porcelain > "$stage/status-before.txt"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	xargs -d '\n' sha256sum > "$stage/source-before.sha256"
sha256sum "$stage/run-ui-cancel.sh" > "$stage/recipe-before.sha256"
for tool in python3 bash git sha256sum awk xargs cc perl ld as mcopy; do
	sha256sum "$(command -v "$tool")"
done > "$stage/tools-before.sha256"
sha256sum /usr/bin/time >> "$stage/tools-before.sha256"
for tool in cc1 as ld; do
	sha256sum "$(command -v "$(cc -print-prog-name="$tool")")" >> "$stage/tools-before.sha256"
done
# Reuse the existing complete build/reference/source/Linux admissions and
# run_stage; the explicit UI phase never executes the retained positional EFI
# request fixture, seeds pflash, rebuilds Core/kernel, or launches prior guests.
# This420s outer guard covers admission/codec/cleanup, not a new guest limit.
# The one guest retains its original180 deadline and actual status receipts.
status=0
/usr/bin/time -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' -o "$stage/ui-cancel.time" \
	python3 -B util/qemu/bin/run-deadline.py --kill-after 3 420 -- \
	python3 -B util/qemu/bin/run-normal-boot-to-fw-ui.py "$build" "$firmware" "$producer" \
	"$unused_request_fixture" "$bundle" "$stage/lifecycle" --ui-control-cancel-only \
	> "$stage/ui-cancel.log" 2>&1 || status=$?
printf '%s\n' "$status" > "$stage/outer.status"
sha256sum -c "$stage/source-before.sha256" > "$stage/source-after-check.log"
sha256sum -c "$stage/recipe-before.sha256" > "$stage/recipe-after-check.log"
sha256sum -c "$stage/tools-before.sha256" > "$stage/tools-after-check.log"
git rev-parse HEAD > "$stage/head-after.txt"
git status --porcelain > "$stage/status-after.txt"
cmp "$stage/head-before.txt" "$stage/head-after.txt"
cmp "$stage/status-before.txt" "$stage/status-after.txt"
exit "$status"
