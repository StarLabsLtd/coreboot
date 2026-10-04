#!/bin/bash
set -euo pipefail
recipe=/home/sean/rtc685-doc684-evidence-collector.D0Rdqt
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/rtc685-and-doc684-host
test ! -e "$destination" && test ! -L "$destination"
first=/home/sean/rtc-board-facts-final.20261004-r1
retry=/home/sean/rtc-board-facts-final.20261004-r2
doc=/home/sean/roadmap-after683-text-check.bz5Ezu/receipt
status_is()
{
	test -f "$1/$2.status" && test ! -L "$1/$2.status"
	test "$(cat "$1/$2.status")" = "$3"
}
status_count()
{
	test "$(find "$1" -maxdepth 1 -type f -name '*.status' | wc -l)" = "$2"
}
status_count "$first" 64
status_count "$retry" 68
status_count "$doc" 5
for path in "$first"/*.status; do
	case "${path##*/}" in
	actual-dependency-binding.status|aggregate.status|execution.status|o0-old-rtc.status|\
	o2-debug-rtc-compile.status|o2-old-rtc-compile.status|o2-release-rtc-compile.status)
		value=1 ;;
	public-host.status) value=2 ;;
	*) value=0 ;;
	esac
	name=${path##*/}
	status_is "$first" "${name%.status}" "$value"
done
for path in "$retry"/*.status; do
	case "${path##*/}" in o0-old-rtc.status|o2-old-rtc.status) value=1 ;; *) value=0 ;; esac
	name=${path##*/}
	status_is "$retry" "${name%.status}" "$value"
done
for directory in "$first" "$retry"; do
	status_is "$directory" closure 0
	status_is "$directory" o0-old-rtc 1
	status_is "$directory" o0-old-rtc-opposition 0
	status_is "$directory" native-rtc-pe 0
	status_is "$directory" native-rtc-pe-inspect 0
done
status_is "$first" execution 1
status_is "$first" aggregate 1
status_is "$first" public-host 2
status_is "$first" actual-dependency-binding 1
status_is "$retry" execution 0
status_is "$retry" aggregate 0
status_is "$retry" public-host 0
status_is "$retry" actual-dependency-binding 0
status_is "$retry" o2-old-rtc 1
status_is "$retry" o2-old-rtc-opposition 0
grep -Fq '[-Werror=strict-aliasing]' "$first/o2-release-rtc-compile.log"
grep -Fq 'LVGL sources are unavailable' "$first/public-host.log"
for path in "$first/o0-old-rtc.log" "$retry/o0-old-rtc.log" "$retry/o2-old-rtc.log"; do
	grep -Fxq 'pcat-rtc test: absent-century policy wrote platform CMOS' "$path"
	grep -Fxq 'pcat-rtc test: alternative-century policy was not copied or touched fixed 0x32' "$path"
done
for name in 000-license-headers 003-whitespace 016-non-ascii 021-coreboot-lowercase; do
	status_is "$doc" "$name" 0
	test ! -s "$doc/$name.stdout"
done
status_is "$doc" aggregate 0
sha256sum --quiet -c <<'PINS'
c68a1650a903031dddaf148193e7cde7ec7499bbaa9eaa4727a5a54efc3ca53f  /home/sean/rtc-board-facts-host-recipe.C9HyCP/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/rtc-board-facts-host-recipe.C9HyCP/hash-inputs.py
8f564606668706b5c0421edafc728c089bb3b28a0083dc8f4d863355de7e4bc3  /home/sean/rtc-board-facts-host-recipe.C9HyCP/README.txt
793d300727b2f557546b1c9bb0b0a065382d8f38e4385c55c7025b361ac5397b  /home/sean/rtc-board-facts-host-retry.uM7Py9/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/rtc-board-facts-host-retry.uM7Py9/hash-inputs.py
da35a5645ed44a76ce21a087358ff1fd8a1092a3203c78190fff331c0e26df50  /home/sean/rtc-board-facts-host-retry.uM7Py9/README.txt
b21f794b63843e1770fa1331da6dc6be13886e30e6e9e51edaa362e0e86ee18d  /home/sean/roadmap-after683-text-check.bz5Ezu/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/roadmap-after683-text-check.bz5Ezu/hash-inputs.py
PINS
mkdir "$destination"
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
		case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list|*.stdout|*.stderr)
			copy_file "$label" "$path" ;;
		esac
	done < <(find "$directory" -maxdepth 1 -type f -print0 | sort -z)
}
collect_root rtc-first-failed "$first"
collect_root rtc-retry "$retry"
collect_root doc684 "$doc"
for directory in /home/sean/rtc-board-facts-host-recipe.C9HyCP \
    /home/sean/rtc-board-facts-host-retry.uM7Py9; do
	case "$directory" in *C9HyCP) label=rtc-first-recipe ;; *) label=rtc-retry-recipe ;; esac
	for file in run.sh hash-inputs.py README.txt; do copy_file "$label" "$directory/$file"; done
done
for file in run.sh hash-inputs.py; do
	copy_file doc684-recipe "/home/sean/roadmap-after683-text-check.bz5Ezu/$file"
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
