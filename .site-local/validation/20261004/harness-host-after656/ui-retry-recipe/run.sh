#!/bin/bash
set -euo pipefail
test "$#" = 1 || { echo 'usage: run.sh NEW_RECEIPT_AND_BUILD_DIRECTORY' >&2; exit 2; }
test -z "${PYTHONOPTIMIZE+x}"
test -z "$(env | sed -n '/^CDK2_/p')"
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export LANG=C LC_ALL=C TMPDIR=/home/sean PYTHONDONTWRITEBYTECODE=1
fw=/home/sean/Documents/.cdk2-worktrees/lvgl-keyboard-focus-after654
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
canonical=/home/sean/Documents/cdk2
original=/home/sean/fresh-normal-after654.nP6Vda/build
recipe=$(cd -- "$(dirname -- "$0")" && pwd -P)
out=$1
test ! -e "$out"
mkdir -p "$out"
out=$(realpath "$out") build=$out/build
mkdir "$build"
printf '1\n' > "$out/aggregate.status"
test "$(git -C "$fw" rev-parse HEAD)" = 0bb127b503d8c5d973b07d608f564ff0ef2ef6bb
test "$(git -C "$fw" diff --binary | sha256sum | cut -d' ' -f1)" = 15609eb42abac33ab4ccf1fd125b6ff839bbe2f0915d18aedb757e9139d33401
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test -z "$(git -C "$producer" status --porcelain --untracked-files=normal)"
sha256sum "$recipe"/* > "$out/recipe-before.sha256"
stage() {
    local label=$1; shift
    python3 -c 'import json,os,sys; print(json.dumps({"argv":sys.argv[1:],"environment":dict(sorted(os.environ.items()))},indent=2))' "$@" > "$out/$label.argv.json"
    local status=0
    /usr/bin/time -p -o "$out/$label.time" "$@" > "$out/$label.log" 2>&1 || status=$?
    printf '%s\n' "$status" > "$out/$label.status"
    return "$status"
}
stage owned-signature git -C "$fw" verify-commit HEAD
stage producer-signature git -C "$producer" verify-commit HEAD
# Public provenance checks the selected worktree's own gitlink path. Populate
# only the exact pinned source via linked vendor worktrees, not symlink tricks.
# Actual compilation still uses supported canonical source-root overrides.
for vendor in lvgl bearssl; do
    expected=$(git -C "$fw" ls-tree HEAD "3rdparty/$vendor" | awk '{print $3}')
    test "$(git -C "$canonical/3rdparty/$vendor" rev-parse HEAD)" = "$expected"
    test -z "$(git -C "$canonical/3rdparty/$vendor" status --porcelain --untracked-files=all)"
    if test -z "$(find "$fw/3rdparty/$vendor" -mindepth 1 -maxdepth 1 -print -quit)"; then
        stage "populate-$vendor" git -C "$canonical/3rdparty/$vendor" worktree add --detach "$fw/3rdparty/$vendor" "$expected"
    fi
    test "$(git -C "$fw/3rdparty/$vendor" rev-parse HEAD)" = "$expected"
    test -z "$(git -C "$fw/3rdparty/$vendor" status --porcelain --untracked-files=all)"
done
test "$(sha256sum "$original/input.config" | cut -d' ' -f1)" = 4a2ee6d9fba3b30a8be8efbc4392eef077d61b15221e7ade60b1475a8a724258
test "$(sha256sum "$original/producer-input.config" | cut -d' ' -f1)" = d2f5db5a8d33dab72cca9587ca958e67c657b1d2ea0bec18ce69bff775bd1492
test "$(sha256sum "$original/initial9/full.config" | cut -d' ' -f1)" = f80853ee695b387dcc85270cc17796c56410a68846f60d829c7b559cbfbb832a
test "$(cat "$original/initial9/source-head.txt")" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
inputs=("$fw" "$producer" "$canonical/3rdparty/lvgl" "$canonical/3rdparty/bearssl" "$fw/3rdparty/lvgl" "$fw/3rdparty/bearssl" "$original/input.config" "$original/producer-input.config" "$original/initial9/full.config" "$original/initial9/source-head.txt")
for tool in bash python3 git make cc gcc ld ld.bfd ar objcopy readelf nm kconfig-conf cp cmp sha256sum sed awk find sort xargs grep cut; do
    inputs+=("$(command -v "$tool")")
done
inputs+=(/usr/bin/time)
for support in cc1 as ld collect2 lto-wrapper; do
    path=$(cc "-print-prog-name=$support")
    test -f "$path" || path=$(command -v "$path")
    inputs+=("$path")
done
inputs+=("$(cc -print-file-name=libgcc.a)" "$(cc -print-file-name=liblto_plugin.so)")
python3 "$recipe/hash-inputs.py" "${inputs[@]}" > "$out/inputs-before.json"
git -C "$fw" diff --binary > "$out/source-before.diff"
finish() {
    local status=$?
    trap - EXIT
    set +e
    python3 "$recipe/hash-inputs.py" "${inputs[@]}" > "$out/inputs-after.json"
    test "$?" = 0 || status=1
    cmp "$out/inputs-before.json" "$out/inputs-after.json" > "$out/inputs-check.log" 2>&1 || status=1
    git -C "$fw" diff --binary > "$out/source-after.diff"
    cmp "$out/source-before.diff" "$out/source-after.diff" > "$out/diff-check.log" 2>&1 || status=1
    sha256sum -c "$out/recipe-before.sha256" > "$out/recipe-check.log" 2>&1 || status=1
    printf '%s\n' "$status" > "$out/aggregate.status"
    exit "$status"
}
trap finish EXIT
cp "$original/input.config" "$build/.config"
cp "$original/producer-input.config" "$out/producer-input.config"
common=(make -C "$fw" -j2 "CDK2_BUILD_DIR=$build" "CDK2_CONFIG=$build/.config" "COREBOOT_TREE=$producer" "COREBOOT_CONFIG=$out/producer-input.config" "CDK2_BEARSSL_DIR=$canonical/3rdparty/bearssl" "CDK2_LVGL_ROOT=$canonical/3rdparty/lvgl" CC=cc HOSTCC=cc)
stage configure "${common[@]}" olddefconfig
cp "$build/.config" "$out/resolved-before.config"
grep -qx 'CONFIG_CDK2_LVGL_RENDERER=y' "$build/.config"
grep -qx 'CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME=y' "$build/.config"
stage renderer "${common[@]}" native-lvgl-renderer-test
cmp "$out/resolved-before.config" "$build/.config" > "$out/config-check.log"
sha256sum "$build/.config" "$build/include/cdk2/config.h" "$build/native/cdk2-lvgl-renderer-test" "$build/native/cdk2-lvgl-smoke-test" "$build/native/cdk2-lvgl-ui-driver-test" "$build/native/LvglUiDxe.efi" > "$out/outputs.sha256"
find "$build/native" -type f -print0 | sort -z | xargs -0 sha256sum > "$out/native-inventory.sha256"
