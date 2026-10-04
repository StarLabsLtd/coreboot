#!/bin/bash
set -euo pipefail
test "$#" = 2 || { echo 'usage: run.sh NEW_DEFAULT_RECEIPT PUBLISHED_SIGNED_HEAD' >&2; exit 2; }
test -z "${PYTHONOPTIMIZE+x}"
test -z "$(env | sed -n '/^CDK2_/p')"
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export HOME=/home/sean LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
export MAKEFLAGS= MFLAGS= MAKEOVERRIDES=
expected=$2
[[ $expected =~ ^[0-9a-f]{40}$ ]]
test "$expected" = 58598b47ab331eabb271f2343f6b0d4d882d30d1
receipt=$1
case "$receipt" in /home/sean/whole-default-after689.*) ;; *) exit 2 ;; esac
suffix=${receipt#/home/sean/whole-default-after689.}
[[ $suffix =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]
test ! -e "$receipt" && test ! -L "$receipt"
mkdir -- "$receipt"
printf '1\n' > "$receipt/aggregate.status"
recipe=/home/sean/whole-regression-after689-recipe.i9xraY
source=/home/sean/Documents/.cdk2-worktrees/whole-regression-after685-host
canonical=/home/sean/Documents/cdk2
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
mbedtls=$producer/3rdparty/mbedtls
build=$receipt/build
roots=("$source" "$canonical" "$producer" "$mbedtls" "$source/3rdparty/bearssl" "$source/3rdparty/lvgl")
tool_names=(bash sh git ssh-keygen make cc gcc ld objcopy objdump nm ar python3 kconfig-conf awk sha256sum patch
    mkfs.vfat sgdisk mcopy mmd mdir mdel mmove openssl sbsign sbverify sbattach
    jq truncate wc ln head cmp readlink env mkdir mktemp rm stat cp cut grep sed sort
    perl dirname realpath find xargs tar touch sleep diff basename tr uniq)
resolved_tools()
{
    local tool alias actual
    for tool in "${tool_names[@]}"; do
        alias=$(type -P "$tool") || return
        actual=$(readlink -f "$alias") || return
        test -f "$actual" && test -x "$actual" || return
        printf '%s\t%s\t%s\n' "$tool" "$alias" "$actual"
    done
}
checker_state()
{
    local path
    for path in "$source/.checkpatch.conf" /home/sean/.checkpatch.conf "$source/.scripts/.checkpatch.conf"; do
        if test -e "$path" || test -L "$path"; then
            test -f "$path" && test ! -L "$path"
            sha256sum "$path"
        else
            printf 'absent %s\n' "$path"
        fi
    done
}
inventory_ready=0
finish()
{
    local original=$? closure=0 tree
    trap - EXIT
    set +e
    printf '%s\n' "$original" > "$receipt/execution.status"
    if test "$inventory_ready" = 1; then
        python3 "$recipe/hash-inputs.py" "${unique_inputs[@]}" > "$receipt/inputs-after.json" || closure=1
        cmp "$receipt/inputs-before.json" "$receipt/inputs-after.json" > "$receipt/inputs-check.log" 2>&1 || closure=1
        resolved_tools > "$receipt/tools-resolved-after.tsv" || closure=1
        cmp "$receipt/tools-resolved-before.tsv" "$receipt/tools-resolved-after.tsv" > "$receipt/tools-resolution-check.log" 2>&1 || closure=1
        checker_state > "$receipt/checker-config-after.txt" || closure=1
        cmp "$receipt/checker-config-before.txt" "$receipt/checker-config-after.txt" > "$receipt/checker-config-check.log" 2>&1 || closure=1
        for tree in "${roots[@]}"; do
            git -C "$tree" rev-parse HEAD
            test -z "$(git -C "$tree" status --porcelain --untracked-files=all)" || closure=1
        done > "$receipt/heads-after.txt"
        cmp "$receipt/heads-before.txt" "$receipt/heads-after.txt" > "$receipt/heads-check.log" 2>&1 || closure=1
    else
        closure=1
        printf 'Initial finite input inventory incomplete; no qualified closure\n' > "$receipt/inputs-check.log"
    fi
    if test -f "$receipt/config-before.sha256"; then
        sha256sum -c "$receipt/config-before.sha256" > "$receipt/config-after-check.log" 2>&1 || closure=1
    fi
    printf '%s\n' "$closure" > "$receipt/closure.status"
    if test "$original" = 0 && test "$closure" = 0; then
        printf '0\n' > "$receipt/aggregate.status"
        exit 0
    fi
    exit 1
}
trap finish EXIT
stage()
{
    local label=$1 status=0
    shift
    printf '%q ' "$@" > "$receipt/$label.command"
    printf '\n' >> "$receipt/$label.command"
    /usr/bin/time -o "$receipt/$label.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
        "$@" > "$receipt/$label.log" 2>&1 || status=$?
    printf '%s\n' "$status" > "$receipt/$label.status"
    return "$status"
}
test "$(git -C "$source" rev-parse HEAD)" = "$expected"
test "$(git -C "$canonical" rev-parse HEAD)" = "$expected"
stage canonical-signature git -C "$canonical" verify-commit "$expected"
stage signature git -C "$source" verify-commit "$expected"
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
stage producer-signature git -C "$producer" verify-commit HEAD
test "$(git -C "$mbedtls" rev-parse HEAD)" = 0bebf8b8c7f07abe3571ded48a11aa907a1ffb20
test "$(git -C "$source/3rdparty/bearssl" rev-parse HEAD)" = 8ef7680081c61b486622f2d983c0d3d21e83caad
test "$(git -C "$source/3rdparty/lvgl" rev-parse HEAD)" = 85aa60d18b3d5e5588d7b247abf90198f07c8a63
for tree in "$mbedtls" "$source/3rdparty/bearssl" "$source/3rdparty/lvgl"; do
    test -e "$tree/.git"
done
for tree in "${roots[@]}"; do
    test -z "$(git -C "$tree" status --porcelain --untracked-files=all)"
    git -C "$tree" rev-parse HEAD
done > "$receipt/heads-before.txt"
inputs=("${roots[@]}"
    "$recipe/normal.sh" "$recipe/default.sh" "$recipe/hash-inputs.py" "$recipe/README.txt" /usr/bin/time)
resolved_tools > "$receipt/tools-resolved-before.tsv"
while IFS=$'\t' read -r tool alias actual; do inputs+=("$actual"); done < "$receipt/tools-resolved-before.tsv"
for support in cc1 as ld collect2 lto-wrapper; do
    selected=$(gcc "-print-prog-name=$support")
    case "$selected" in /*) ;; *) selected=$(type -P "$selected") ;; esac
    test -f "$selected"
    selected=$(readlink -f "$selected")
    printf '%s\t%s\n' "$support" "$selected" >> "$receipt/compiler-support.tsv"
    inputs+=("$selected")
done
for support in libgcc.a liblto_plugin.so libasan.so libubsan.so libtsan.so; do
    selected=$(gcc "-print-file-name=$support")
    test -f "$selected"
    selected=$(readlink -f "$selected")
    printf '%s\t%s\n' "$support" "$selected" >> "$receipt/compiler-support.tsv"
    inputs+=("$selected")
done
mapfile -t unique_inputs < <(printf '%s\n' "${inputs[@]}" | sort -u)
printf '%s\n' "${unique_inputs[@]}" > "$receipt/selected-inputs.txt"
python3 "$recipe/hash-inputs.py" "${unique_inputs[@]}" > "$receipt/inputs-before.json"
checker_state > "$receipt/checker-config-before.txt"
inventory_ready=1
mkdir "$receipt/tmp"
common=(env -i PATH="$PATH" HOME=/home/sean TMPDIR="$receipt/tmp" LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
    make -C "$source" -j4 -k CDK2_BUILD_DIR="$build" CDK2_DEFCONFIG="$source/defconfig"
    COREBOOT_CONFIG= COREBOOT_TREE="$producer" MBEDTLS_SOURCE="$mbedtls"
    CC=gcc HOSTCC=gcc)
stage configure "${common[@]}" defconfig
test ! -s "$build/kconfig/coreboot-source.tmp"
test "$(head -n 1 "$build/kconfig/coreboot-input.identity")" = standalone
sha256sum "$build/.config" "$build/include/cdk2/config.h" \
    "$build/kconfig/coreboot-source.tmp" "$build/kconfig/coreboot-input.identity" > "$receipt/config-before.sha256"
stage regression "${common[@]}" review-profile-check check native-stage
