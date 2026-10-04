#!/bin/bash
set -euo pipefail
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C
recipe=$(cd -- "$(dirname -- "$0")" && pwd -P)
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/host-regression-fixes-after661
test ! -e "$destination"
mkdir -p "$destination"
: > "$destination/ORIGINAL_FILES.sha256"
printf 'original_sha256\toriginal_path\tstored_relative_path\tencoding\n' > "$destination/FILE_MAP.tsv"

copy_one() {
	local label=$1 source=$2 name=$3 path stored digest encoding=original
	path=$source/$name
	test -f "$path" && test ! -L "$path"
	case "$name" in */*|*[$'\t\n']*) echo 'non-flat input name refused' >&2; exit 1;; esac
	mkdir -p "$destination/$label"
	stored=$label/$name
	digest=$(sha256sum "$path" | cut -d' ' -f1)
	printf '%s  %s\n' "$digest" "$path" >> "$destination/ORIGINAL_FILES.sha256"
	if test "$(stat -c '%s' "$path")" -gt 1048576; then
		stored=$stored.gz
		encoding=gzip-n
		gzip -n -c "$path" > "$destination/$stored"
		gzip -t "$destination/$stored"
		cmp "$path" <(gzip -cd "$destination/$stored")
	else
		cp -p "$path" "$destination/$stored"
		cmp "$path" "$destination/$stored"
	fi
	printf '%s\t%s\t%s\t%s\n' "$digest" "$path" "$stored" "$encoding" >> "$destination/FILE_MAP.tsv"
}

copy_receipts() {
	local label=$1 source=$2 path name
	test -d "$source" && test ! -L "$source"
	# Flat selected original receipts only. No audit-build/tmp descent, raw media,
	# executable/firmware/object/archive, imported source or generic Python scan.
	while IFS= read -r -d '' path; do
		name=${path##*/}
		case "$name" in
			*.log|*.txt|*.time|*.status|*.sha256|*.sh|*.md|*.diff|*.list|*.json|*.tsv|*.command)
				copy_one "$label" "$source" "$name";;
		esac
	done < <(find "$source" -maxdepth 1 -type f -print0 | sort -z)
}

copy_receipts pr659-window-host /home/sean/capsule-window-report-link-gates.fDb8NU
copy_receipts pr660-protected-host /home/sean/protected-boot-hob-final.ZS59fV
copy_receipts pr661-cache-first-failed /home/sean/capsule-report-cache-host-final.20261004-r1
copy_receipts pr661-cache-retry-passed /home/sean/capsule-report-cache-host-retry.20261004-r2
for group in cache-first-recipe cache-retry-recipe; do
	if test "$group" = cache-first-recipe; then
		source=/home/sean/capsule-report-cache-host-recipe.pGyK7O
	else
		source=/home/sean/capsule-report-cache-host-retry-recipe.I7xlfG
	fi
	for name in run.sh audit.py hash-inputs.py remove.mk README.txt; do
		copy_one "$group" "$source" "$name"
	done
done

source=/home/sean/default-regression-after661.E4XtC9
# Refuse to label executed output as an unexecuted source recipe.
test ! -e "$source/build" && test ! -e "$source/tmp"
test ! -e "$source/regression.log" && test ! -e "$source/execution.status"
test "$(sha256sum "$source/run.sh" | cut -d' ' -f1)" = 305719e677faf28a5e13febd0c56f89f880a5cf4889b61fa920f9b2bd07f5f54
test "$(sha256sum "$source/PREPARATION.txt" | cut -d' ' -f1)" = cb3aa4acb6227f46839bfc307fb0603154644bac8989ace2559b1a42a43390bf
copy_one default661-UNEXECUTED-recipe "$source" run.sh
copy_one default661-UNEXECUTED-recipe "$source" PREPARATION.txt
copy_one packet "$recipe" collect.sh
copy_one packet "$recipe" README.txt
# Detect any original change during copying; never overwrite or reclassify it.
sha256sum -c "$destination/ORIGINAL_FILES.sha256"
(
	cd "$destination"
	find . -type f ! -name ARCHIVE.sha256 -print0 | sort -z | xargs -0 sha256sum > ARCHIVE.sha256
	sha256sum -c ARCHIVE.sha256
)
