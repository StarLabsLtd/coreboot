#!/bin/bash
set -euo pipefail
recipe=/home/sean/uart686-stable-evidence-collector.gyRBSp
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/uart-unique-r1-r5-and-stable686
test ! -e "$destination" && test ! -L "$destination"
uart1=/home/sean/uart-unique-final.20261004-r1
uart2=/home/sean/uart-unique-final.20261004-r2
uart3=/home/sean/uart-unique-final.20261004-r3
uart4=/home/sean/uart-unique-final.20261004-r4
uart5=/home/sean/uart-unique-final.20261004-r5
stable=/home/sean/stable-lint-after686.20261004-r1

# Root must independently qualify the closed r5 receipt before executing this collector.
for directory in "$uart1" "$uart2" "$uart3" "$uart4" "$uart5" "$stable"; do
    test -d "$directory" && test ! -L "$directory"
    test -f "$directory/closure.status" && test ! -L "$directory/closure.status"
    case "$directory" in
        "$uart1") count=9 ;;
        "$uart2"|"$uart3") count=11 ;;
        "$uart4") count=17 ;;
        "$uart5") count=74 ;;
        "$stable") count=11 ;;
    esac
    test "$(find "$directory" -maxdepth 1 -type f -name '*.status' | wc -l)" = "$count"
    while IFS= read -r -d '' file; do
        expected=0
        case "$directory/${file##*/}" in
            "$uart1/execution.status"|"$uart1/aggregate.status"|\
            "$uart2/execution.status"|"$uart2/aggregate.status"|\
            "$uart3/execution.status"|"$uart3/aggregate.status"|\
            "$uart4/execution.status"|"$uart4/aggregate.status") expected=1 ;;
            "$uart2/allowlist-prerequisite.status"|"$uart3/allowlist-prerequisite.status"|\
            "$uart4/original-disabled-public.status") expected=2 ;;
            "$uart5/o0-cdk2_coreboot_fill_diagnostic_handoff.status"|\
            "$uart5/o2-cdk2_coreboot_fill_diagnostic_handoff.status"|\
            "$uart5/o0-cdk2_coreboot_append_serial_hobs.status"|\
            "$uart5/o2-cdk2_coreboot_append_serial_hobs.status"|\
            "$uart5/o0-cdk2_coreboot_validate_handoff.status"|\
            "$uart5/o2-cdk2_coreboot_validate_handoff.status") expected=1 ;;
        esac
        test "$(cat "$file")" = "$expected"
    done < <(find "$directory" -maxdepth 1 -type f -name '*.status' -print0)
done
grep -Fxq 'ERROR: deadline first-match mutant missed the duplicate oracle' "$uart4/original-disabled-public.log"
for mode in o0 o2 asan ubsan; do
    grep -Fxq 'cdk2 coreboot test: PASS' "$uart5/disabled-$mode-coreboot.log"
    grep -Fxq 'cdk2 coreboot test: PASS' "$uart5/$mode-coreboot.log"
done
for path in include/cdk2/diagnostic.h src/boot/coreboot.h src/boot/coreboot_handoff.c src/boot/coreboot_test.c src/lib/diagnostic.c; do
    test ! -s "$uart5/style-${path//\//-}.log"
done
grep -Fq 'lint-stable-025-native-boundary): success' "$stable/lint-stable.log"
grep -Fq 'lint-stable-029-native-types): success' "$stable/lint-stable.log"

sha256sum --quiet -c <<'PINS'
9cd530e0a607207ad84f570203a994673f95713d43b55b8530bfce1ee43a1433  /home/sean/uart-unique-host-style-recipe.1kJY6I/run.sh
aaa3dbf9cd1a88b6b0c064b6656ee0061191f1dc82f135451efb46dffdc5f8ed  /home/sean/uart-unique-host-style-recipe.1kJY6I/README.txt
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/uart-unique-host-style-recipe.1kJY6I/hash-inputs.py
edb749fca641549cc0e5541dd9727392cad68d630026cb1f57ab634822badc47  /home/sean/uart-unique-derived-host-recipe.TyNSz5/run.sh
0f4566584208374eb5f67e5681378ac02879388704de24775ab7d18fcaf915ea  /home/sean/uart-unique-derived-host-recipe.TyNSz5/README.txt
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/uart-unique-derived-host-recipe.TyNSz5/hash-inputs.py
fc45932de72497e1647f59081d5442de46b167c321f611e9dbc59e0be871547d  /home/sean/uart-unique-derived-style-retry.XJ6Brx/run.sh
4e87f4cb9bce4468aafde133a35a3ba3b03ff077f4bc98e32a7c35a0ab36c8f5  /home/sean/uart-unique-derived-style-retry.XJ6Brx/README.txt
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/uart-unique-derived-style-retry.XJ6Brx/hash-inputs.py
c23dfff4b310a0eb2ca3494e410441c8d2af9536f4e8f56f5b2cf7121795b91b  /home/sean/uart-unique-cursor-host-retry.yLdPgO/run.sh
16aacb234b75add86639bc858a24a050053bbd5536d3bc5d3acf0ad64b486385  /home/sean/uart-unique-cursor-host-retry.yLdPgO/README.txt
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/uart-unique-cursor-host-retry.yLdPgO/hash-inputs.py
6a2cd226e3a5eda55d90d8bdcb2f861b1b2c09cf64fa2d5177335b54a37d78b4  /home/sean/uart-unique-memory-host-retry.S6zAma/run.sh
a814d946b62f150279e5d67020cd6a78e3edd3d8e4dbb1e0d70e1b0df3c01d22  /home/sean/uart-unique-memory-host-retry.S6zAma/README.txt
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/uart-unique-memory-host-retry.S6zAma/hash-inputs.py
52fc4adebc5c40d7e60c44f6d8541e63065ae48d356ce3777d69bdae26f60bba  /home/sean/stable-lint-after686-recipe.uiorZv/run.sh
de3f5adefece2b7f69500d4f8533630c33802d0a18322a82f9f9fae95b745946  /home/sean/stable-lint-after686-recipe.uiorZv/README.txt
827e515738d08dbccc6d1d7064ee595256d58fe875cf03c5604579ee30bbbebb  /home/sean/stable-lint-after686-recipe.uiorZv/hash-inputs.py
PINS

mkdir -- "$destination"
: > "$destination/ORIGINAL_FILES.sha256"
: > "$destination/FILES_MAP.tsv"
copy_file()
{
    local label=$1 path=$2 target
    test -f "$path" && test ! -L "$path"
    target=$destination/$label/${path##*/}
    test ! -e "$target" && test ! -e "$target.gz"
    mkdir -p "$(dirname "$target")"
    sha256sum "$path" >> "$destination/ORIGINAL_FILES.sha256"
    if test "$(stat -c %s "$path")" -gt 1048576; then
        gzip -n -c "$path" > "$target.gz"
        gzip -dc "$target.gz" | cmp "$path" -
        target=$target.gz
    else
        cp -p "$path" "$target"
        cmp "$path" "$target"
    fi
    printf '%s\t%s\n' "$path" "${target#"$destination/"}" >> "$destination/FILES_MAP.tsv"
}
collect_root()
{
    local label=$1 directory=$2 path
    while IFS= read -r -d '' path; do
        case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
            copy_file "$label" "$path" ;;
        esac
    done < <(find "$directory" -maxdepth 1 -type f -print0 | sort -z)
}
collect_root uart-r1-failed "$uart1"
collect_root uart-r2-failed "$uart2"
collect_root uart-r3-failed "$uart3"
collect_root uart-r4-failed "$uart4"
collect_root uart-r5-host "$uart5"
collect_root stable686-host "$stable"
for directory in /home/sean/uart-unique-host-style-recipe.1kJY6I /home/sean/uart-unique-derived-host-recipe.TyNSz5 /home/sean/uart-unique-derived-style-retry.XJ6Brx /home/sean/uart-unique-cursor-host-retry.yLdPgO /home/sean/uart-unique-memory-host-retry.S6zAma /home/sean/stable-lint-after686-recipe.uiorZv; do
    case "$directory" in
        *1kJY6I) label=uart-r1-recipe ;;
        *TyNSz5) label=uart-r2-recipe ;;
        *XJ6Brx) label=uart-r3-recipe ;;
        *yLdPgO) label=uart-r4-recipe ;;
        *S6zAma) label=uart-r5-recipe ;;
        *uiorZv) label=stable686-recipe ;;
    esac
    for file in run.sh README.txt hash-inputs.py; do copy_file "$label" "$directory/$file"; done
done
sha256sum --quiet -c "$destination/ORIGINAL_FILES.sha256"
cp -p "$recipe/collect.sh" "$destination/collector.sh"
cp -p "$recipe/README.txt" "$destination/README.txt"
cmp "$recipe/collect.sh" "$destination/collector.sh"
cmp "$recipe/README.txt" "$destination/README.txt"
(
    cd "$destination"
    find . -type f ! -name ARCHIVE.sha256 -print0 | sort -z | xargs -0 sha256sum > ARCHIVE.sha256
    sha256sum --quiet -c ARCHIVE.sha256
)
