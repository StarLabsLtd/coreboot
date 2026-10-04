#!/bin/bash
# SOURCE-ONLY proposal: Root must obtain opposing source review before execution.
set -euo pipefail
umask 077
export PATH=/usr/local/bin:/usr/bin:/bin
export LANG=C LC_ALL=C
proposal=/home/sean/default661-retained-inputs-successor.ZRd18j
input_root=/home/sean/default-regression-after661.E4XtC9
receipt=/home/sean/default661-input-retention.20261004-r1
archive=/home/sean/Documents/cdk2-validation/retained-inputs/default661-generated-inputs-20261004.tar.gz
partial=$archive.partial
manifest=$proposal/selected.list

# No overwrite, source edit, Git write, firmware execution, or deletion.
test -d "$input_root" && test ! -L "$input_root"
test -d "${archive%/*}" && test ! -L "${archive%/*}"
test ! -e "$receipt" && test ! -e "$archive" && test ! -e "$partial"
test ! -L "$receipt" && test ! -L "$archive" && test ! -L "$partial"
test "$(realpath -e "${archive%/*}")" = "${archive%/*}"
test -f "$manifest" && test ! -L "$manifest"
test "$(wc -l < "$manifest")" = 59
test "$(sort -u "$manifest" | wc -l)" = 59
while IFS= read -r relative; do
	case "$relative" in build/.config|build/include/cdk2/config.h|tmp/*) ;; *) exit 1;; esac
	case "$relative" in /*|*..*|*\\*|*.key|*PRIVATE*) exit 1;; esac
	input=$input_root/$relative
	test -f "$input" && test ! -L "$input"
	test "$(realpath -e "$input")" = "$input"
done < "$manifest"
# The selected PEMs are public certificates, not private signing keys.
for signer in trusted rogue; do
	pem=$input_root/tmp/cdk2-system-fmp-future-floor.dNuT3J/$signer.pem
	test "$(head -n 1 "$pem")" = '-----BEGIN CERTIFICATE-----'
	! grep -q 'PRIVATE KEY' "$pem"
done
test -z "$(find "$input_root" ! -type f ! -type d -print -quit)"
mkdir "$receipt"
finish()
{
	local raw=$?
	trap - EXIT
	printf '%s\n' "$raw" > "$receipt/execution.status"
}
trap finish EXIT
cp "$manifest" "$receipt/selected.list"
cp "$proposal/collect.sh" "$receipt/collector.sh"
cp "$proposal/README.txt" "$receipt/README.txt"
sha256sum "$proposal/collect.sh" "$manifest" "$proposal/README.txt" > "$receipt/recipe-before.sha256"
while IFS= read -r relative; do
	sha256sum "$input_root/$relative"
done < "$manifest" > "$receipt/selected-originals.sha256"
while IFS= read -r relative; do
	printf '%s\t%s\t%s\n' "$input_root/$relative" "$archive" "$relative"
done < "$manifest" > "$receipt/FILES_MAP.tsv"

# Text/SHA metadata records every exclusion; no excluded contents are archived.
find "$input_root" -type f -printf '%P\n' | sort > "$receipt/all-original-files.list"
while IFS= read -r relative; do
	if ! grep -Fxq -- "$relative" "$manifest"; then
		sha256sum "$input_root/$relative"
	fi
done < "$receipt/all-original-files.list" > "$receipt/excluded-originals.sha256"

# Deterministic tar metadata and gzip header, exact finite regular-file list.
tar --format=ustar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
	--no-recursion -C "$input_root" -T "$manifest" -cf - | gzip -n > "$partial"
gzip -t "$partial"
tar -tzf "$partial" | sort > "$receipt/archive-members.list"
sort "$manifest" > "$receipt/selected-sorted.list"
cmp "$receipt/selected-sorted.list" "$receipt/archive-members.list"
while IFS= read -r relative; do
	status=0
	tar -xOzf "$partial" -- "$relative" | cmp "$input_root/$relative" - || status=$?
	printf '%s\t%s\n' "$relative" "$status"
	test "$status" = 0
done < "$manifest" > "$receipt/extracted-byte-comparisons.tsv"
sha256sum -c "$receipt/selected-originals.sha256" > "$receipt/selected-after-check.log"
sha256sum -c "$receipt/excluded-originals.sha256" > "$receipt/excluded-after-check.log"
sha256sum -c "$receipt/recipe-before.sha256" > "$receipt/recipe-after-check.log"
find "$input_root" -type f -printf '%P\n' | sort > "$receipt/all-original-files-after.list"
cmp "$receipt/all-original-files.list" "$receipt/all-original-files-after.list"
mv -n "$partial" "$archive"
test ! -e "$partial" && test -f "$archive"
sha256sum "$archive" > "$receipt/local-archive.sha256"
stat -c 'bytes=%s path=%n' "$archive" > "$receipt/local-archive-size.txt"
# Literal inputs remain present; only Root may retire after opposing actual review.
