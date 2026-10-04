#!/bin/bash
set -euo pipefail
test "$#" = 1 || { echo 'usage: run.sh NEW_ABSOLUTE_RECEIPT_DIRECTORY' >&2; exit 2; }
test -z "${PYTHONOPTIMIZE+x}"
test -z "$(env | sed -n '/^CDK2_/p')"
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
export MAKEFLAGS= MFLAGS= MAKEOVERRIDES= MAKE=make CC=cc HOSTCC=cc
export COREBOOT_CONFIG=
fw=/home/sean/Documents/.cdk2-worktrees/capsule-report-cache-input-after658
export COREBOOT_TREE=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
export MBEDTLS_SOURCE=$COREBOOT_TREE/3rdparty/mbedtls
recipe=$(cd -- "$(dirname -- "$0")" && pwd -P)
out=$1
case "$out" in /home/sean/*) ;; *) echo 'new receipt must be absolute under /home/sean' >&2; exit 2;; esac
test ! -e "$out"
mkdir "$out"
out=$(realpath "$out")
export TMPDIR=$out/tmp
mkdir "$TMPDIR"
printf '1\n' > "$out/aggregate.status"
test "$(git -C "$fw" rev-parse HEAD)" = b37a977ec5f81b6fa0bce527ab74acd743040a24
test "$(git -C "$fw" diff --binary | sha256sum | cut -d' ' -f1)" = ec23d86c719360eea378e1b537899fe0006583525c0e41f3279bf37e2fc275f2
test "$(git -C "$fw" status --porcelain --untracked-files=all)" = ' M src/boot/Makefile'
test "$(git -C "$COREBOOT_TREE" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test -z "$(git -C "$COREBOOT_TREE" status --porcelain --untracked-files=all)"
test "$(git -C "$MBEDTLS_SOURCE" rev-parse HEAD)" = 0bebf8b8c7f07abe3571ded48a11aa907a1ffb20
test -z "$(git -C "$MBEDTLS_SOURCE" status --porcelain --untracked-files=all)"
sha256sum "$recipe"/* > "$out/recipe-before.sha256"
inputs=("$fw" "$COREBOOT_TREE" "$MBEDTLS_SOURCE")
for tool in bash sh python3 git ssh-keygen make cc gcc ld as ar objcopy objdump nm readelf kconfig-conf awk sed grep sha256sum stat paste cp cmp head mktemp rm dirname sort readlink realpath cut mkdir env mv flock; do
    path=$(command -v "$tool")
    printf '%s\t%s\n' "$tool" "$path" >> "$out/tools-resolved.tsv"
    inputs+=("$path")
done
inputs+=(/usr/bin/time)
for support in cc1 as ld collect2 lto-wrapper; do
    path=$(cc "-print-prog-name=$support")
    test -f "$path" || path=$(command -v "$path")
    printf '%s\t%s\n' "$support" "$path" >> "$out/compiler-support.tsv"
    inputs+=("$path")
done
for support in libgcc.a liblto_plugin.so; do
    path=$(cc "-print-file-name=$support")
    test -f "$path"
    printf '%s\t%s\n' "$support" "$path" >> "$out/compiler-support.tsv"
    inputs+=("$path")
done
python3 "$recipe/hash-inputs.py" "${inputs[@]}" > "$out/inputs-before.json"
git -C "$fw" diff --binary > "$out/source-before.diff"
git -C "$fw" rev-parse HEAD > "$out/head-before.txt"
execution=1
finish() {
    local interrupted=$? closure=0
    trap - EXIT
    set +e
    python3 "$recipe/hash-inputs.py" "${inputs[@]}" > "$out/inputs-after.json" || closure=1
    cmp "$out/inputs-before.json" "$out/inputs-after.json" > "$out/inputs-check.log" 2>&1 || closure=1
    git -C "$fw" diff --binary > "$out/source-after.diff"
    cmp "$out/source-before.diff" "$out/source-after.diff" > "$out/diff-check.log" 2>&1 || closure=1
    git -C "$fw" rev-parse HEAD > "$out/head-after.txt"
    cmp "$out/head-before.txt" "$out/head-after.txt" > "$out/head-check.log" 2>&1 || closure=1
    sha256sum -c "$out/recipe-before.sha256" > "$out/recipe-check.log" 2>&1 || closure=1
    if test -f "$out/config-before.sha256"; then
        sha256sum -c "$out/config-before.sha256" > "$out/config-check.log" 2>&1 || closure=1
    fi
    printf '%s\n' "$execution" > "$out/execution.status"
    printf '%s\n' "$closure" > "$out/closure.status"
    local aggregate=0
    test "$execution" = 0 && test "$closure" = 0 && test "$interrupted" = 0 || aggregate=1
    printf '%s\n' "$aggregate" > "$out/aggregate.status"
    exit "$aggregate"
}
trap finish EXIT
stage() {
    local label=$1 status=0; shift
    python3 -c 'import json,os,sys; print(json.dumps({"argv":sys.argv[1:],"environment":dict(sorted(os.environ.items()))},indent=2))' "$@" > "$out/$label.argv.json"
    /usr/bin/time -p -o "$out/$label.time" "$@" > "$out/$label.log" 2>&1 || status=$?
    printf '%s\n' "$status" > "$out/$label.status"
    return "$status"
}
stage owned-signature git -C "$fw" verify-commit HEAD
stage producer-signature git -C "$COREBOOT_TREE" verify-commit HEAD
build=$out/audit-build
stage audit-configure make -s -C "$fw" -j1 CDK2_CONFIG_READY= "CDK2_BUILD_DIR=$build" defconfig
test -f "$build/kconfig/coreboot-source.tmp"
test ! -s "$build/kconfig/coreboot-source.tmp"
grep -Fxq standalone "$build/kconfig/coreboot-input.identity"
sha256sum "$build/.config" "$build/include/cdk2/config.h" "$build/kconfig/coreboot-source.tmp" "$build/kconfig/coreboot-input.identity" > "$out/config-before.sha256"
target=$build/native/dxe-core-capsule_report.o
for phase in registered removed restored; do
    extra=/dev/null expectation=registered
    if test "$phase" = removed; then extra=$recipe/remove.mk; expectation=removed; fi
    stage "make-$phase" make -pn -C "$fw" -j1 -f "$fw/Makefile" -f "$extra" CDK2_CONFIG_READY=1 "CDK2_BUILD_DIR=$build" "$target"
    stage "audit-$phase" python3 "$recipe/audit.py" "$out/make-$phase.log" "$build" "$fw" "$expectation"
done
# Run the complete source-owned n/y/n cache/rebuild/inventory test unchanged.
# sh -x preserves actual profile iteration and Make invocations despite cleanup.
stage native-config-cache sh -x "$fw/tests/native_config_cache_test.sh"
execution=0
