#!/bin/bash
# UNEXECUTED metadata-only archive copier; Root execution after opposing review.
set -euo pipefail
umask 077
export PATH=/usr/local/bin:/usr/bin:/bin LANG=C LC_ALL=C
proposal=/home/sean/default661-metadata-archive-proposal.cMhjvu
receipt=/home/sean/default661-input-retention.20261004-r1
input_root=/home/sean/default-regression-after661.E4XtC9
local_archive=/home/sean/Documents/cdk2-validation/retained-inputs/default661-generated-inputs-20261004.tar.gz
packet=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/default661-local-retained-inputs
files=(FILES_MAP.tsv README.txt all-original-files-after.list all-original-files.list
	archive-members.list collector.sh excluded-after-check.log excluded-originals.sha256
	execution.status extracted-byte-comparisons.tsv local-archive-size.txt
	local-archive.sha256 recipe-after-check.log recipe-before.sha256
	selected-after-check.log selected-originals.sha256 selected-sorted.list selected.list)
test "$(realpath -e "${packet%/*}")" = "${packet%/*}"
test ! -e "$packet" && test ! -L "$packet"
test -d "$receipt" && test ! -L "$receipt"
test "$(cat "$receipt/execution.status")" = 0
test "$(wc -l < "$receipt/selected.list")" = 59
test "$(wc -l < "$receipt/extracted-byte-comparisons.tsv")" = 59
awk -F '\t' 'NF!=2 || $2!="0" {bad=1} END {exit bad}' "$receipt/extracted-byte-comparisons.tsv"
for file in "${files[@]}"; do
	test -f "$receipt/$file" && test ! -L "$receipt/$file"
	test "$(realpath -e "$receipt/$file")" = "$receipt/$file"
done
test -z "$(find "$receipt" ! -type f ! -type d -print -quit)"
cmp <(printf '%s\n' "${files[@]}" | sort) <(find "$receipt" -mindepth 1 -maxdepth 1 -printf '%f\n' | sort)
sha256sum -c "$receipt/local-archive.sha256" >/dev/null
# These are current collector ledgers, not the old regression's source snapshot.
sha256sum -c "$receipt/selected-originals.sha256" >/dev/null
sha256sum -c "$receipt/excluded-originals.sha256" >/dev/null
sha256sum -c "$receipt/recipe-before.sha256" >/dev/null
cmp "$receipt/all-original-files.list" <(find "$input_root" -type f -printf '%P\n' | sort)
cmp "$receipt/selected-sorted.list" <(tar -tzf "$local_archive" | sort)

mkdir "$packet" "$packet/actual-retention"
for file in "${files[@]}"; do
	cp "$receipt/$file" "$packet/actual-retention/$file"
	cmp "$receipt/$file" "$packet/actual-retention/$file"
	printf '%s\t%s\n' "$receipt/$file" "actual-retention/$file"
done > "$packet/FILES_MAP.tsv"
cp "$proposal/collect.sh" "$packet/metadata-collector.sh"
cp "$proposal/README.txt" "$packet/README.txt"
cmp "$proposal/collect.sh" "$packet/metadata-collector.sh"
cmp "$proposal/README.txt" "$packet/README.txt"
(cd "$packet" && find . -type f ! -name ARCHIVE.sha256 -print0 | sort -z | xargs -0 sha256sum) > "$packet/ARCHIVE.sha256"
(cd "$packet" && sha256sum -c ARCHIVE.sha256)
# No Git operations, source edits, test/firmware execution, or deletion.
# Root must independently qualify actual bytes, force-add ignored log counterparts,
# commit/sign/push this exact packet, and prove every stored path is in that commit
# before any original default661 directory retirement.
