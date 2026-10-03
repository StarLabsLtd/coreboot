#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -eu
packet=$1
repository=$2
temporary=$(mktemp -d /home/sean/linear-api-packet-verify.XXXXXX)
trap 'rmdir "$temporary" 2>/dev/null || true' EXIT HUP INT TERM
(cd "$packet" && sha256sum -c SHA256SUMS)
baseline=$(sed -n '1p' "$packet/source/baseline-identity.txt")
candidate=$(sed -n '1p' "$packet/source/signed-identity.txt")
for archive in baseline-596 candidate-598 unchanged-proof-dependencies-596; do
	commit=$baseline
	if test "$archive" = candidate-598; then commit=$candidate; fi
	zstd -tq "$packet/source/$archive.tar.zst"
	tar --zstd -tf "$packet/source/$archive.tar.zst" |
	while IFS= read -r path; do
		case "$path" in */) continue;; esac
		git -C "$repository" show "$commit:$path" > "$temporary/git-blob"
		tar --zstd -xOf "$packet/source/$archive.tar.zst" "$path" |
			cmp "$temporary/git-blob" -
	done
done
for profile in p0 p1; do
	for optimization in 0 2 s; do
		for suffix in bytes layout; do
			cmp "$packet/proof/$profile-baseline-o$optimization.$suffix" \
				"$packet/proof/$profile-candidate-o$optimization.$suffix"
		done
	done
done
rm "$temporary/git-blob"
printf 'Manifest / signed regular source blobs / six bytes-layout pairs PASS\n'
