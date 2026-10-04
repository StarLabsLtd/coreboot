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
source=/home/sean/Documents/.cdk2-worktrees/efivar-unlock-host-target-after672
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
expected=c2689a99e6da23b8cca372469dc52a0a672e434e
test "$(git rev-parse HEAD)" = "$expected"
test "$(git status --porcelain --untracked-files=all)" = ' M Makefile
 M src/boot/Makefile'
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d' ' -f1)" = 43ae07f15ebf10dd646edea5640f3e034717f5776c6da3eca9384b5edc11c15a
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
inputs=("$source" "$producer" "$vendors/bearssl" "$vendors/lvgl"
    "$origin/payload/include/cdk2/config.h" "$origin/payload-resolved.config"
    "$origin/payload-input.config" "$origin/producer-resolved.config")
tools=(bash sh python3 git ssh-keygen make cc gcc ld as ar objcopy objdump nm readelf
    kconfig-conf awk sed grep sha256sum stat cp cmp head mktemp rm dirname sort
    readlink realpath cut mkdir env mv xargs perl find cat tr flock wc basename tar touch)
paths() {
    local name path
    for name in "${tools[@]}"; do
        path=$(command -v "$name")
        printf '%s\t%s\t%s\n' "$name" "$path" "$(readlink -f "$path")"
    done
}
paths > "$receipt/tool-paths-before.tsv"
while IFS=$'\t' read -r name path resolved; do inputs+=("$path"); done < "$receipt/tool-paths-before.tsv"
inputs+=(/usr/bin/time "$(perl -MErrno -e 'print $INC{"Errno.pm"}')")
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
    sha256sum -c "$receipt/recipe-before.sha256" > "$receipt/recipe-check.log" 2>&1 || closure=1
    for kind in config compiler-inputs cache-source; do
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
gate compiler-inputs gcc -std=c11 -D_GNU_SOURCE -M "$source/tests/efivar_unlock_test.c"
perl -MText::ParseWords=shellwords -0777 -ne '
    s/\\\n/ /g;
    for (split /\n/) { s/^[^:]+: // or next; s/\$\$/\$/g;
        print join("\n", shellwords($_)), "\n"; }
' "$receipt/compiler-inputs.log" | sort -u > "$receipt/compiler-inputs.txt"
test -s "$receipt/compiler-inputs.txt"
xargs -d '\n' sha256sum < "$receipt/compiler-inputs.txt" > "$receipt/compiler-inputs-before.sha256"
gate public "${make_args[@]}" native-efivar-unlock-test
test -x "$build/native/efivar-unlock-test"
test -s "$build/native/efivar-unlock-test.d"
grep -Fq 'efivar-unlock.c' "$build/native/efivar-unlock-test.d"
gate database "${make_args[@]}" -pn native-efivar-unlock-test
grep -F "$build/native/efivar-unlock-test:" "$receipt/database.log" > "$receipt/registered-target.txt"
perl -MText::ParseWords=shellwords -ne '
    s/^[^:]+:\s*// or next;
    print join("\n", shellwords($_)), "\n";
' "$receipt/registered-target.txt" |
    while IFS= read -r path; do realpath -m -s -- "$path"; done \
    > "$receipt/registered-prerequisites.txt"
grep -Fxq "$source/util/qemu/fixtures/linux-mini/efivar-unlock.c" \
    "$receipt/registered-prerequisites.txt"
mkdir "$receipt/cache-source" "$receipt/cache-build"
git archive HEAD | tar -x -C "$receipt/cache-source"
cp "$source/Makefile" "$receipt/cache-source/Makefile"
cp "$source/src/boot/Makefile" "$receipt/cache-source/src/boot/Makefile"
find "$receipt/cache-source" -type f -print0 | sort -z | xargs -0 sha256sum > "$receipt/cache-source-before.sha256"
cp "$origin/payload-input.config" "$receipt/cache-build/.config"
cache_args=(make -C "$receipt/cache-source" -j1 "CDK2_BUILD_DIR=$receipt/cache-build"
    "CDK2_CONFIG=$receipt/cache-build/.config" "COREBOOT_TREE=$producer"
    "COREBOOT_CONFIG=$origin/producer-resolved.config"
    "CDK2_BEARSSL_DIR=$vendors/bearssl" "CDK2_LVGL_ROOT=$vendors/lvgl" CC=cc HOSTCC=cc)
gate cache-public "${cache_args[@]}" native-efivar-unlock-test
# Only the task-owned tracked-source copy is touched, never the frozen author.
touch "$receipt/cache-source/util/qemu/fixtures/linux-mini/efivar-unlock.c"
gate cache-rebuild "${cache_args[@]}" --trace native-efivar-unlock-test
grep -F 'efivar-unlock.c' "$receipt/cache-rebuild.log" > "$receipt/helper-rebuild-reason.txt"
grep -Fq 'due to:' "$receipt/helper-rebuild-reason.txt"
test -s "$receipt/cache-build/native/efivar-unlock-test.d"
execution=0
