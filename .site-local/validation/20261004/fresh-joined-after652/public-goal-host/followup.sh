#!/usr/bin/env bash
# Separate final HOST recipe; preserve original receipt-only setup failure.
set -uo pipefail
owner=/home/sean/fresh-renderer-public-goal-host.UXyCdb
receipt=$owner/followup
source=/home/sean/Documents/.cdk2-worktrees/fresh-renderer-public-goal-after650
mkdir "$receipt" || exit 1
cd "$source" || exit 1
snapshot_tools()
{
    for tool in python3 bash git sha256sum awk xargs cmp realpath tee time; do
        alias=$(type -P "$tool") || return
        actual=$(realpath -e "$alias") || return
        printf 'TOOL\t%s\t%s\t%s\n' "$tool" "$alias" "$actual"
        sha256sum "$actual" || return
    done
}
git diff --binary | sha256sum > "$receipt/freeze-before.sha256"
git status --porcelain > "$receipt/status-before.txt"
git ls-files --stage | awk '$1 == "100644" || $1 == "100755" {sub(/^[^\t]*\t/, ""); print}' |
    xargs -d '\n' sha256sum > "$receipt/source-before.sha256"
sha256sum "$owner/followup.sh" "$owner/invocation-goal-model.py" > "$receipt/recipe-before.sha256"
snapshot_tools > "$receipt/tools-before.txt" || exit 1
status=0
for name in fresh_normal_fwui_admission_test normal_fwui_admission_test; do
    /usr/bin/time -p -o "$receipt/$name.time" python3 -B "tests/$name.py" > "$receipt/$name.log" 2>&1
    result=$?
    printf '%s=%s\n' "$name" "$result" | tee -a "$receipt/status"
    test "$result" = 0 || status=1
done
python3 -B "$owner/invocation-goal-model.py" > "$receipt/invocation-goal-model.log" 2>&1
result=$?
printf 'invocation-goal-model=%s\n' "$result" | tee -a "$receipt/status"
test "$result" = 0 || status=1
bash -n util/qemu/bin/build-normal-fwui-fresh.sh || status=1
python3 -B -c 'import ast, pathlib; ast.parse(pathlib.Path("util/qemu/bin/fresh_normal_fwui.py").read_text())' || status=1
git diff --check || status=1
sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log" || status=1
sha256sum -c "$receipt/recipe-before.sha256" > "$receipt/recipe-after-check.log" || status=1
snapshot_tools > "$receipt/tools-after.txt" || status=1
git diff --binary | sha256sum > "$receipt/freeze-after.sha256"
git status --porcelain > "$receipt/status-after.txt"
cmp "$receipt/freeze-before.sha256" "$receipt/freeze-after.sha256" || status=1
cmp "$receipt/status-before.txt" "$receipt/status-after.txt" || status=1
cmp "$receipt/tools-before.txt" "$receipt/tools-after.txt" || status=1
printf '%s\n' "$status" > "$receipt/aggregate.status"
printf 'HOST_AGGREGATE=%s\n' "$status"
exit "$status"
