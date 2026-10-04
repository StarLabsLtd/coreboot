#!/bin/bash
set -euo pipefail
export LANG=C LC_ALL=C
fw=/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675
out=/home/sean/roadmap-after683-text-check.bz5Ezu/receipt
test ! -e "$out"
mkdir "$out"
cd "$fw"
test "$(git rev-parse HEAD)" = 59225e6c7e7c0ff4f80984a0a291dd3cd0121856
git diff --binary > "$out/source-before.diff"
test "$(sha256sum "$out/source-before.diff" | cut -d' ' -f1)" = 47a4f80f642a6c9bc4f4895dcff552b3741399cfeea0a61559428a65b56882c9
python3 /home/sean/roadmap-after683-text-check.bz5Ezu/hash-inputs.py "$fw" > "$out/source-before.json"
sha256sum /home/sean/roadmap-after683-text-check.bz5Ezu/run.sh /home/sean/roadmap-after683-text-check.bz5Ezu/hash-inputs.py > "$out/recipe-before.sha256"
for tool in bash sh python3 git awk sed grep sha256sum cut xargs cmp mkdir rm dirname mktemp uname; do
 command -v "$tool" >> "$out/tools-before.txt"
done
xargs sha256sum < "$out/tools-before.txt" > "$out/tools-before.sha256"
git diff --check
for name in 021-coreboot-lowercase 016-non-ascii 003-whitespace 000-license-headers; do
 status=0
 sh "util/lint/lint-stable-$name" > "$out/$name.stdout" 2> "$out/$name.stderr" || status=$?
 printf '%s\n' "$status" > "$out/$name.status"
 test "$status" = 0
 test ! -s "$out/$name.stdout"
done
git diff --binary > "$out/source-after.diff"
cmp "$out/source-before.diff" "$out/source-after.diff"
python3 /home/sean/roadmap-after683-text-check.bz5Ezu/hash-inputs.py "$fw" > "$out/source-after.json"
cmp "$out/source-before.json" "$out/source-after.json" > "$out/source-check.log" 2>&1
sha256sum --quiet -c "$out/recipe-before.sha256" > "$out/recipe-check.log" 2>&1
sha256sum --quiet -c "$out/tools-before.sha256" > "$out/tools-check.log" 2>&1
for tool in bash sh python3 git awk sed grep sha256sum cut xargs cmp mkdir rm dirname mktemp uname; do
 command -v "$tool" >> "$out/tools-after.txt"
done
cmp "$out/tools-before.txt" "$out/tools-after.txt"
test "$(git rev-parse HEAD)" = 59225e6c7e7c0ff4f80984a0a291dd3cd0121856
printf '0\n' > "$out/aggregate.status"
