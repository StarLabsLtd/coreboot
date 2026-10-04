#!/bin/bash
set -Eeuo pipefail
recipe=/home/sean/whole689-evidence-collector.Y8iRkl
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/whole689-and-full688-provenance
test ! -e "$destination" && test ! -L "$destination"
normal=/home/sean/whole-normal-after689.20261004-r1
default=/home/sean/whole-default-after689.20261004-r1
full=/home/sean/full-lint-after688-final.20261004-r1
provenance=/home/sean/review-profile-provenance-final.20261004-r1
status_vector()
{
	local directory=$1 count=$2 path expected
	test -d "$directory" && test ! -L "$directory"
	test "$(readlink -f "$directory")" = "$directory"
	test -z "$(find "$directory" -maxdepth 1 -type l -print)"
	test "$(find "$directory" -maxdepth 1 -type f -name '*.status' | wc -l)" = "$count"
	for path in "$directory"/*.status; do
		expected=0
		if test "$directory" = "$provenance" && test "${path##*/}" = old-pin.status; then
			expected=1
		fi
		test -f "$path" && test ! -L "$path"
		test "$(cat "$path")" = "$expected"
	done
}
status_vector "$normal" 10
status_vector "$default" 8
status_vector "$full" 14
status_vector "$provenance" 15
test ! -s "$full/full-checkpatch.log"
test ! -s "$full/extended-script-inventory.txt"
grep -Fxq 'pinned local coreboot patch or result digest differs' "$provenance/old-pin.log"
grep -Fq 'WALL=1112.08 ' "$normal/regression.time"
grep -Fq 'WALL=941.42 ' "$default/regression.time"
sha256sum --quiet -c <<'PINS'
892ca714fb23556a2e1f6cf72136097a009fc51b61fc2dcbea891c3cc48a4a89  /home/sean/whole-regression-after689-recipe.i9xraY/normal.sh
938a0725c16c24225dfefdf6df43b3e42e113809de3359e1723adbda4e7ca765  /home/sean/whole-regression-after689-recipe.i9xraY/default.sh
827e515738d08dbccc6d1d7064ee595256d58fe875cf03c5604579ee30bbbebb  /home/sean/whole-regression-after689-recipe.i9xraY/hash-inputs.py
4864f919aaec9ddeeeb9946c7fcdd66105bc66932cd85bfd6228b0d320bd1226  /home/sean/whole-regression-after689-recipe.i9xraY/README.txt
d5aec20198fc2d36b1673beadcbffc355648bcfd954d7aa7feed2f2d76ec31fb  /home/sean/full-lint-after688-recipe.FIdWyr/run.sh
827e515738d08dbccc6d1d7064ee595256d58fe875cf03c5604579ee30bbbebb  /home/sean/full-lint-after688-recipe.FIdWyr/hash-inputs.py
8e11f63ab58a5a0d77783d0aa7b5538d1afffda75f4c1cb6158aa2b8198123b2  /home/sean/full-lint-after688-recipe.FIdWyr/README.txt
9a20822cd65dd6070c26703ecdd8c8d40c25eea1c61d200ca386a54444e89ef3  /home/sean/review-profile-provenance-host-recipe.h5uePF/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/review-profile-provenance-host-recipe.h5uePF/hash-inputs.py
3b07802f53b13e126cfb6918b53774b06c8514756b900df4c8a3620b2d82481a  /home/sean/review-profile-provenance-host-recipe.h5uePF/README.txt
10f6aef02d7678a1ae8d5ffab8157b5ab0a37438576a645cdf237fe130fa2aac  /home/sean/whole-normal-after689.20261004-r1/build/.config
ac668eb9c8a622e020182653fd832e6671741f5e616a01cfa4fb33b8e9b4065f  /home/sean/whole-normal-after689.20261004-r1/build/include/cdk2/config.h
d2f5db5a8d33dab72cca9587ca958e67c657b1d2ea0bec18ce69bff775bd1492  /home/sean/whole-normal-after689.20261004-r1/build/kconfig/coreboot-source.tmp
8b87082e6f19124f7f6565e8388cbd65d22fb758526dbff551fe9432ce7cbe38  /home/sean/whole-normal-after689.20261004-r1/build/kconfig/coreboot-input.identity
4edfabf04f47d30e6fc0ffdb82d80f31a3967742df57a9879c32431c093216ab  /home/sean/whole-default-after689.20261004-r1/build/.config
eb6f9c5213c63d577dda51761c00c1018a4f04736e1adc878799869c04b80f02  /home/sean/whole-default-after689.20261004-r1/build/include/cdk2/config.h
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  /home/sean/whole-default-after689.20261004-r1/build/kconfig/coreboot-source.tmp
490b0d9771939d3d0cbc21b9b34bbe272f065bd9d02cd65fea0353ba73ece2cf  /home/sean/whole-default-after689.20261004-r1/build/kconfig/coreboot-input.identity
10f6aef02d7678a1ae8d5ffab8157b5ab0a37438576a645cdf237fe130fa2aac  /home/sean/full-lint-after688-final.20261004-r1/build/.config
ac668eb9c8a622e020182653fd832e6671741f5e616a01cfa4fb33b8e9b4065f  /home/sean/full-lint-after688-final.20261004-r1/build/include/cdk2/config.h
PINS
mkdir "$destination"
printf '1\n' > "$destination/collection.status"
finish()
{
	local raw=$?
	trap - EXIT
	if test "$raw" != 0; then printf '1\n' > "$destination/collection.status"; fi
	exit "$raw"
}
trap finish EXIT
: > "$destination/ORIGINAL_FILES.sha256"
: > "$destination/FILES_MAP.tsv"
: > "$destination/EXCLUDED_TOPLEVEL.tsv"
tools=(bash cat find wc grep sha256sum mkdir stat dirname gzip cmp cp sort xargs readlink file cut)
for name in "${tools[@]}"; do
	path=$(type -P "$name")
	path=$(readlink -f "$path")
	test -f "$path" && test -x "$path"
	printf '%s\t%s\n' "$name" "$path" >> "$destination/COLLECTOR_TOOLS.tsv"
	sha256sum "$path" >> "$destination/COLLECTOR_TOOLS.sha256"
done
copy_file()
{
	local label=$1 path=$2 basename=${3:-${2##*/}} target mime
	test -f "$path" && test ! -L "$path"
	mime=$(file -b --mime-type "$path")
	case "$mime" in text/*|application/json|inode/x-empty) ;; *) echo "Non-text input: $path ($mime)" >&2; exit 1 ;; esac
	if grep -aEq '^-----BEGIN (RSA |EC |DSA |OPENSSH |ENCRYPTED )?PRIVATE KEY-----' "$path"; then
		echo "Private key input refused: $path" >&2; exit 1
	fi
	target=$destination/$label/$basename
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
		*) printf '%s\t%s\t%s\tmetadata-only-not-copied\n' "$path" "$(stat -c %s "$path")" "$(sha256sum "$path" | cut -d ' ' -f 1)" >> "$destination/EXCLUDED_TOPLEVEL.tsv" ;;
		esac
	done < <(find "$directory" -maxdepth 1 -type f -print0 | sort -z)
}
collect_root whole-normal689 "$normal"
collect_root whole-default689 "$default"
collect_root full-lint688 "$full"
collect_root provenance689 "$provenance"
for directory in "$normal" "$default"; do
	if test "$directory" = "$normal"; then label=whole-normal689-config; else label=whole-default689-config; fi
	copy_file "$label" "$directory/build/.config" resolved.config
	copy_file "$label" "$directory/build/include/cdk2/config.h"
	copy_file "$label" "$directory/build/kconfig/coreboot-source.tmp"
	copy_file "$label" "$directory/build/kconfig/coreboot-input.identity"
done
copy_file full-lint688-config "$full/build/.config" resolved.config
copy_file full-lint688-config "$full/build/include/cdk2/config.h"
for name in normal.sh default.sh hash-inputs.py README.txt; do
	copy_file whole689-recipe /home/sean/whole-regression-after689-recipe.i9xraY/$name
done
for name in run.sh hash-inputs.py README.txt; do
	copy_file full-lint688-recipe /home/sean/full-lint-after688-recipe.FIdWyr/$name
	copy_file provenance689-recipe /home/sean/review-profile-provenance-host-recipe.h5uePF/$name
done
copy_file collector "$recipe/collect.sh"
copy_file collector "$recipe/README.txt"
status_vector "$normal" 10
status_vector "$default" 8
status_vector "$full" 14
status_vector "$provenance" 15
sha256sum --quiet -c "$destination/ORIGINAL_FILES.sha256"
sha256sum --quiet -c "$destination/COLLECTOR_TOOLS.sha256"
printf '0\n' > "$destination/collection.status"
(
	cd "$destination"
	find . -type f ! -name ARCHIVE.sha256 -print0 | sort -z | xargs -0 sha256sum > ARCHIVE.sha256
	sha256sum --quiet -c ARCHIVE.sha256
)
printf 'PASS finite whole689/full688/provenance receipt preservation; no source replay, build, VM or retirement\n'
