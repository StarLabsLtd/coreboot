#!/bin/bash
set -euo pipefail
receipt=/home/sean/fresh-normal-after654.nP6Vda/ready-host
source=/home/sean/Documents/.cdk2-worktrees/fresh-receipt-membership-after653-ready
mkdir "$receipt"
trap 'printf "%s\n" "$?" > "$receipt/aggregate.status"' EXIT
cd "$source"
test "$(git rev-parse HEAD)" = 0bb127b503d8c5d973b07d608f564ff0ef2ef6bb
test -z "$(git status --porcelain)"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' | xargs -d '\n' sha256sum > "$receipt/source-before.sha256"
sha256sum "$0" /usr/bin/python3.13 /usr/bin/bash /home/sean/fresh-receipt-membership-final.rlhoF3/replay-actual.py > "$receipt/tools-before.sha256"
python3 -B tests/fresh_normal_fwui_admission_test.py > "$receipt/fresh.log" 2>&1
python3 -B tests/normal_fwui_admission_test.py > "$receipt/historical.log" 2>&1
python3 -B /home/sean/fresh-receipt-membership-final.rlhoF3/replay-actual.py "$source" /home/sean/fresh-normal-after653.noNGMS/build/outputs.sha256 /home/sean/Documents/cdk2 > "$receipt/actual-native.log" 2>&1
sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log"
sha256sum -c "$receipt/tools-before.sha256" > "$receipt/tools-after-check.log"
test "$(git rev-parse HEAD)" = 0bb127b503d8c5d973b07d608f564ff0ef2ef6bb
test -z "$(git status --porcelain)"
