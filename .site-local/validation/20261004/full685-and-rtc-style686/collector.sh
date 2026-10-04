#!/bin/bash
set -euo pipefail
recipe=/home/sean/full685-rtc-style-evidence-collector.aFuTm9
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/full685-and-rtc-style686
test ! -e "$destination" && test ! -L "$destination"
full=/home/sean/full-lint-after685-final.20261004-r1
style=/home/sean/rtc-fixture-style-final.20261004-r1
test "$(find "$full" -maxdepth 1 -type f -name '*.status' | wc -l)" = 14
test "$(find "$style" -maxdepth 1 -type f -name '*.status' | wc -l)" = 25
for directory in "$full" "$style"; do
    while IFS= read -r -d '' file; do
        expected=0
        case "$directory/${file##*/}" in
            "$full/lint.status") expected=2 ;;
            "$full/full-checkpatch.status"|"$full/execution.status"|"$full/aggregate.status"|"$style/old-style.status") expected=1 ;;
        esac
        test "$(cat "$file")" = "$expected"
    done < <(find "$directory" -maxdepth 1 -type f -name '*.status' -print0)
done
grep -Fxq 'total: 3 errors, 0 warnings, 6648 lines checked' "$full/full-checkpatch.log"
test "$(grep -c '^ERROR:SPACING:' "$full/full-checkpatch.log")" = 2
test "$(grep -c '^ERROR:TRAILING_STATEMENTS:' "$full/full-checkpatch.log")" = 1
test ! -s "$style/current-style.log"
for mode in o0 o2 asan ubsan; do
    grep -Fxq 'cdk2 coreboot test: PASS' "$style/$mode-coreboot.log"
done
sha256sum --quiet -c <<'PINS'
bf0916425ce793f3cd62e541ef8d5f98f922812ac242757c9376e7e4fb063ee4  /home/sean/full-lint-after685-recipe.gihXSc/run.sh
2edf942d9cde1bd2bd4cc1a483ee0a44a125b952fe6701b4f4bfcb0dabdd1eb5  /home/sean/full-lint-after685-recipe.gihXSc/README.txt
827e515738d08dbccc6d1d7064ee595256d58fe875cf03c5604579ee30bbbebb  /home/sean/full-lint-after685-recipe.gihXSc/hash-inputs.py
ad2caa9c86e0bf881a753de6e9d8d2ed8116381ae208acfe02e672b5747f99ff  /home/sean/rtc-fixture-style-host-recipe.eRcy5U/run.sh
e9cce413190bd8a207d8305ed66839a73aea7dcbf7fd17e7d663f35b9eb19087  /home/sean/rtc-fixture-style-host-recipe.eRcy5U/README.txt
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/rtc-fixture-style-host-recipe.eRcy5U/hash-inputs.py
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
collect_root full685-failed "$full"
collect_root rtc-style686-host "$style"
for directory in /home/sean/full-lint-after685-recipe.gihXSc /home/sean/rtc-fixture-style-host-recipe.eRcy5U; do
    case "$directory" in *gihXSc) label=full685-recipe ;; *) label=rtc-style686-recipe ;; esac
    for file in run.sh README.txt hash-inputs.py; do copy_file "$label" "$directory/$file"; done
done
sha256sum --quiet -c "$destination/ORIGINAL_FILES.sha256"
cp -p "$recipe/collect.sh" "$destination/collector.sh"
cp -p "$recipe/README.md" "$destination/README.md"
cmp "$recipe/collect.sh" "$destination/collector.sh"
cmp "$recipe/README.md" "$destination/README.md"
(
    cd "$destination"
    find . -type f ! -name ARCHIVE.sha256 -print0 | sort -z | xargs -0 sha256sum > ARCHIVE.sha256
    sha256sum --quiet -c ARCHIVE.sha256
)
