#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
repo=${1:?signed source repository is required}
packet=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$packet"
sha256sum -c SHA256SUMS
temporary=$(mktemp -d /home/sean/acpi-owner-verify.XXXXXX)
for item in \
	'type/baseline-source.tar.zst:15c3bc1109ab39f06d1298895fdd95ae0dec6759' \
	'type/signed-source.tar.zst:7d39fb416b3a34b257e3abe124491608cf0b247d' \
	'deletion/signed-source.tar.zst:33c586738eb34b08d05c24e991a276169dae5280'; do
	archive=${item%%:*}
	commit=${item#*:}
	zstd -t "$archive"
	zstd -dc "$archive" | tar -tf - > "$temporary/members"
	while IFS= read -r member; do
		case "$member" in */) continue ;; esac
		git -C "$repo" show "$commit:$member" > "$temporary/git-file"
		zstd -dc "$archive" | tar -xOf - "$member" > "$temporary/archive-file"
		cmp "$temporary/git-file" "$temporary/archive-file"
	done < "$temporary/members"
done
for stage in type deletion; do
	for profile in p0 p1; do
		for optimization in 0 2 s; do
			cmp "$stage/outputs/$profile-baseline-o$optimization.bytes" \
				"$stage/outputs/$profile-candidate-o$optimization.bytes"
			cmp "$stage/outputs/$profile-baseline-o$optimization.layout" \
				"$stage/outputs/$profile-candidate-o$optimization.layout"
		done
	done
done
printf 'Signed archive blobs and twelve actual native byte/layout pairs PASS\n'
printf 'Read-only verification copies preserved at %s\n' "$temporary"
