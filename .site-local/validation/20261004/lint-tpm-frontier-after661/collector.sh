#!/bin/bash
set -euo pipefail
recipe=/home/sean/lint-tpm-frontier-collector.WkfyW8
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/lint-tpm-frontier-after661
lint=/home/sean/full-lint-after661-final.20261004-r1
built=/home/sean/tpm-acceptance-relative-build-after661
guests=/home/sean/tpm-acceptance-relative-guests-after661
test ! -e "$destination"
test "$(cat "$lint/aggregate.status")" = 1
test "$(cat "$lint/closure.status")" = 0
test "$(cat "$built/aggregate.status")" = 0
test "$(cat "$guests/aggregate.status")" = 1
for lane in tpm-linux tpm-absent-dma-safe; do
	jq -e '.qemu_status == 124' "$guests/$lane/manifest.json" >/dev/null
	test "$(cat "$guests/$lane-oracle.status")" = 1
	test "$(cat "$guests/$lane-media.status")" = 0
done
sha256sum --quiet -c <<'PINS'
cb953ed85bd3bf32d7f53a92d7a2ec12ada2143706a91943b9dbf18497df341a  /home/sean/full-lint-after661-recipe.l4JKYZ/run.sh
827e515738d08dbccc6d1d7064ee595256d58fe875cf03c5604579ee30bbbebb  /home/sean/full-lint-after661-recipe.l4JKYZ/hash-inputs.py
8de2a81f00dcfb45e6b7441d59484a4818467cbf1be4980a1a4c4b2ea06d28bc  /home/sean/full-lint-after661-recipe.l4JKYZ/README.txt
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
for label in lint build guests; do
	case "$label" in lint) directory=$lint ;; build) directory=$built ;; guests) directory=$guests ;; esac
	while IFS= read -r -d '' path; do
		case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.diff)
			copy_file "$label" "$path" ;;
		esac
	done < <(find "$directory" -maxdepth 1 -type f -print0 | sort -z)
done
for lane in tpm-linux tpm-absent-dma-safe; do
	for name in manifest.json serial.log debugcon.log qemu-command.txt qemu.log qemu.status coreboot-config.txt cdk2-config.txt; do
		copy_file "$lane" "$guests/$lane/$name"
	done
done
copy_file tpm-linux "$guests/tpm-linux/swtpm.log"
test ! -e "$guests/tpm-absent-dma-safe/swtpm.log"
for name in run.sh hash-inputs.py README.txt; do
	copy_file lint-recipe "/home/sean/full-lint-after661-recipe.l4JKYZ/$name"
done
for name in build.sh run-two.sh receipt.py README.md; do
	copy_file tpm-recipe "/home/sean/tpm-acceptance-recipes-relative-vboot.JG80tX/$name"
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
