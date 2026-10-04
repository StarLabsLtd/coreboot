#!/bin/bash
set -euo pipefail
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/tpm-first-producer-failure-after661
recipe=/home/sean/tpm-first-failure-collector.XarFDR
failed=/home/sean/tpm-acceptance-build-after661
test ! -e "$destination"
test "$(cat "$failed/producer-build.status")" = 2
test "$(cat "$failed/aggregate.status")" = 2
test ! -e "$failed/payload-build.status"
test ! -s "$failed/source-tools-check.log"
sha256sum --quiet -c <<'PINS'
a19a526858569fe7be13b271e45164c64b174acb5e07f282fc3cc771b658f39c  /home/sean/tpm-acceptance-recipes.5YqxUa/build.sh
fd59c06a75a62cd55f163cd9bd4d9a4e0d80fcaf04e2cd83b67e72e10a2c70a9  /home/sean/tpm-acceptance-recipes.5YqxUa/run-two.sh
35e3eb7d9b7d82b60da90c0548b3196fbb3f42e75249e7fe87c58843b0c4eaa7  /home/sean/tpm-acceptance-recipes.5YqxUa/receipt.py
092575d344657d4484a8a5d976c451d429d19ed17ac80187351fc04f025ab20f  /home/sean/tpm-acceptance-recipes.5YqxUa/README.md
6688f57fe5026cab787163b3ad8eb5733eaef5783cee3c56fd67a204cd170a50  /home/sean/tpm-acceptance-recipes-relative-vboot.JG80tX/build.sh
fd59c06a75a62cd55f163cd9bd4d9a4e0d80fcaf04e2cd83b67e72e10a2c70a9  /home/sean/tpm-acceptance-recipes-relative-vboot.JG80tX/run-two.sh
35e3eb7d9b7d82b60da90c0548b3196fbb3f42e75249e7fe87c58843b0c4eaa7  /home/sean/tpm-acceptance-recipes-relative-vboot.JG80tX/receipt.py
092575d344657d4484a8a5d976c451d429d19ed17ac80187351fc04f025ab20f  /home/sean/tpm-acceptance-recipes-relative-vboot.JG80tX/README.md
PINS
mkdir -p "$destination"
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
	else
		cp -p "$path" "$target"
		cmp "$path" "$target"
	fi
}
while IFS= read -r -d '' path; do
	case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config)
		copy_file failed-producer "$path" ;;
	esac
done < <(find "$failed" -maxdepth 1 -type f -print0 | sort -z)
for label in original-draft relative-draft; do
	case "$label" in
	original-draft) directory=/home/sean/tpm-acceptance-recipes.5YqxUa ;;
	relative-draft) directory=/home/sean/tpm-acceptance-recipes-relative-vboot.JG80tX ;;
	esac
	for name in build.sh run-two.sh receipt.py README.md; do
		copy_file "$label" "$directory/$name"
	done
done
cp -p "$recipe/collect.sh" "$destination/collector.sh"
cp -p "$recipe/README.md" "$destination/README.md"
cmp "$recipe/collect.sh" "$destination/collector.sh"
cmp "$recipe/README.md" "$destination/README.md"
(
	cd "$destination"
	find . -type f ! -name ARCHIVE.sha256 -print0 | sort -z | xargs -0 sha256sum > ARCHIVE.sha256
	sha256sum --quiet -c ARCHIVE.sha256
)
