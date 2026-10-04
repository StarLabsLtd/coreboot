#!/bin/bash
set -euo pipefail
receipt=/home/sean/fresh-normal-after653.noNGMS/ready-host
source=/home/sean/Documents/.cdk2-worktrees/fresh-producer-xcompile-after652-ready
mkdir "$receipt"
trap 'printf "%s\n" "$?" > "$receipt/aggregate.status"' EXIT
cd "$source"
test "$(git rev-parse HEAD)" = 8229ed31abca6017dec0c5877646f8bc43dbca71
test -z "$(git status --porcelain)"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' | xargs -d '\n' sha256sum > "$receipt/source-before.sha256"
sha256sum "$0" /usr/bin/python3.13 /usr/bin/bash > "$receipt/tools-before.sha256"
python3 -B tests/fresh_normal_fwui_admission_test.py > "$receipt/fresh.log" 2>&1
python3 -B tests/normal_fwui_admission_test.py > "$receipt/historical.log" 2>&1
sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log"
sha256sum -c "$receipt/tools-before.sha256" > "$receipt/tools-after-check.log"
test "$(git rev-parse HEAD)" = 8229ed31abca6017dec0c5877646f8bc43dbca71
test -z "$(git status --porcelain)"
