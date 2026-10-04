#!/bin/bash
set -euo pipefail
export GIT_OPTIONAL_LOCKS=0
test "$#" = 1
receipt=$1
case "$receipt" in /home/sean/coreboot-vendor-promotion.*) ;; *) exit 2;; esac
suffix=${receipt#/home/sean/coreboot-vendor-promotion.}
[[ $suffix =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]
test ! -e "$receipt" && test ! -L "$receipt"
recipe=/home/sean/coreboot-vendor-recovery-allrefs-proposal.FJaETV
recovery=/home/sean/Documents/cdk2-validation/retained-inputs/vboot-dd38-recovery.git
vboot_source=/home/sean/Documents/coreboot/.git/worktrees/authvar-presence-final-publication/modules/vboot
vboot_target=/home/sean/Documents/coreboot/.git/modules/vboot
vboot_oid=dd38e912b39166975ed13e46602d8a5b2a6e85f4
test ! -e "$recovery" && test ! -L "$recovery"
mkdir "$receipt"
printf '1\n' > "$receipt/aggregate.status"
stage()
{
    local label=$1 status=0
    shift
    printf '%q ' "$@" > "$receipt/$label.command"
    printf '\n' >> "$receipt/$label.command"
    "$@" > "$receipt/$label.log" 2>&1 || status=$?
    printf '%s\n' "$status" > "$receipt/$label.status"
    return "$status"
}
finish()
{
    local original=$? closure=0 target source oid
    trap - EXIT
    set +e
    snapshot > "$receipt/state-after.txt" || closure=1
    cmp "$receipt/state-before.txt" "$receipt/state-after.txt" > "$receipt/state-check.log" 2>&1 || closure=1
    sha256sum --quiet -c "$receipt/recipe-before.sha256" > "$receipt/recipe-check.log" 2>&1 || closure=1
    printf '%s\n' "$original" > "$receipt/execution.status"
    printf '%s\n' "$closure" > "$receipt/closure.status"
    if test "$original" = 0 && test "$closure" = 0; then
        printf '0\n' > "$receipt/aggregate.status"
        exit 0
    fi
    exit 1
}
mapfile -t records < "$recipe/PROMOTIONS.tsv"
snapshot()
{
    (
    set -e
    local target source oid repo
    for repo in "$vboot_source" "$vboot_target"; do
        printf 'REPO %s\n' "$repo"
        git --git-dir "$repo" rev-parse HEAD || exit 1
        git --git-dir "$repo" status --porcelain=v1 --untracked-files=all || exit 1
        if test -f "$repo/shallow"; then sha256sum "$repo/shallow" || exit 1; else printf 'NO_SHALLOW\n'; fi
    done
    for row in "${records[@]}"; do
        IFS=$'\t' read -r target source oid <<< "$row"
        for repo in "$target" "$source"; do
            printf 'REPO %s\n' "$repo"
            git --git-dir "$repo" rev-parse HEAD || exit 1
            git --git-dir "$repo" status --porcelain=v1 --untracked-files=all || exit 1
            if test -f "$repo/shallow"; then sha256sum "$repo/shallow" || exit 1; else printf 'NO_SHALLOW\n'; fi
        done
    done
    )
}
sha256sum "$recipe/preserve.sh" "$recipe/README.txt" "$recipe/PROMOTIONS.tsv" > "$receipt/recipe-before.sha256"
snapshot > "$receipt/state-before.txt"
trap finish EXIT
for tool in git sha256sum cmp cp grep mkdir bash find sort xargs readlink; do
    path=$(type -P "$tool")
    sha256sum "$(readlink -f "$path")" >> "$receipt/tools-before.sha256"
done
for tool in git-upload-pack git-index-pack git-unpack-objects; do
    path="$(git --exec-path)/$tool"
    test -f "$path"
    sha256sum "$(readlink -f "$path")" >> "$receipt/tools-before.sha256"
done
test "$(git --git-dir "$vboot_source" rev-parse --is-shallow-repository)" = true
test "$(git --git-dir "$vboot_target" rev-parse --is-shallow-repository)" = false
test "$(git --git-dir "$vboot_source" rev-parse refs/heads/main)" = "$vboot_oid"
git --git-dir "$vboot_source" cat-file -e "$vboot_oid^{commit}"
cp "$vboot_source/shallow" "$receipt/vboot-source-shallow.txt"
# File transport, not a local object hardlink/alternate borrowing clone.
mkdir "$receipt/empty-template"
stage vboot-clone git -c protocol.file.allow=always clone --template="$receipt/empty-template" --bare --single-branch --branch main --no-tags --no-local --no-hardlinks "file://$vboot_source" "$recovery"
test "$(git --git-dir "$recovery" rev-parse --is-bare-repository)" = true
test "$(git --git-dir "$recovery" rev-parse --is-shallow-repository)" = true
test ! -e "$recovery/objects/info/alternates"
test ! -e "$recovery/objects/info/http-alternates"
test -z "$(git --git-dir "$recovery" config --get-regexp 'extensions.partialClone|remote\..*\.promisor' || true)"
test "$(git --git-dir "$recovery" rev-parse HEAD)" = "$vboot_oid"
test "$(git --git-dir "$recovery" rev-parse refs/heads/main)" = "$vboot_oid"
grep -Fxq "$vboot_oid" "$recovery/shallow"
stage vboot-recovery-ref git --git-dir "$recovery" update-ref "refs/archive/retired-vendor-$vboot_oid" "$vboot_oid" 0000000000000000000000000000000000000000
stage vboot-connectivity git --git-dir "$recovery" fsck --connectivity-only --no-dangling
cp "$recovery/shallow" "$receipt/vboot-recovery-shallow.txt"
git --git-dir "$recovery" for-each-ref --format='%(refname) %(objectname)' > "$receipt/vboot-recovery-refs.txt"
sha256sum "$recovery/shallow" "$recovery/config" > "$receipt/vboot-recovery-metadata.sha256"
find "$recovery" -type f -print0 | sort -z | xargs -0 sha256sum > "$receipt/vboot-recovery-all-files.sha256"
count=0
for row in "${records[@]}"; do
    IFS=$'\t' read -r target source oid <<< "$row"
    [[ $oid =~ ^[0-9a-f]{40}$ ]]
    test "$(git --git-dir "$source" rev-parse --is-shallow-repository)" = false
    test "$(git --git-dir "$target" rev-parse --is-shallow-repository)" = false
    git --git-dir "$source" cat-file -e "$oid^{commit}"
    ref=refs/archive/retired-vendor-$oid
    if git --git-dir "$target" show-ref --verify --quiet "$ref"; then
        test "$(git --git-dir "$target" rev-parse "$ref")" = "$oid"
    else
        count=$((count + 1))
        stage "fetch-$count" git -c protocol.file.allow=always --git-dir "$target" fetch --no-tags --no-write-fetch-head "$source" "$oid:$ref"
    fi
    test "$(git --git-dir "$target" rev-parse "$ref")" = "$oid"
    test "$(git --git-dir "$target" rev-parse --is-shallow-repository)" = false
    printf '%s\t%s\t%s\n' "$target" "$ref" "$oid" >> "$receipt/promoted-refs.tsv"
done
test "$(git --git-dir "$vboot_target" rev-parse --is-shallow-repository)" = false
sha256sum --quiet -c "$receipt/tools-before.sha256" > "$receipt/tools-after-check.log"
sha256sum --quiet -c "$receipt/vboot-recovery-all-files.sha256" > "$receipt/vboot-recovery-after-check.log"
printf 'Exact local recovery only; vboot tip dd38 is shallow; its missing parent341fc8 is NOT reconstructed.\nCanonical vboot remains nonshallow and unchanged in HEAD/status/shallow metadata.\nNo network fetch, checkout, branch overwrite, source/config change or deletion.\n' > "$receipt/QUALIFICATION.txt"
