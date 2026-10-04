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
fw=/home/sean/Documents/.cdk2-worktrees/direct-phase-capsule-report-link-after661
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
expected=6faf053c28d7b33de30daeaeae8408340685d006
test "$(git -C "$fw" rev-parse HEAD)" = "$expected"
test "$(git -C "$fw" diff --binary | sha256sum | cut -d' ' -f1)" = d674d59b36f881894020e8d42e308618bac51926c526b5c9e87ca6763d99af33
test "$(git -C "$fw" status --porcelain --untracked-files=all)" = ' M tests/direct_phase_entry_test.sh
 M tests/direct_phase_presence_fixture.c
 M tests/dxe_core_entry_test.c'
test "$(git -C "$donor" rev-parse HEAD)" = "$expected"
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
for tool in bash sh python3 git ssh-keygen make cc gcc ld as ar objcopy objdump nm readelf kconfig-conf awk sed grep sha256sum stat cp cmp head mktemp rm dirname sort readlink realpath cut mkdir env mv xargs perl find cat tr flock wc basename openssl ln; do
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
    if test -f "$out/old-script-before.sha256"; then
        sha256sum -c "$out/old-script-before.sha256" > "$out/old-script-check.log" 2>&1 || closure=1
        old_links > "$out/old-links-after.txt" || closure=1
        cmp "$out/old-links-before.txt" "$out/old-links-after.txt" > "$out/old-links-check.log" 2>&1 || closure=1
    fi
    find "$out/build" -type f -name config.h -exec sha256sum {} + > "$out/generated-headers.sha256" 2>/dev/null
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
old=$out/old-source
mkdir -p "$old/tests"
git -C "$fw" show "$expected:tests/direct_phase_entry_test.sh" > "$old/tests/direct_phase_entry_test.sh"
ln -s "$fw/src" "$old/src"
ln -s "$fw/include" "$old/include"
for file in dxe_core_entry_test.c direct_phase_presence_fixture.c dxe_core_entry_mixed_partition_test.c; do
    ln -s "$fw/tests/$file" "$old/tests/$file"
done
old_links() {
    local path
    for path in "$old/src" "$old/include" "$old/tests/dxe_core_entry_test.c" "$old/tests/direct_phase_presence_fixture.c" "$old/tests/dxe_core_entry_mixed_partition_test.c"; do
        test -L "$path" && test -e "$path" || return 1
        printf '%s\t%s\n' "$path" "$(readlink "$path")"
    done
}
old_links > "$out/old-links-before.txt"
sha256sum "$old/tests/direct_phase_entry_test.sh" > "$out/old-script-before.sha256"
old_opposition() {
    local label=$1 header=$2 status=0 accepted=0
    stage "$label-old-script" sh "$old/tests/direct_phase_entry_test.sh" "$header" cc \
        '-std=c11 -O2 -Wall -Wextra -Werror -fshort-wchar' || status=$?
    test "$status" = 1 || accepted=1
    grep -Fq 'undefined reference to `cdk2_capsule_report_fmp' "$out/$label-old-script.log" || accepted=1
    grep -Fq 'undefined reference to `cdk2_capsule_report_initialize' "$out/$label-old-script.log" || accepted=1
    grep -Fq 'collect2: error: ld returned 1 exit status' "$out/$label-old-script.log" || accepted=1
    printf '%s\n' "$accepted" > "$out/$label-old-script-opposition.status"
    return "$accepted"
}
# The public strict direct-phase target retains every existing executable and
# source-oracle mutation under the exact regenerated normal protected profile.
failed=0
old_opposition normal "$build/include/cdk2/config.h" || failed=1
stage normal-direct-phase "${common[@]}" CDK2_CONFIG_READY=1 native-dxe-direct-phase-entry-test || failed=1
# Resolve the source-owned genuine legacy fixture independently. Never mutate
# a generated header or inherit the protected producer configuration.
legacy=$out/build/legacy
legacy_common=(env -u MAKEFLAGS -u MFLAGS -u MAKEOVERRIDES -u CDK2_CONFIG_READY
    -u CDK2_CONFIG_LOCK_FD make -C "$fw" -j1 "CDK2_BUILD_DIR=$legacy"
    "CDK2_CONFIG=$legacy/.config" "CDK2_CONFIG_HEADER=$legacy/include/cdk2/config.h"
    "CDK2_KCONFIG=$fw/Kconfig" "CDK2_DEFCONFIG=$fw/tests/dxe_core_legacy_fixture.config"
    COREBOOT_CONFIG= CDK2_NATIVE_HOST_CC=cc
    'CDK2_NATIVE_HOST_CFLAGS=-std=c11 -O2 -Wall -Wextra -Werror -fshort-wchar')
if stage legacy-configure "${legacy_common[@]}" defconfig; then
    valid=0
    for setting in 'PROTECTED_VARIABLE_RUNTIME 0' 'STRICT_DIRECT_RUNTIME 1' 'LINEAR_BOOT 1' 'NATIVE_SECURITY_STUB 1' 'COREBOOT_CAPSULE_PROFILE 1' 'NATIVE_SYSTEM_FMP 1'; do
        grep -Fxq "#define CONFIG_CDK2_$setting" "$legacy/include/cdk2/config.h" || valid=1
    done
    grep -Fxq '#define CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED 0' "$legacy/include/cdk2/config.h" || valid=1
    test -f "$legacy/kconfig/coreboot-source.tmp" && test ! -s "$legacy/kconfig/coreboot-source.tmp" || valid=1
    printf '%s\n' "$valid" > "$out/legacy-profile.status"
    sha256sum "$legacy/.config" "$legacy/include/cdk2/config.h" >> "$out/config-before.sha256"
    if test "$valid" = 0; then
        old_opposition legacy "$legacy/include/cdk2/config.h" || failed=1
        stage legacy-direct-phase "${legacy_common[@]}" CDK2_CONFIG_READY=1 native-dxe-direct-phase-entry-test || failed=1
    else
        failed=1
    fi
else
    failed=1
fi
for file in tests/direct_phase_entry_test.sh tests/direct_phase_presence_fixture.c tests/dxe_core_entry_test.c; do
    stage "checkpatch-${file##*/}" perl "$fw/util/lint/checkpatch.pl" \
        --typedefsfile "$fw/util/lint/cdk2-typedefs.checkpatch" \
        --no-tree --show-types --file "$fw/$file" || failed=1
done
execution=$failed
exit "$failed"
