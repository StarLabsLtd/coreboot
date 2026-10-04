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
source=/home/sean/Documents/.cdk2-worktrees/uart-unique-authority-after684
canonical=/home/sean/Documents/cdk2
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
mbedtls=$producer/3rdparty/mbedtls
normal=/home/sean/fresh-normal-focus-after657.CftZZg/build
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
expected=ad04476fe802bf3c051beb2d7d9f212d95d0e6d0
test "$(git rev-parse HEAD)" = "$expected"
test "$(git status --porcelain --untracked-files=all)" = ' M include/cdk2/diagnostic.h
 M src/boot/coreboot.h
 M src/boot/coreboot_handoff.c
 M src/boot/coreboot_test.c
 M src/lib/diagnostic.c'
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d' ' -f1)" = 40a5c2ee56beedfd4dd830f3c09b6118a8685b7706e1f2c77e946c79b70ee126
test "$(git -C "$canonical" rev-parse HEAD)" = a3ed5f42934cf4ff7f8171ba5a709c2c31f801a6
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test "$(git -C "$mbedtls" rev-parse HEAD)" = 0bebf8b8c7f07abe3571ded48a11aa907a1ffb20
test "$(git -C "$canonical/3rdparty/bearssl" rev-parse HEAD)" = 8ef7680081c61b486622f2d983c0d3d21e83caad
test "$(git -C "$canonical/3rdparty/lvgl" rev-parse HEAD)" = 85aa60d18b3d5e5588d7b247abf90198f07c8a63
for tree in "$canonical" "$producer" "$mbedtls" "$canonical/3rdparty/bearssl" "$canonical/3rdparty/lvgl"; do
    test -z "$(git -C "$tree" status --porcelain --untracked-files=all)"
done
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
inputs=("$source" "$canonical" "$producer" "$mbedtls"
    "$source/3rdparty/bearssl" "$source/3rdparty/lvgl"
    "$canonical/3rdparty/bearssl" "$canonical/3rdparty/lvgl"
    "$normal/resolved.config" "$normal/input.config" "$normal/producer-input.config"
    "$normal/include/cdk2/config.h")
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
checker_state() {
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
test -f "$source/.checkpatch.conf" && test ! -L "$source/.checkpatch.conf"
checker_state > "$receipt/checker-config-before.txt"
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
    checker_state > "$receipt/checker-config-after.txt" || closure=1
    cmp "$receipt/checker-config-before.txt" "$receipt/checker-config-after.txt" > "$receipt/checker-config-check.log" 2>&1 || closure=1
    paths > "$receipt/tool-paths-after.tsv" || closure=1
    cmp "$receipt/tool-paths-before.tsv" "$receipt/tool-paths-after.tsv" > "$receipt/tool-paths-check.log" 2>&1 || closure=1
    sha256sum -c "$receipt/recipe-before.sha256" > "$receipt/recipe-check.log" 2>&1 || closure=1
    for kind in config mutation compiler-inputs outputs; do
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
gate canonical-signature git -C "$canonical" verify-commit HEAD
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
grep -Fxq '#define CONFIG_CDK2_SERIAL 0' "$build/include/cdk2/config.h"
disabled_build=$build
sha256sum "$build/.config" "$build/include/cdk2/config.h" > "$receipt/config-before.sha256"

# Fail on genuine source-quality regressions before model compilation.
gate checkpatch-filter-inputs gcc -std=c11 -M -MT model-dependencies \
    "$source/util/lint/cdk2-checkpatch-filter.c"
gate allowlist-prerequisite "${make_args[@]}" checkpatch-allowlist-check
for path in include/cdk2/diagnostic.h src/boot/coreboot.h src/boot/coreboot_handoff.c src/boot/coreboot_test.c src/lib/diagnostic.c; do
    label=style-${path//\//-}
    gate "$label" perl util/lint/checkpatch.pl --no-tree --show-types --file --quiet \
        --typedefsfile util/lint/cdk2-typedefs.checkpatch --max-line-length=96 "$path"
    test ! -s "$receipt/$label.log"
done
gate original-disabled-public "${make_args[@]}" native-coreboot-test native-cbmem-console-test
enabled_build=$receipt/enabled-build
mkdir "$enabled_build"
gate derive-serial-input python3 - "$normal/input.config" "$enabled_build/.config" <<'PY'
from pathlib import Path
import sys
text = Path(sys.argv[1]).read_text()
before = '# CONFIG_CDK2_SERIAL is not set\n'
if text.count(before) != 1 or 'CONFIG_CDK2_SERIAL=y\n' in text:
    raise ValueError('original normal input serial selection not unique')
Path(sys.argv[2]).write_text(text.replace(before, 'CONFIG_CDK2_SERIAL=y\n'))
print('only exact original input serial selection changed')
PY
enabled_make_args=(make -C "$source" -j1 "CDK2_BUILD_DIR=$enabled_build" "CDK2_CONFIG=$enabled_build/.config"
    "COREBOOT_TREE=$producer" "COREBOOT_CONFIG=$normal/producer-input.config"
    "MBEDTLS_SOURCE=$mbedtls" CC=cc HOSTCC=cc)
gate configure-enabled "${enabled_make_args[@]}" CDK2_CONFIG_READY= olddefconfig
gate admit-single-serial-difference python3 - "$normal/resolved.config" "$enabled_build/.config" \
    "$normal/include/cdk2/config.h" "$enabled_build/include/cdk2/config.h" <<'PY'
from pathlib import Path
import sys
config, actual_config, header, actual_header = (Path(value) for value in sys.argv[1:])
for baseline, actual, before, after in [
    (config, actual_config, b'# CONFIG_CDK2_SERIAL is not set\n', b'CONFIG_CDK2_SERIAL=y\n'),
    (header, actual_header, b'#define CONFIG_CDK2_SERIAL 0\n', b'#define CONFIG_CDK2_SERIAL 1\n')]:
    original = baseline.read_bytes()
    if original.count(before) != 1 or after in original:
        raise ValueError('original disabled selection is not unique')
    if actual.read_bytes() != original.replace(before, after):
        raise ValueError('genuine resolved derivative changed more than serial selection')
    print(str(actual) + ': exactly serial-only derivative of independently pinned original')
PY
sha256sum "$enabled_build/.config" "$enabled_build/include/cdk2/config.h" >> "$receipt/config-before.sha256"
mkdir "$receipt/mutations"
gate compose-mutations python3 - "$source/src/boot/coreboot_handoff.c" "$receipt/mutations" <<'PY'
from pathlib import Path
import sys
original = Path(sys.argv[1]).read_text()
out = Path(sys.argv[2])
for name in ['cdk2_coreboot_fill_diagnostic_handoff', 'cdk2_coreboot_append_serial_hobs',
             'cdk2_coreboot_validate_handoff']:
    marker = 'static EFI_STATUS ' + name + '('
    if original.count(marker) != 1:
        raise ValueError('actual definition not unique')
    begin = original.index(marker)
    end = original.index('\n}\n', begin) + 3
    body = original[begin:end]
    old = 'cdk2_coreboot_find_unique_record'
    if body.count(old) != 1:
        raise ValueError('serial lookup not unique')
    changed = body.replace(old, 'cdk2_coreboot_find_record', 1)
    candidate = original[:begin] + changed + original[end:]
    inverse = original[:begin] + changed.replace('cdk2_coreboot_find_record', old, 1) + original[end:]
    if inverse != original:
        raise ValueError('mutation inverse changed source')
    (out / (name + '.c')).write_text(candidate)
    (out / (name + '.inverse.c')).write_text(inverse)
    print(name + ': one lookup reversion, complete inverse equal')
PY
for name in cdk2_coreboot_fill_diagnostic_handoff cdk2_coreboot_append_serial_hobs cdk2_coreboot_validate_handoff; do
    gate "$name-inverse" cmp "$source/src/boot/coreboot_handoff.c" "$receipt/mutations/$name.inverse.c"
done
find "$receipt/mutations" -type f -print0 | sort -z | xargs -0 sha256sum > "$receipt/mutation-before.sha256"
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
dependency_logs=("$receipt/checkpatch-filter-inputs.log")
for mode in o0 o2 asan ubsan; do
    flags_for_mode
    gate "disabled-$mode-inputs" "${common[@]}" gcc "${flags[@]}" -M -MT model-dependencies "${coreboot_sources[@]}"
    dependency_logs+=("$receipt/disabled-$mode-inputs.log")
done
build=$enabled_build
make_args=("${enabled_make_args[@]}")

for mode in o0 o2 asan ubsan; do
    flags_for_mode
    gate "$mode-inputs" "${common[@]}" gcc "${flags[@]}" -M -MT model-dependencies "${coreboot_sources[@]}"
    dependency_logs+=("$receipt/$mode-inputs.log")
done
for name in cdk2_coreboot_fill_diagnostic_handoff cdk2_coreboot_append_serial_hobs cdk2_coreboot_validate_handoff; do
    mutant_sources=("${coreboot_sources[@]}")
    mutant_sources[8]=$receipt/mutations/$name.c
    for mode in o0 o2; do
        flags_for_mode
        gate "$mode-$name-inputs" "${common[@]}" gcc "${flags[@]}" -M -MT model-dependencies "${mutant_sources[@]}"
        dependency_logs+=("$receipt/$mode-$name-inputs.log")
    done
done
perl -MText::ParseWords=shellwords -0777 -ne '
    s/\\\n/ /g;
    for (split /\n/) { s/^model-dependencies: // or next; s/\$\$/\$/g;
        print join("\n", shellwords($_)), "\n"; }
' "${dependency_logs[@]}" | sort -u > "$receipt/compiler-inputs.txt"
test -s "$receipt/compiler-inputs.txt"
xargs -d '\n' sha256sum < "$receipt/compiler-inputs.txt" > "$receipt/compiler-inputs-before.sha256"
failed=0
build=$disabled_build
for mode in o0 o2 asan ubsan; do
    flags_for_mode
    if gate "disabled-$mode-coreboot-compile" "${common[@]}" gcc "${flags[@]}" "${coreboot_sources[@]}" -o "$build/$mode-coreboot"; then
        gate "disabled-$mode-coreboot" "${common[@]}" "$build/$mode-coreboot" || failed=1
    else failed=1; fi
done
build=$enabled_build
for mode in o0 o2 asan ubsan; do
    flags_for_mode
    if gate "$mode-coreboot-compile" "${common[@]}" gcc "${flags[@]}" "${coreboot_sources[@]}" -o "$build/$mode-coreboot"; then
        gate "$mode-coreboot" "${common[@]}" "$build/$mode-coreboot" || failed=1
    else failed=1; fi
done
for name in cdk2_coreboot_fill_diagnostic_handoff cdk2_coreboot_append_serial_hobs cdk2_coreboot_validate_handoff; do
    mutant_sources=("${coreboot_sources[@]}")
    mutant_sources[8]=$receipt/mutations/$name.c
    case "$name" in
    cdk2_coreboot_fill_diagnostic_handoff) diagnostic='diagnostic HOB producer did not enforce unique UART authority' ;;
    cdk2_coreboot_append_serial_hobs) diagnostic='serial HOB producer did not enforce unique UART authority' ;;
    cdk2_coreboot_validate_handoff) diagnostic='early handoff did not refuse ambiguous UART before DMA admission' ;;
    esac
    for mode in o0 o2; do
        flags_for_mode
        if gate "$mode-$name-compile" "${common[@]}" gcc "${flags[@]}" "${mutant_sources[@]}" -o "$build/$mode-$name"; then
            gate "$mode-$name" "${common[@]}" "$build/$mode-$name" || true
            opposition=0
            test "$(cat "$receipt/$mode-$name.status")" = 1 || opposition=1
            grep -Fxq "cdk2 coreboot test: $diagnostic" "$receipt/$mode-$name.log" || opposition=1
            if grep -Eq 'AddressSanitizer|LeakSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$receipt/$mode-$name.log"; then opposition=1; fi
            printf '%s\n' "$opposition" > "$receipt/$mode-$name-opposition.status"
            test "$opposition" = 0 || failed=1
        else failed=1; fi
    done
done
gate public-host "${make_args[@]}" native-coreboot-test native-cbmem-console-test || failed=1
gate actual-public-dependency-binding python3 - "$source/include/cdk2/diagnostic.h" "$build/native/coreboot-test.d" <<'PY' || failed=1
from pathlib import Path
import shlex
import sys
expected = Path(sys.argv[1]).resolve(strict=True)
rule = Path(sys.argv[2]).read_text().replace('\\\n', ' ').splitlines()[0]
actual = {Path(value).resolve(strict=True) for value in shlex.split(rule.split(':', 1)[1])}
if expected not in actual:
    raise ValueError('actual public dependency rule omitted diagnostic snapshot interface')
print('actual public coreboot .d includes diagnostic interface')
PY
find "$disabled_build" "$enabled_build" -type f -print0 | sort -z | xargs -0 sha256sum > "$receipt/outputs-before.sha256"
execution=$failed
exit "$failed"
