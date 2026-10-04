#!/bin/bash
set -euo pipefail
test "$#" = 1 || { echo 'usage: run.sh NEW_ABSOLUTE_RECEIPT_DIRECTORY' >&2; exit 2; }
test -z "${PYTHONOPTIMIZE+x}"
test -z "$(env | sed -n '/^CDK2_/p')"
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export HOME=/home/sean LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
export MAKEFLAGS= MFLAGS= MAKEOVERRIDES= MAKE=make CC=cc HOSTCC=cc
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export KEEP_SPLASH_STATUS_TMP=1
ulimit -c 0
fw=/home/sean/Documents/.cdk2-worktrees/dxe-publication-joined-after670
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
mbedtls=$producer/3rdparty/mbedtls
donor=/home/sean/Documents/cdk2
normal=/home/sean/fresh-normal-focus-after657.CftZZg/build
recipe=$(cd -- "$(dirname -- "$0")" && pwd -P)
out=$1
case "$out" in /home/sean/*) ;; *) echo 'receipt must be absolute under /home/sean' >&2; exit 2;; esac
test ! -e "$out"
mkdir "$out"
out=$(realpath "$out")
export TMPDIR=$out/tmp
mkdir "$TMPDIR"
printf '1\n' > "$out/aggregate.status"
expected=25e55658e15449d6de34917398fbcd8df68e9b9f
base=6faf053c28d7b33de30daeaeae8408340685d006
linear=9698e3ed5f0526e8bae89d725445c91e55c24ed6
test "$(git -C "$fw" rev-parse HEAD)" = "$expected"
test "$(git -C "$fw" diff --binary | sha256sum | cut -d' ' -f1)" = ac425f1c2681ddcc17a794edfd80bff672f85724d7b8a1422932a0389a51ed2b
test "$(git -C "$fw" status --porcelain --untracked-files=all)" = ' M src/modules/dxe_core/entry.c
 A tests/dxe_capsule_publication_test.c
 A tests/dxe_capsule_publication_test.sh'
test "$(git -C "$donor" rev-parse HEAD)" = "$base"
test "$(git -C "$fw" rev-parse "$linear^")" = "$base"
test "$(git -C "$fw" rev-parse "$expected^")" = "$linear"
test "$(git -C "$fw" diff --binary "$base" "$linear" | sha256sum | cut -d' ' -f1)" = 0dac17a371f47097580c4c69d0820f7b04ebf24ae9ea79a54e65808c129cb523
test "$(git -C "$fw" diff --binary "$linear" "$expected" | sha256sum | cut -d' ' -f1)" = 553a32e85dde6b20324ec4805b66fc44db5be79acb3b938c18df2c4bb7f42f8e
printf 'base=%s\nlinear-prefix=%s\ndirect-prefix=%s\n' "$base" "$linear" "$expected" > "$out/composition.txt"
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test "$(git -C "$mbedtls" rev-parse HEAD)" = 0bebf8b8c7f07abe3571ded48a11aa907a1ffb20
test "$(git -C "$donor/3rdparty/bearssl" rev-parse HEAD)" = 8ef7680081c61b486622f2d983c0d3d21e83caad
test "$(git -C "$donor/3rdparty/lvgl" rev-parse HEAD)" = 85aa60d18b3d5e5588d7b247abf90198f07c8a63
for tree in "$producer" "$mbedtls" "$donor" "$donor/3rdparty/bearssl" "$donor/3rdparty/lvgl"; do
    test -z "$(git -C "$tree" status --porcelain --untracked-files=all)"
done
sha256sum --quiet -c <<'PINS'
10f6aef02d7678a1ae8d5ffab8157b5ab0a37438576a645cdf237fe130fa2aac  /home/sean/fresh-normal-focus-after657.CftZZg/build/resolved.config
d2f5db5a8d33dab72cca9587ca958e67c657b1d2ea0bec18ce69bff775bd1492  /home/sean/fresh-normal-focus-after657.CftZZg/build/producer-input.config
ac668eb9c8a622e020182653fd832e6671741f5e616a01cfa4fb33b8e9b4065f  /home/sean/fresh-normal-focus-after657.CftZZg/build/include/cdk2/config.h
PINS
cd "$fw"
config_state() {
    local path
    printf 'HOME=/home/sean precedence=.:/home/sean:.scripts\n'
    for path in "$fw/.checkpatch.conf" /home/sean/.checkpatch.conf "$fw/.scripts/.checkpatch.conf"; do
        if test -e "$path" || test -L "$path"; then
            test -f "$path" && test ! -L "$path"
            sha256sum "$path"
        else
            printf 'absent %s\n' "$path"
        fi
    done
}
config_state > "$out/checker-config-before.txt"
sha256sum "$recipe"/* > "$out/recipe-before.sha256"
inputs=("$fw" "$producer" "$mbedtls" "$donor" "$donor/3rdparty/bearssl" "$donor/3rdparty/lvgl"
    "$normal/resolved.config" "$normal/producer-input.config" "$normal/include/cdk2/config.h")
for tool in bash sh python3 git ssh-keygen make cc gcc ld as ar objcopy objdump nm readelf kconfig-conf awk sed grep sha256sum stat cp cmp head mktemp rm dirname sort readlink realpath cut mkdir env mv xargs perl find cat tr flock wc basename openssl; do
    path=$(command -v "$tool")
    printf '%s\t%s\t%s\n' "$tool" "$path" "$(readlink -f "$path")" >> "$out/tools-resolved.tsv"
    inputs+=("$path")
done
inputs+=(/usr/bin/time)
for support in cc1 as ld collect2 lto-wrapper; do
    path=$(cc "-print-prog-name=$support")
    test -f "$path" || path=$(command -v "$path")
    printf '%s\t%s\n' "$support" "$path" >> "$out/compiler-support.tsv"
    inputs+=("$path")
done
for support in libgcc.a liblto_plugin.so libasan.so libubsan.so; do
    path=$(cc "-print-file-name=$support")
    test -f "$path"
    printf '%s\t%s\n' "$support" "$path" >> "$out/compiler-support.tsv"
    inputs+=("$path")
done
python3 "$recipe/hash-inputs.py" "${inputs[@]}" > "$out/inputs-before.json"
git -C "$fw" diff --binary > "$out/source-before.diff"
git -C "$fw" status --porcelain --untracked-files=all > "$out/status-before.txt"
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
    git -C "$fw" status --porcelain --untracked-files=all > "$out/status-after.txt"
    cmp "$out/status-before.txt" "$out/status-after.txt" > "$out/status-check.log" 2>&1 || closure=1
    git -C "$fw" rev-parse HEAD > "$out/head-after.txt"
    cmp "$out/head-before.txt" "$out/head-after.txt" > "$out/head-check.log" 2>&1 || closure=1
    sha256sum -c "$out/recipe-before.sha256" > "$out/recipe-check.log" 2>&1 || closure=1
    config_state > "$out/checker-config-after.txt" || closure=1
    cmp "$out/checker-config-before.txt" "$out/checker-config-after.txt" > "$out/checker-config-check.log" 2>&1 || closure=1
    if test -f "$out/config-before.sha256"; then
        sha256sum -c "$out/config-before.sha256" > "$out/config-check.log" 2>&1 || closure=1
    fi
    find "$out/build/native" -maxdepth 1 -type f -executable -exec sha256sum {} + > "$out/outputs.sha256" 2>/dev/null
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
stage linear-prefix-signature git -C "$fw" verify-commit "$linear"
stage base-signature git -C "$fw" verify-commit "$base"
stage donor-signature git -C "$donor" verify-commit HEAD
stage producer-signature git -C "$producer" verify-commit HEAD
build=$out/build
mkdir "$build"
cp "$normal/resolved.config" "$build/resolved.config"
common=(make -C "$fw" -j1 "CDK2_BUILD_DIR=$build" "CDK2_CONFIG=$build/resolved.config"
    "COREBOOT_TREE=$producer" "COREBOOT_CONFIG=$normal/producer-input.config"
    "MBEDTLS_SOURCE=$mbedtls" "CDK2_BEARSSL_DIR=$donor/3rdparty/bearssl"
    "CDK2_LVGL_ROOT=$donor/3rdparty/lvgl" CC=cc HOSTCC=cc)
stage configure "${common[@]}" CDK2_CONFIG_READY= olddefconfig
stage normal-config-match cmp "$normal/resolved.config" "$build/resolved.config"
stage normal-header-match cmp "$normal/include/cdk2/config.h" "$build/include/cdk2/config.h"
for symbol in CDK2_LINEAR_BOOT CDK2_COREBOOT_CAPSULE_PROFILE CDK2_NATIVE_SYSTEM_FMP PAYLOAD_DMA_HANDOFF CDK2_NATIVE_TCG2 CDK2_STRICT_DIRECT_RUNTIME; do
    grep -Fxq "#define CONFIG_$symbol 1" "$build/include/cdk2/config.h"
done
sha256sum "$build/resolved.config" "$build/include/cdk2/config.h" > "$out/config-before.sha256"
# Existing public HOST target families compile and run the edited fixtures.
# Script stages retain their original O0/O2 SAN and causal-mutant contracts.
failed=0
for target in native-dxe-core-test native-dxe-direct-phase-entry-test native-linear-pre-capsule-order-test native-capsule-certified-refusal-test native-splash-status-report-test native-dxe-core-diagnostic-parity; do
    stage "$target" "${common[@]}" CDK2_CONFIG_READY=1 "$target" || failed=1
done
stage capsule-disk-stage sh "$fw/tests/dxe_core_capsule_disk_stage_test.sh" \
    "$build/include/cdk2/config.h" cc '-std=gnu11 -Wall -Wextra -Werror -fshort-wchar' || failed=1
stage actual-publication sh "$fw/tests/dxe_capsule_publication_test.sh" "$build/include/cdk2/config.h" || failed=1
for file in src/modules/dxe_core/entry.c tests/dxe_capsule_publication_test.c tests/dxe_capsule_publication_test.sh; do
    label=checkpatch-${file##*/}
    stage "$label" perl "$fw/util/lint/checkpatch.pl" --no-tree --show-types --typedefsfile "$fw/util/lint/cdk2-typedefs.checkpatch" --file "$fw/$file" || failed=1
done
execution=$failed
exit "$failed"
