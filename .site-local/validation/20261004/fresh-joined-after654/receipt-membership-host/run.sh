#!/usr/bin/env bash
set -euo pipefail
receipt=/home/sean/fresh-receipt-membership-final.rlhoF3
source=/home/sean/Documents/.cdk2-worktrees/fresh-receipt-membership-after653
firmware=/home/sean/Documents/cdk2
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
stage=/home/sean/fresh-normal-after653.noNGMS/build
expected=8229ed31abca6017dec0c5877646f8bc43dbca71
freeze=f81f48326d499689736acc7ce35672ed4458872337c153e59395de8f5dd8ca3c
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean LANG=C LC_ALL=C
unset PYTHONOPTIMIZE
ulimit -c 0
trap 'printf "%s\n" "$?" > "$receipt/aggregate.status"' EXIT
cd "$source"
test "$(git rev-parse HEAD)" = "$expected"
test "$(git diff --binary | sha256sum | cut -d' ' -f1)" = "$freeze"
git diff --binary > "$receipt/frozen.diff"
git status --porcelain > "$receipt/status-before.txt"
git rev-parse HEAD > "$receipt/head-before.txt"
git -C "$firmware" rev-parse HEAD > "$receipt/canonical-head-before.txt"
git -C "$producer" rev-parse HEAD > "$receipt/producer-head-before.txt"
git -C "$firmware" verify-commit "$expected" > "$receipt/canonical-signature.log" 2>&1
git ls-files --stage | awk '$1 == "100644" || $1 == "100755" {sub(/^[^\t]*\t/, ""); print}' |
  while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
for tool in python3 bash git sha256sum awk xargs sort cmp wc cut time; do
  sha256sum "$(realpath -e "$(type -P "$tool")")"
done > "$receipt/tools-before.sha256"
sha256sum "$receipt/run.sh" "$receipt/replay-actual.py" > "$receipt/recipe-before.sha256"
for input in "$stage/source-before.sha256" "$stage/initial9/source-before.sha256" "$stage/outputs.sha256"; do
  sha256sum "$input"
done > "$receipt/consumed-receipts-before.sha256"
sort -u "$stage/source-before.sha256" "$stage/initial9/source-before.sha256" \
  "$stage/outputs.sha256" > "$receipt/consumed-inputs-before.sha256"
sha256sum "$stage/input.config" "$stage/producer-input.config" "$stage/resolved.config" \
  "$stage/include/cdk2/config.h" "$stage/initial9/input.config" "$stage/initial9/full.config" \
  >> "$receipt/consumed-inputs-before.sha256"
wc -l "$stage/source-before.sha256" "$stage/initial9/source-before.sha256" \
  "$stage/outputs.sha256" > "$receipt/actual-row-counts.txt"
aggregate=0
run_gate()
{
  local name=$1 status=0
  shift
  /usr/bin/time -o "$receipt/$name.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
    "$@" > "$receipt/$name.log" 2>&1 || status=$?
  printf '%s\n' "$status" > "$receipt/$name.status"
  if ((status != 0)); then aggregate=1; fi
}
run_gate fresh python3 -B tests/fresh_normal_fwui_admission_test.py
run_gate historical python3 -B tests/normal_fwui_admission_test.py
run_gate actual-canonical python3 -B "$receipt/replay-actual.py" "$source" "$stage/source-before.sha256" "$firmware"
run_gate actual-producer python3 -B "$receipt/replay-actual.py" "$source" "$stage/initial9/source-before.sha256" "$producer"
run_gate actual-native python3 -B "$receipt/replay-actual.py" "$source" "$stage/outputs.sha256" "$firmware"
sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log"
sha256sum -c "$receipt/tools-before.sha256" > "$receipt/tools-after-check.log"
sha256sum -c "$receipt/recipe-before.sha256" > "$receipt/recipe-after-check.log"
sha256sum -c "$receipt/consumed-receipts-before.sha256" > "$receipt/consumed-receipts-after-check.log"
sha256sum -c "$receipt/consumed-inputs-before.sha256" > "$receipt/consumed-inputs-after-check.log"
git status --porcelain > "$receipt/status-after.txt"
git rev-parse HEAD > "$receipt/head-after.txt"
git -C "$firmware" rev-parse HEAD > "$receipt/canonical-head-after.txt"
git -C "$producer" rev-parse HEAD > "$receipt/producer-head-after.txt"
for kind in status head canonical-head producer-head; do cmp "$receipt/$kind-before.txt" "$receipt/$kind-after.txt"; done
test "$(git diff --binary | sha256sum | cut -d' ' -f1)" = "$freeze"
git diff --check > "$receipt/diff-check.log"
printf '0\n' > "$receipt/closure.status"
test "$aggregate" = 0
