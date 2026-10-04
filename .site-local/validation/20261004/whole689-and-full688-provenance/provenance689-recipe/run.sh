#!/bin/bash
set -Eeuo pipefail
export LC_ALL=C TZ=UTC
[[ ! ${PYTHONOPTIMIZE+x} ]]
[[ -z $(env | sed -n '/^CDK2_/p') ]]
unset MAKEFLAGS MFLAGS MAKEOVERRIDES
source_root=/home/sean/Documents/.cdk2-worktrees/uart-unique-authority-after684
expected_head=62352715633271454b9c2edc10c055c12fb5deef
expected_diff=1c308686b9a5e01b8799d7b2074e410ae9d15d88109f4e30d746291cf167a0e8
recipe=$(cd -- "$(dirname -- "$0")" && pwd -P)
output=${1:?new absolute absent receipt directory required}
[[ $output = /home/sean/review-profile-provenance-final.* && $output != *..* ]]
[[ ! -e $output && ! -L $output ]]
mkdir -- "$output"
mkdir -- "$output/tmp"
export TMPDIR="$output/tmp"
cd -- "$source_root"
[[ $(git rev-parse HEAD) = "$expected_head" ]]
git verify-commit HEAD > "$output/signature.log" 2>&1
git diff --binary > "$output/source-before.diff"
[[ $(sha256sum "$output/source-before.diff" | awk '{print $1}') = "$expected_diff" ]]
git diff --check
git status --porcelain --untracked-files=all > "$output/status-before.txt"
git rev-parse HEAD > "$output/head-before.txt"
tool_names=(bash sh python3 git ssh-keygen jq sha256sum patch awk sed grep cp cmp stat find sort xargs env mkdir mktemp dirname pwd cat tail wc tr curl date sleep chmod mv rm make head cut printf readlink)
snapshot_tools()
{
    local name resolved
    for name in "${tool_names[@]}"; do
        resolved=$(type -P "$name")
        [[ -n $resolved && -f $resolved ]]
        printf '%s\t%s\t%s\n' "$name" "$resolved" "$(readlink -f "$resolved")"
    done
}
snapshot_tools > "$output/resolver-before.txt"
mapfile -t tool_paths < <(cut -f2 "$output/resolver-before.txt" | sort -u)
python3 "$recipe/hash-inputs.py" "$source_root" > "$output/inputs-before.json"
python3 "$recipe/hash-inputs.py" "${tool_paths[@]}" > "$output/tools-before.json"
sha256sum "$recipe/run.sh" "$recipe/hash-inputs.py" "$recipe/README.txt" > "$output/recipe.sha256"
finish()
{
    local raw=$? closure=0 aggregate comparison pair suffix
    trap - EXIT
    set +e
    printf '%s\n' "$raw" > "$output/execution.status"
    python3 "$recipe/hash-inputs.py" "$source_root" > "$output/inputs-after.json" || closure=1
    python3 "$recipe/hash-inputs.py" "${tool_paths[@]}" > "$output/tools-after.json" || closure=1
    snapshot_tools > "$output/resolver-after.txt" || closure=1
    git diff --binary > "$output/source-after.diff" || closure=1
    git status --porcelain --untracked-files=all > "$output/status-after.txt" || closure=1
    git rev-parse HEAD > "$output/head-after.txt" || closure=1
    for pair in inputs tools resolver source status head; do
        case $pair in inputs|tools) suffix=json;; source) suffix=diff;; *) suffix=txt;; esac
        cmp "$output/$pair-before.$suffix" "$output/$pair-after.$suffix" > "$output/$pair-comparison.log" 2>&1
        comparison=$?
        printf '%s\n' "$comparison" > "$output/$pair-comparison.status"
        [[ $comparison = 0 ]] || closure=1
    done
    sha256sum -c "$output/recipe.sha256" > "$output/recipe-check.log" 2>&1 || closure=1
    git diff --check > "$output/diff-check.log" 2>&1 || closure=1
    printf '%s\n' "$closure" > "$output/closure.status"
    aggregate=0
    [[ $raw = 0 && $closure = 0 ]] || aggregate=1
    printf '%s\n' "$aggregate" > "$output/aggregate.status"
    exit "$aggregate"
}
trap finish EXIT
stage()
{
    local name=$1 raw
    shift
    set +e
    (set -e; "$@") > "$output/$name.log" 2>&1
    raw=$?
    set -e
    printf '%s\n' "$raw" > "$output/$name.status"
    return "$raw"
}
patch_proof()
{
    cp util/lint/checkpatch.pl "$output/reconstructed-upstream.pl"
    patch --reverse --fuzz=0 --batch --no-backup-if-mismatch --reject-file=/dev/null \
        "$output/reconstructed-upstream.pl" review-profile/coreboot/checkpatch-local.patch
    [[ $(sha256sum "$output/reconstructed-upstream.pl" | awk '{print $1}') = 945a529e4a1e7183ede7e025b87cee4b50d28b7b642406f9502b1a2392dce18a ]]
    cp "$output/reconstructed-upstream.pl" "$output/reconstructed-local.pl"
    patch --forward --fuzz=0 --batch --no-backup-if-mismatch --reject-file=/dev/null \
        "$output/reconstructed-local.pl" review-profile/coreboot/checkpatch-local.patch
    [[ $(sha256sum "$output/reconstructed-local.pl" | awk '{print $1}') = 712c2421bac764ab65d084a2f0901fa3d5e254ea2ef3065f8fa7ab368b50323b ]]
    cmp util/lint/checkpatch.pl "$output/reconstructed-local.pl"
    sha256sum "$output/reconstructed-upstream.pl" "$output/reconstructed-local.pl" > "$output/patch-output.sha256"
}
corpus_proof()
{
    local file digest
    for file in evidence.jsonl checkpoint.jsonl source-records.jsonl observations.md; do
        git show "$expected_head:review-profile/coreboot/generated/$file" > "$output/original-$file"
        cmp "$output/original-$file" "review-profile/coreboot/generated/$file"
    done
    git show "$expected_head:review-profile/coreboot/generated/manifest.json" | jq -S 'del(.input_sha256)' > "$output/manifest-old-body.json"
    jq -S 'del(.input_sha256)' review-profile/coreboot/generated/manifest.json > "$output/manifest-current-body.json"
    cmp "$output/manifest-old-body.json" "$output/manifest-current-body.json"
    digest=$(jq -cjnS --slurpfile c review-profile/coreboot/config.json \
        --slurpfile s review-profile/coreboot/sources.json \
        --arg tool_sha256 "$(sha256sum util/review_profile/coreboot-review-profile | awk '{print $1}')" \
        '{config:$c[0],sources:$s[0],tool_sha256:$tool_sha256}' | sha256sum | awk '{print $1}')
    printf '%s\n' "$digest" > "$output/computed-input-digest.txt"
    [[ $digest = $(jq -er .input_sha256 review-profile/coreboot/generated/manifest.json) ]]
}
old_pin_opposition()
{
    local sandbox="$output/old-pin-root" path raw
    mkdir -p "$sandbox/util/review_profile" "$sandbox/review-profile/coreboot/generated" "$sandbox/bin"
    while IFS= read -r path; do
        mkdir -p "$sandbox/$(dirname "$path")"
        cp "$source_root/$path" "$sandbox/$path"
    done < <(git ls-files util/review_profile review-profile/coreboot)
    mkdir -p "$sandbox/util/lint"
    cp util/lint/checkpatch.pl "$sandbox/util/lint/checkpatch.pl"
    git show "$expected_head:review-profile/coreboot/sources.json" > "$sandbox/review-profile/coreboot/sources.json"
    git show "$expected_head:review-profile/coreboot/checkpatch-local.patch" > "$sandbox/review-profile/coreboot/checkpatch-local.patch"
    printf '#!/bin/sh\nprintf "forbidden external fetch\\n" >&2\nexit 99\n' > "$sandbox/bin/curl"
    chmod +x "$sandbox/bin/curl"
    find "$sandbox" -type f -print0 | sort -z | xargs -0 sha256sum > "$output/old-pin-inputs.sha256"
    set +e
    PATH="$sandbox/bin:$PATH" "$sandbox/util/review_profile/coreboot-review-profile" update > "$output/old-pin.log" 2>&1
    raw=$?
    set -e
    printf '%s\n' "$raw" > "$output/old-pin.status"
    [[ $raw = 1 ]]
    [[ $(wc -l < "$output/old-pin.log") = 1 ]]
    grep -Fx 'pinned local coreboot patch or result digest differs' "$output/old-pin.log"
    sha256sum -c "$output/old-pin-inputs.sha256"
}
stage patch-proof patch_proof
stage unchanged-corpus corpus_proof
stage old-pin-opposition old_pin_opposition
stage existing-review-profile-tests tests/review-profile-test
stage current-corpus-verify util/review_profile/coreboot-review-profile verify
sha256sum -c "$output/patch-output.sha256" > "$output/patch-output-check.log"
