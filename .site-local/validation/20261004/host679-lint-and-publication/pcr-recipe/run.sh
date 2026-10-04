#!/bin/bash
set -euo pipefail
test "$#" = 1 || { echo 'usage: run.sh NEW_ABSOLUTE_RECEIPT_DIRECTORY' >&2; exit 2; }
test -z "${PYTHONOPTIMIZE+x}"
test -z "$(env | sed -n '/^CDK2_/p')"
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export HOME=/home/sean LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
export MAKEFLAGS= MFLAGS= MAKEOVERRIDES=
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ulimit -c 0
source=/home/sean/Documents/.cdk2-worktrees/tcg2-boot-variable-pcr1-after661
baseline=/home/sean/Documents/.cdk2-worktrees/tcg2-transport-initializer-after661
producer=/home/sean/Documents/.coreboot-worktrees/tpm-fifo-command-ready-after7ee
origin=/home/sean/tpm-fifo-functional-build-after661
vendors=/home/sean/Documents/cdk2/3rdparty
recipe=$(cd -- "$(dirname -- "$0")" && pwd -P)
receipt=$1
case "$receipt" in /home/sean/*) ;; *) exit 2 ;; esac
test ! -e "$receipt"
mkdir "$receipt"
receipt=$(realpath "$receipt")
export TMPDIR=$receipt/tmp
mkdir "$TMPDIR" "$receipt/build"
printf '1\n' > "$receipt/aggregate.status"
cd "$source"
expected=6faf053c28d7b33de30daeaeae8408340685d006
test "$(git rev-parse HEAD)" = "$expected"
test "$(git status --porcelain --untracked-files=all)" = ' A docs/tcg2-boot-variable-parity.md
 M src/boot/Makefile
 M src/modules/tcg2/driver.c
 A tests/tcg2_driver_boot_variable_test.c
 M util/qemu/bin/assert-tpm-event-log-selftest.py
 M util/qemu/bin/assert-tpm-event-log.py'
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d' ' -f1)" = de77baaafa142dfd8b494c81e6b255ef3eb68dbc61226c7eea4223ab4461c849
test "$(git -C "$baseline" rev-parse HEAD)" = "$expected"
test "$(git -C "$baseline" status --porcelain --untracked-files=all)" = ' M src/modules/tcg2/driver.c'
git -C "$baseline" diff --binary > "$receipt/initializer-baseline.diff"
test "$(sha256sum "$receipt/initializer-baseline.diff" | cut -d' ' -f1)" = 45317043c5e370cc14bc1d351e7d04022281ee1789a4a8cb2d9deaec1a68fdc7
test "$(git -C "$producer" rev-parse HEAD)" = 3e2609c8938a06c6bb782474c7a01209745a4319
test "$(git -C "$vendors/bearssl" rev-parse HEAD)" = 8ef7680081c61b486622f2d983c0d3d21e83caad
test "$(git -C "$vendors/lvgl" rev-parse HEAD)" = 85aa60d18b3d5e5588d7b247abf90198f07c8a63
for tree in "$producer" "$vendors/bearssl" "$vendors/lvgl"; do
    test -z "$(git -C "$tree" status --porcelain --untracked-files=normal)"
done
sha256sum --quiet -c <<'PINS'
46a4a9d6bce99e78ed6fea666b8f281bf35efa30b594983a5ace6a6f70ecd73c  /home/sean/tpm-fifo-functional-build-after661/payload/include/cdk2/config.h
51a81cd1edecaefc0c176d4d3c36bff26b3220c9a95e48d53596cbd1cdf1b1c6  /home/sean/tpm-fifo-functional-build-after661/payload-resolved.config
3e401544c6a21c95b1d472006b13d3e84196e4a6ffbbdcb1a23d1a5168fc2f7b  /home/sean/tpm-fifo-functional-build-after661/payload-input.config
43023a30a8ffa2a1d407aa77dbfc5065f4258719429bafc63d89388fdfd50465  /home/sean/tpm-fifo-functional-build-after661/producer-resolved.config
PINS
inputs=("$source" "$baseline" "$producer" "$vendors/bearssl" "$vendors/lvgl"
    "$origin/payload/include/cdk2/config.h" "$origin/payload-resolved.config"
    "$origin/payload-input.config" "$origin/producer-resolved.config")
tools=(bash sh python3 git ssh-keygen make cc gcc ld as ar objcopy objdump nm readelf
    kconfig-conf awk sed grep sha256sum stat cp cmp head mktemp rm dirname sort
    readlink realpath cut mkdir env mv xargs perl find cat tr flock wc basename)
paths() {
    local name path
    for name in "${tools[@]}"; do
        path=$(command -v "$name")
        printf '%s\t%s\t%s\n' "$name" "$path" "$(readlink -f "$path")"
    done
}
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
    git -C "$baseline" diff --binary > "$receipt/initializer-baseline-after.diff"
    cmp "$receipt/initializer-baseline.diff" "$receipt/initializer-baseline-after.diff" > "$receipt/initializer-baseline-check.log" 2>&1 || closure=1
    paths > "$receipt/tool-paths-after.tsv" || closure=1
    cmp "$receipt/tool-paths-before.tsv" "$receipt/tool-paths-after.tsv" > "$receipt/tool-paths-check.log" 2>&1 || closure=1
    sha256sum -c "$receipt/recipe-before.sha256" > "$receipt/recipe-check.log" 2>&1 || closure=1
    for kind in config old-source compiler-inputs; do
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
gate baseline-signature git -C "$baseline" verify-commit HEAD
gate producer-signature git -C "$producer" verify-commit HEAD
build=$receipt/build
cp "$origin/payload-input.config" "$build/.config"
make_args=(make -C "$source" -j1 "CDK2_BUILD_DIR=$build" "CDK2_CONFIG=$build/.config"
    "COREBOOT_TREE=$producer" "COREBOOT_CONFIG=$origin/producer-resolved.config"
    "CDK2_BEARSSL_DIR=$vendors/bearssl" "CDK2_LVGL_ROOT=$vendors/lvgl" CC=cc HOSTCC=cc)
gate configure "${make_args[@]}" CDK2_CONFIG_READY= olddefconfig
gate config-match cmp "$origin/payload-resolved.config" "$build/.config"
gate header-match cmp "$origin/payload/include/cdk2/config.h" "$build/include/cdk2/config.h"
grep -Fxq '#define CONFIG_CDK2_NATIVE_TCG2 1' "$build/include/cdk2/config.h"
grep -Fxq '#define CONFIG_CDK2_LINEAR_BOOT 1' "$build/include/cdk2/config.h"
sha256sum "$build/.config" "$build/include/cdk2/config.h" > "$receipt/config-before.sha256"
mkdir "$receipt/old-source"
git show "$expected:src/modules/tcg2/driver.c" > "$receipt/old-source/driver-original.c"
# The PCR5 opposition has the identical separately reviewed initializer repair;
# do not confuse the preserved signed661 compile failure with a PCR assertion.
cp "$baseline/src/modules/tcg2/driver.c" "$receipt/old-source/driver.c"
git show "$expected:util/qemu/bin/assert-tpm-event-log.py" > "$receipt/old-source/assert-tpm-event-log.py"
cp "$source/util/qemu/bin/assert-tpm-event-log-selftest.py" "$receipt/old-source/assert-tpm-event-log-selftest.py"
sha256sum "$receipt/old-source"/* > "$receipt/old-source-before.sha256"
common=(env -i PATH="$PATH" HOME="$HOME" TMPDIR="$TMPDIR" LANG=C LC_ALL=C
    PYTHONDONTWRITEBYTECODE=1 ASAN_OPTIONS="$ASAN_OPTIONS" UBSAN_OPTIONS="$UBSAN_OPTIONS")
wraps=(-Wl,--wrap=cdk2_tcg2_measure_boot_variable,--wrap=cdk2_tcg2_measure_separator
    -Wl,--wrap=cdk2_tcg2_boot_attempt,--wrap=cdk2_tcg2_measure_spans)
driver_flags() {
    flags=(-std=c11 -Wall -Wextra -Werror -fshort-wchar -g -fno-pie -no-pie
        -DCDK2_HOST_TEST -ffunction-sections -fdata-sections
        -I"$build/include" -I"$source/include" -Wl,--gc-sections "${wraps[@]}")
    case "$mode" in
    o0) flags+=(-O0) ;;
    o2) flags+=(-O2) ;;
    asan) flags+=(-O1 -fsanitize=address -fno-sanitize-recover=all --param asan-globals=0) ;;
    ubsan) flags+=(-O1 -fsanitize=undefined -fno-sanitize-recover=all) ;;
    esac
}
dependency_logs=()
for mode in o0 o2 asan ubsan; do
    driver_flags
    gate "$mode-inputs" "${common[@]}" gcc "${flags[@]}" -M -MT model-dependencies "$source/tests/tcg2_driver_boot_variable_test.c"
    dependency_logs+=("$receipt/$mode-inputs.log")
done
perl -MText::ParseWords=shellwords -0777 -ne '
    s/\\\n/ /g;
    for (split /\n/) { s/^model-dependencies: // or next; s/\$\$/\$/g;
        print join("\n", shellwords($_)), "\n"; }
' "${dependency_logs[@]}" | sort -u > "$receipt/compiler-inputs.txt"
test -s "$receipt/compiler-inputs.txt"
xargs -d '\n' sha256sum < "$receipt/compiler-inputs.txt" > "$receipt/compiler-inputs-before.sha256"
failed=0
mode=o0
driver_flags
gate original-initializer-compile "${common[@]}" gcc "${flags[@]}" \
    "-DCDK2_TCG2_DRIVER_SOURCE=\"$receipt/old-source/driver-original.c\"" \
    "$source/tests/tcg2_driver_boot_variable_test.c" -o "$build/original-initializer-driver" || true
opposition=0
test "$(cat "$receipt/original-initializer-compile.status")" = 1 || opposition=1
grep -Fq "error: missing initializer for field 'max_command_size'" "$receipt/original-initializer-compile.log" || opposition=1
printf '%s\n' "$opposition" > "$receipt/original-initializer-opposition.status"
test "$opposition" = 0 || failed=1
for mode in o0 o2 asan ubsan; do
    driver_flags
    if gate "$mode-driver-compile" "${common[@]}" gcc "${flags[@]}" "$source/tests/tcg2_driver_boot_variable_test.c" -o "$build/$mode-driver"; then
        gate "$mode-driver" "${common[@]}" "$build/$mode-driver" || failed=1
    else
        failed=1
    fi
done
gate public-entry "${make_args[@]}" CDK2_CONFIG_READY=1 native-tcg2-entry-test || failed=1
gate generic-service "${make_args[@]}" CDK2_CONFIG_READY=1 native-tcg2-service-test native-tcg2-measure-test || failed=1
gate oracle-model "${common[@]}" python3 "$source/util/qemu/bin/assert-tpm-event-log-selftest.py" || failed=1
gate old-oracle "${common[@]}" python3 "$receipt/old-source/assert-tpm-event-log-selftest.py" || true
opposition=0
test "$(cat "$receipt/old-oracle.status")" = 1 || opposition=1
grep -Fq 'hostile TPM evidence was accepted' "$receipt/old-oracle.log" || opposition=1
printf '%s\n' "$opposition" > "$receipt/old-oracle-opposition.status"
test "$opposition" = 0 || failed=1
for mode in o0 o2; do
    driver_flags
    if gate "$mode-old-driver-compile" "${common[@]}" gcc "${flags[@]}" \
        "-DCDK2_TCG2_DRIVER_SOURCE=\"$receipt/old-source/driver.c\"" \
        "$source/tests/tcg2_driver_boot_variable_test.c" -o "$build/$mode-old-driver"; then
        gate "$mode-old-driver" "${common[@]}" "$build/$mode-old-driver" || true
        opposition=0
        test "$(cat "$receipt/$mode-old-driver.status")" = 134 || opposition=1
        grep -Fq 'record->pcr == pcr && record->type == type' "$receipt/$mode-old-driver.log" || opposition=1
        printf '%s\n' "$opposition" > "$receipt/$mode-old-driver-opposition.status"
        test "$opposition" = 0 || failed=1
    else
        failed=1
    fi
done
execution=$failed
exit "$failed"
