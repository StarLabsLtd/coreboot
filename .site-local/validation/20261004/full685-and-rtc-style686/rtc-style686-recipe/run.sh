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
ulimit -c 0
source=/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
mbedtls=$producer/3rdparty/mbedtls
normal=/home/sean/fresh-normal-focus-after657.CftZZg/build
recipe=$(cd -- "$(dirname -- "$0")" && pwd -P)
receipt=$1
case "$receipt" in /home/sean/*) ;; *) exit 2 ;; esac
test ! -e "$receipt" && test ! -L "$receipt"
mkdir "$receipt"
receipt=$(realpath "$receipt")
export TMPDIR=$receipt/tmp
mkdir "$TMPDIR" "$receipt/build"
printf '1\n' > "$receipt/aggregate.status"
cd "$source"
expected=ddc179372681983e2f903ab0bb4cb7b0f0c495ff
test "$(git rev-parse HEAD)" = "$expected"
test "$(git status --porcelain --untracked-files=all)" = ' M src/boot/coreboot_test.c'
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d' ' -f1)" = ee5bdfe1e5dfcbb7a9ef8cc1b9ded80425b726350536aa9c6f7cb30d709b71e9
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test "$(git -C "$mbedtls" rev-parse HEAD)" = 0bebf8b8c7f07abe3571ded48a11aa907a1ffb20
for tree in "$producer" "$mbedtls"; do test -z "$(git -C "$tree" status --porcelain --untracked-files=all)"; done
for vendor in bearssl lvgl; do
    selected=$(git ls-tree HEAD "3rdparty/$vendor" | awk '$1 == "160000" { print $3 }')
    test -n "$selected"
    test "$(git -C "$source/3rdparty/$vendor" rev-parse HEAD)" = "$selected"
    test -z "$(git -C "$source/3rdparty/$vendor" status --porcelain --untracked-files=all)"
done
sha256sum --quiet -c <<'PINS'
10f6aef02d7678a1ae8d5ffab8157b5ab0a37438576a645cdf237fe130fa2aac  /home/sean/fresh-normal-focus-after657.CftZZg/build/resolved.config
4a2ee6d9fba3b30a8be8efbc4392eef077d61b15221e7ade60b1475a8a724258  /home/sean/fresh-normal-focus-after657.CftZZg/build/input.config
d2f5db5a8d33dab72cca9587ca958e67c657b1d2ea0bec18ce69bff775bd1492  /home/sean/fresh-normal-focus-after657.CftZZg/build/producer-input.config
ac668eb9c8a622e020182653fd832e6671741f5e616a01cfa4fb33b8e9b4065f  /home/sean/fresh-normal-focus-after657.CftZZg/build/include/cdk2/config.h
PINS
inputs=("$source" "$producer" "$mbedtls" "$source/3rdparty/bearssl" "$source/3rdparty/lvgl"
    "$normal/resolved.config" "$normal/input.config" "$normal/producer-input.config" "$normal/include/cdk2/config.h")
tools=(bash sh python3 git ssh-keygen make cc gcc ld as ar objcopy objdump nm readelf
    kconfig-conf awk sed grep sha256sum stat cp cmp head tail mktemp rm dirname sort
    readlink realpath cut mkdir env mv xargs perl find cat tr flock wc basename openssl)
paths() {
    local name path
    for name in "${tools[@]}"; do
        path=$(command -v "$name")
        printf '%s\t%s\t%s\n' "$name" "$path" "$(readlink -f "$path")"
    done
}
configuration_paths=("$source/.checkpatch.conf" "$HOME/.checkpatch.conf" "$source/.scripts/.checkpatch.conf")
configuration_state() {
    local path
    for path in "${configuration_paths[@]}"; do
        if test -e "$path" || test -L "$path"; then
            test -f "$path" && test ! -L "$path"
            printf 'present\t%s\n' "$path"
        else printf 'absent\t%s\n' "$path"; fi
    done
}
configuration_state > "$receipt/checkpatch-config-before.tsv"
for path in "${configuration_paths[@]}"; do test ! -f "$path" || inputs+=("$path"); done
paths > "$receipt/tool-paths-before.tsv"
while IFS=$'\t' read -r name path resolved; do inputs+=("$path"); done < "$receipt/tool-paths-before.tsv"
inputs+=(/usr/bin/time)
for support in cc1 as ld collect2 lto-wrapper; do
    path=$(gcc "-print-prog-name=$support")
    test -f "$path" || path=$(command -v "$path")
    inputs+=("$path")
    printf '%s\t%s\n' "$support" "$path" >> "$receipt/compiler-support.tsv"
done
for support in libgcc.a liblto_plugin.so libasan.so libubsan.so; do
    path=$(gcc "-print-file-name=$support")
    test -f "$path"
    inputs+=("$path")
    printf '%s\t%s\n' "$support" "$path" >> "$receipt/compiler-support.tsv"
done
python3 "$recipe/hash-inputs.py" "${inputs[@]}" > "$receipt/inputs-before.json"
sha256sum "$recipe"/* > "$receipt/recipe-before.sha256"
git rev-parse HEAD > "$receipt/head-before.txt"
git status --porcelain --untracked-files=all > "$receipt/status-before.txt"
execution=1
finish() {
    local interrupted=$? closure=0
    trap - EXIT
    set +e
    python3 "$recipe/hash-inputs.py" "${inputs[@]}" > "$receipt/inputs-after.json" || closure=1
    cmp "$receipt/inputs-before.json" "$receipt/inputs-after.json" > "$receipt/inputs-check.log" 2>&1 || closure=1
    git rev-parse HEAD > "$receipt/head-after.txt"
    git status --porcelain --untracked-files=all > "$receipt/status-after.txt"
    git diff --binary > "$receipt/source-after.diff"
    cmp "$receipt/head-before.txt" "$receipt/head-after.txt" > "$receipt/head-check.log" 2>&1 || closure=1
    cmp "$receipt/status-before.txt" "$receipt/status-after.txt" > "$receipt/status-check.log" 2>&1 || closure=1
    cmp "$receipt/frozen.diff" "$receipt/source-after.diff" > "$receipt/diff-check.log" 2>&1 || closure=1
    paths > "$receipt/tool-paths-after.tsv" || closure=1
    cmp "$receipt/tool-paths-before.tsv" "$receipt/tool-paths-after.tsv" > "$receipt/tool-paths-check.log" 2>&1 || closure=1
    configuration_state > "$receipt/checkpatch-config-after.tsv" || closure=1
    cmp "$receipt/checkpatch-config-before.tsv" "$receipt/checkpatch-config-after.tsv" > "$receipt/checkpatch-config-check.log" 2>&1 || closure=1
    sha256sum -c "$receipt/recipe-before.sha256" > "$receipt/recipe-check.log" 2>&1 || closure=1
    for kind in config old-source compiler-inputs outputs; do
        if test -f "$receipt/$kind-before.sha256"; then
            sha256sum -c "$receipt/$kind-before.sha256" > "$receipt/$kind-check.log" 2>&1 || closure=1
        fi
    done
    printf '%s\n' "$execution" > "$receipt/execution.status"
    printf '%s\n' "$closure" > "$receipt/closure.status"
    local aggregate=0
    test "$execution" = 0 && test "$closure" = 0 && test "$interrupted" = 0 || aggregate=1
    printf '%s\n' "$aggregate" > "$receipt/aggregate.status"
    exit "$aggregate"
}
trap finish EXIT
gate() {
    local label=$1 status=0; shift
    python3 -c 'import json,os,sys; print(json.dumps({"argv":sys.argv[1:],"environment":dict(sorted(os.environ.items()))},indent=2))' "$@" > "$receipt/$label.argv.json"
    /usr/bin/time -p -o "$receipt/$label.time" "$@" > "$receipt/$label.log" 2>&1 || status=$?
    printf '%s\n' "$status" > "$receipt/$label.status"
    return "$status"
}
gate source-signature git verify-commit HEAD
gate producer-signature git -C "$producer" verify-commit HEAD
build=$receipt/build
cp "$normal/input.config" "$build/.config"
make_args=(make -C "$source" -j1 "CDK2_BUILD_DIR=$build" "CDK2_CONFIG=$build/.config"
    "COREBOOT_TREE=$producer" "COREBOOT_CONFIG=$normal/producer-input.config"
    "MBEDTLS_SOURCE=$mbedtls" CC=cc HOSTCC=cc)
gate configure "${make_args[@]}" CDK2_CONFIG_READY= olddefconfig
gate config-match cmp "$normal/resolved.config" "$build/.config"
gate header-match cmp "$normal/include/cdk2/config.h" "$build/include/cdk2/config.h"
for flag in CDK2_COREBOOT_CAPSULE_PROFILE CDK2_NATIVE_SYSTEM_FMP PAYLOAD_DMA_HANDOFF; do
    grep -Fxq "#define CONFIG_$flag 1" "$build/include/cdk2/config.h"
done
sha256sum "$build/.config" "$build/include/cdk2/config.h" > "$receipt/config-before.sha256"
mkdir "$receipt/old-source"
git show "$expected:src/boot/coreboot_test.c" > "$receipt/old-source/coreboot_test.c"
sha256sum "$receipt/old-source/coreboot_test.c" > "$receipt/old-source-before.sha256"
checker=(perl "$source/util/lint/checkpatch.pl" --no-tree --show-types --file --quiet
    --typedefsfile "$source/util/lint/cdk2-typedefs.checkpatch" --max-line-length=96)
failed=0
gate current-style "${checker[@]}" "$source/src/boot/coreboot_test.c" || failed=1
test ! -s "$receipt/current-style.log" || failed=1
gate old-style "${checker[@]}" "$receipt/old-source/coreboot_test.c" || true
opposition=0
test "$(cat "$receipt/old-style.status")" = 1 || opposition=1
test "$(grep -Fc "ERROR:SPACING: space prohibited before that ':' (ctx:WxW)" "$receipt/old-style.log")" = 2 || opposition=1
test "$(grep -Fxc 'ERROR:TRAILING_STATEMENTS: trailing statements should be on next line' "$receipt/old-style.log")" = 1 || opposition=1
test "$(grep -Ec '^(ERROR|WARNING|CHECK):' "$receipt/old-style.log")" = 3 || opposition=1
grep -Fxq 'total: 3 errors, 0 warnings, 6648 lines checked' "$receipt/old-style.log" || opposition=1
printf '%s\n' "$opposition" > "$receipt/old-style-opposition.status"
test "$opposition" = 0 || failed=1
gate checkpatch-allowlist "${make_args[@]}" checkpatch-allowlist-check || failed=1
common=(env -i PATH="$PATH" HOME="$HOME" TMPDIR="$TMPDIR" LANG=C LC_ALL=C
    PYTHONDONTWRITEBYTECODE=1 ASAN_OPTIONS="$ASAN_OPTIONS" UBSAN_OPTIONS="$UBSAN_OPTIONS")
coreboot_sources=(src/boot/coreboot.c src/lib/boot_private_buffer.c src/lib/image_policy_snapshot.c
    src/boot/coreboot_dma_handoff.c src/boot/coreboot_checksum.c src/boot/coreboot_resource.c
    src/boot/coreboot_hobs.c src/modules/pci_host_bridge/model.c src/boot/coreboot_handoff.c
    src/lib/boot_logo.c src/boot/early_splash.c src/boot/services.c src/lib/linear_boot.c
    src/lib/diagnostic.c src/lib/direct_image_table.c src/lib/pe_image_view.c src/boot/coreboot_test.c)
for index in "${!coreboot_sources[@]}"; do coreboot_sources[index]=$source/${coreboot_sources[index]}; done
flags_for_mode() {
    flags=(-std=c11 -Wall -Wextra -Werror -fshort-wchar -g -fno-pie -no-pie
        -I"$build/include" -I"$source/include" -I"$source/src/boot"
        -DCDK2_COREBOOT_BACKEND_TEST -DCDK2_DIAG_UNIT_TEST
        -ffunction-sections -fdata-sections -Wl,--gc-sections)
    case "$mode" in
    o0) flags+=(-O0) ;;
    o2) flags+=(-O2) ;;
    asan) flags+=(-O1 -fsanitize=address -fno-sanitize-recover=all) ;;
    ubsan) flags+=(-O1 -fsanitize=undefined -fno-sanitize-recover=all) ;;
    esac
}
dependency_logs=()
for mode in o0 o2 asan ubsan; do
    flags_for_mode
    gate "$mode-inputs" "${common[@]}" gcc "${flags[@]}" -M -MT model-dependencies "${coreboot_sources[@]}"
    dependency_logs+=("$receipt/$mode-inputs.log")
done
gate filter-inputs gcc -std=c11 -M -MT model-dependencies "$source/util/lint/cdk2-checkpatch-filter.c"
dependency_logs+=("$receipt/filter-inputs.log")
perl -MText::ParseWords=shellwords -0777 -ne '
    s/\\\n/ /g;
    for (split /\n/) { s/^model-dependencies: // or next; s/\$\$/\$/g;
        print join("\n", shellwords($_)), "\n"; }
' "${dependency_logs[@]}" | sort -u > "$receipt/compiler-inputs.txt"
test -s "$receipt/compiler-inputs.txt"
xargs -d '\n' sha256sum < "$receipt/compiler-inputs.txt" > "$receipt/compiler-inputs-before.sha256"
for mode in o0 o2 asan ubsan; do
    flags_for_mode
    if gate "$mode-coreboot-compile" "${common[@]}" gcc "${flags[@]}" "${coreboot_sources[@]}" -o "$build/$mode-coreboot"; then
        gate "$mode-coreboot" "${common[@]}" "$build/$mode-coreboot" || failed=1
    else failed=1; fi
done
find "$build" -type f -print0 | sort -z | xargs -0 sha256sum > "$receipt/outputs-before.sha256"
execution=$failed
exit "$failed"
