#!/bin/bash
set -euo pipefail
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
test -z "${PYTHONOPTIMIZE+x}"
recipe=/home/sean/stage-code-decorators-host-recipe.WecndG
source=/home/sean/Documents/.cdk2-worktrees/stage-code-decorators-after679
out=/home/sean/stage-code-decorators-final.20261004-r1
normal=/home/sean/fresh-normal-focus-after657.CftZZg/build
fixture=$source/tests/dxe_core_capsule_disk_stage_test.c
cd "$source"
test -f .checkpatch.conf && test ! -L .checkpatch.conf
test "$(git ls-files .checkpatch.conf)" = .checkpatch.conf
test ! -e "$out"
test "$(git -C "$source" rev-parse HEAD)" = a3ed5f42934cf4ff7f8171ba5a709c2c31f801a6
test "$(git -C "$source" diff --binary HEAD | sha256sum | cut -d ' ' -f1)" = 5baf7aa670d31225fd2eb295520e8fca7b73bf71da70a21f662e7cb3351c1021
mkdir "$out" "$out/tmp"
export TMPDIR=$out/tmp
printf '1\n' > "$out/aggregate.status"
git -C "$source" verify-commit HEAD > "$out/signature.log" 2>&1
git -C "$source" show HEAD:tests/dxe_core_capsule_disk_stage_test.c > "$out/old.c"
sha256sum --quiet -c <<'PINS'
ac668eb9c8a622e020182653fd832e6671741f5e616a01cfa4fb33b8e9b4065f  /home/sean/fresh-normal-focus-after657.CftZZg/build/include/cdk2/config.h
10f6aef02d7678a1ae8d5ffab8157b5ab0a37438576a645cdf237fe130fa2aac  /home/sean/fresh-normal-focus-after657.CftZZg/build/resolved.config
PINS
for symbol in CDK2_LINEAR_BOOT CDK2_COREBOOT_CAPSULE_PROFILE CDK2_NATIVE_SYSTEM_FMP PAYLOAD_DMA_HANDOFF CDK2_NATIVE_TCG2 CDK2_STRICT_DIRECT_RUNTIME; do
    grep -Fxq "#define CONFIG_$symbol 1" "$normal/include/cdk2/config.h"
done
inputs=("$source" "$normal/include/cdk2/config.h" "$normal/resolved.config" "$out/old.c")
tools=(bash sh cc gcc perl python3 git ssh-keygen sha256sum cut grep sed dirname mktemp mkdir cp cmp awk sort xargs objdump readelf ld as ar env readlink)
paths() {
    local tool path
    for tool in "${tools[@]}"; do
        path=$(type -P "$tool")
        printf '%s\t%s\t%s\n' "$tool" "$path" "$(readlink -f "$path")"
    done
}
paths > "$out/tools-before.tsv"
while IFS=$'\t' read -r tool path resolved; do inputs+=("$path"); done < "$out/tools-before.tsv"
inputs+=("$recipe/run.sh" "$recipe/hash-inputs.py" "$recipe/verify-tokens.pl" "$recipe/README.txt" /usr/bin/time /usr/bin/readlink)
for support in cc1 as ld collect2 lto-wrapper; do
    path=$(cc "-print-prog-name=$support")
    test -f "$path" || path=$(command -v "$path")
    inputs+=("$path")
done
for support in libgcc.a libasan.so libubsan.so; do
    path=$(cc "-print-file-name=$support")
    test -f "$path"
    inputs+=("$path")
done
python3 "$recipe/hash-inputs.py" "${inputs[@]}" > "$out/inputs-before.json"
git -C "$source" diff --binary HEAD > "$out/source-before.diff"
git -C "$source" rev-parse HEAD > "$out/head-before.txt"
execution=1
finish() {
    local original=$? closure=0 aggregate=1
    trap - EXIT
    set +e
    python3 "$recipe/hash-inputs.py" "${inputs[@]}" > "$out/inputs-after.json" || closure=1
    cmp "$out/inputs-before.json" "$out/inputs-after.json" > "$out/inputs-check.log" 2>&1 || closure=1
    git -C "$source" diff --binary HEAD > "$out/source-after.diff"
    git -C "$source" rev-parse HEAD > "$out/head-after.txt"
    cmp "$out/source-before.diff" "$out/source-after.diff" > "$out/diff-check.log" 2>&1 || closure=1
    cmp "$out/head-before.txt" "$out/head-after.txt" > "$out/head-check.log" 2>&1 || closure=1
    paths > "$out/tools-after.tsv"
    cmp "$out/tools-before.tsv" "$out/tools-after.tsv" > "$out/tools-check.log" 2>&1 || closure=1
    printf '%s\n' "$execution" > "$out/execution.status"
    printf '%s\n' "$closure" > "$out/closure.status"
    test "$original" = 0 && test "$execution" = 0 && test "$closure" = 0 && aggregate=0
    printf '%s\n' "$aggregate" > "$out/aggregate.status"
    exit "$aggregate"
}
trap finish EXIT
gate() {
    local label=$1 status=0; shift
    printf '%q ' "$@" > "$out/$label.command"
    printf '\n' >> "$out/$label.command"
    /usr/bin/time -p -o "$out/$label.time" "$@" > "$out/$label.log" 2>&1 || status=$?
    printf '%s\n' "$status" > "$out/$label.status"
    return "$status"
}
gate token-equivalence perl "$recipe/verify-tokens.pl" "$out/old.c" "$fixture"
checker=(perl "$source/util/lint/checkpatch.pl" --no-tree --typedefsfile "$source/util/lint/cdk2-typedefs.checkpatch" --show-types --file --quiet --max-line-length 96)
gate current-checkpatch "${checker[@]}" "$fixture"
old_status=0
gate old-checkpatch "${checker[@]}" "$out/old.c" || old_status=$?
test "$old_status" = 1
test "$(grep -c '^ERROR:COMPLEX_MACRO:' "$out/old-checkpatch.log")" = 3
test "$(grep -c '^ERROR:' "$out/old-checkpatch.log")" = 3
! grep -q '^WARNING:' "$out/old-checkpatch.log"
printf '0\n' > "$out/old-checkpatch-opposition.status"
gate actual-stage sh "$source/tests/dxe_core_capsule_disk_stage_test.sh" "$normal/include/cdk2/config.h" cc
artifact=$(sed -n 's/^Retained staging HOST artifacts: //p' "$out/actual-stage.log")
case "$artifact" in "$out/tmp/"*) ;; *) exit 1 ;; esac
test -f "$artifact/test"
objdump -t "$artifact/test" > "$out/symbols.txt"
for symbol in model_process model_validate model_scatter model_query model_update binding_supported binding_stop; do
    awk -v name="$symbol" '$4 == ".stage_runtime" && $NF == name { found++ } END { exit found != 1 }' "$out/symbols.txt"
done
for symbol in pci_supported pci_stop deactivate quiesce; do
    awk -v name="$symbol" '$4 == ".stage_pci" && $NF == name { found++ } END { exit found != 1 }' "$out/symbols.txt"
done
for symbol in xhci_supported xhci_stop xhci_quiesce; do
    awk -v name="$symbol" '$4 == ".stage_xhci" && $NF == name { found++ } END { exit found != 1 }' "$out/symbols.txt"
done
readelf -d "$artifact/test" > "$out/dynamic.txt"
grep -Fq libasan "$out/dynamic.txt"
grep -Fq libubsan "$out/dynamic.txt"
sha256sum "$artifact/test" "$artifact/mutant" > "$out/retained-outputs.sha256"
printf '0\n' > "$out/sections-and-sanitizers.status"
execution=0
