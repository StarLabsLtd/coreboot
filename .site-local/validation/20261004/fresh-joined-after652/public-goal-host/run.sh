#!/usr/bin/env bash
# Released HOST source models only; no Make, compiler or guest invocation.
set -uo pipefail
receipt=/home/sean/fresh-renderer-public-goal-host.UXyCdb
source=/home/sean/Documents/.cdk2-worktrees/fresh-renderer-public-goal-after650
cd "$source" || exit 1
git diff --binary | sha256sum > "$receipt/freeze-before.sha256"
git status --porcelain > "$receipt/status-before.txt"
git ls-files --stage | awk '$1 == "100644" || $1 == "100755" {sub(/^[^\t]*\t/, ""); print}' |
    xargs -d '\n' sha256sum > "$receipt/source-before.sha256"
sha256sum "$receipt/run.sh" "$receipt/invocation-goal-model.py" > "$receipt/recipe-before.sha256"
for tool in python3 bash git sha256sum awk xargs cmp realpath tee time; do
    alias=$(command -v "$tool") || exit 1
    actual=$(realpath -e "$alias") || exit 1
    printf 'TOOL\t%s\t%s\t%s\n' "$tool" "$alias" "$actual"
    sha256sum "$actual" || exit 1
done > "$receipt/tools-before.txt"
status=0
for name in fresh_normal_fwui_admission_test normal_fwui_admission_test; do
    /usr/bin/time -p -o "$receipt/$name.time" python3 -B "tests/$name.py" > "$receipt/$name.log" 2>&1
    result=$?
    printf '%s=%s\n' "$name" "$result" | tee -a "$receipt/status"
    test "$result" = 0 || status=1
done
python3 -B "$receipt/invocation-goal-model.py" > "$receipt/invocation-goal-model.log" 2>&1
result=$?
printf 'invocation-goal-model=%s\n' "$result" | tee -a "$receipt/status"
test "$result" = 0 || status=1
bash -n util/qemu/bin/build-normal-fwui-fresh.sh || status=1
python3 -B -c 'import ast, pathlib; ast.parse(pathlib.Path("util/qemu/bin/fresh_normal_fwui.py").read_text())' || status=1
git diff --check || status=1
sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log" || status=1
sha256sum -c "$receipt/recipe-before.sha256" > "$receipt/recipe-after-check.log" || status=1
awk '/^[0-9a-f]{64}  / {print}' "$receipt/tools-before.txt" |
    sha256sum -c > "$receipt/tools-after-check.log" || status=1
for tool in python3 bash git sha256sum awk xargs cmp realpath tee time; do
    alias=$(command -v "$tool") || status=1
    actual=$(realpath -e "$alias") || status=1
    printf 'TOOL\t%s\t%s\t%s\n' "$tool" "$alias" "$actual"
    sha256sum "$actual" || status=1
done > "$receipt/tools-after.txt"
git diff --binary | sha256sum > "$receipt/freeze-after.sha256"
git status --porcelain > "$receipt/status-after.txt"
cmp "$receipt/freeze-before.sha256" "$receipt/freeze-after.sha256" || status=1
cmp "$receipt/status-before.txt" "$receipt/status-after.txt" || status=1
cmp "$receipt/tools-before.txt" "$receipt/tools-after.txt" || status=1
printf '%s\n' "$status" > "$receipt/aggregate.status"
printf 'HOST_AGGREGATE=%s\n' "$status"
exit "$status"
