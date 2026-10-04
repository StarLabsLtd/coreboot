#!/bin/bash
set -euo pipefail
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/default661-tpm-diagnostic
recipe=/home/sean/default661-diagnostic-evidence-collector.1VcBLb
default=/home/sean/default-regression-after661.E4XtC9
stable=/home/sean/stable-lint-style-host-gates.6u6nEI
built=/home/sean/tpm-fifo-diagnostic-build-after661
guest=/home/sean/tpm-fifo-diagnostic-guest-after661
diagnostic=/home/sean/tpm-fifo-diagnostic-recipes.z1LD5d
test ! -e "$destination"
for directory in "$default" "$stable" "$built"; do
	for status in "$directory"/*.status; do test "$(cat "$status")" = 0; done
done
test "$(cat "$guest/aggregate.status")" = 1
test "$(cat "$guest/tpm-linux-run.status")" = 0
test "$(cat "$guest/tpm-linux-media.status")" = 0
test "$(cat "$guest/tpm-linux-oracle.status")" = 1
test "$(cat "$guest/tpm-linux-freezer.status")" = 1
jq -e '.name == "tpm-linux" and .qemu_status == 124' "$guest/tpm-linux/manifest.json" > /dev/null
grep -Fq 'TPM2 FIFO diagnostic: invalid STS=40' "$guest/tpm-linux/serial.log"
cmp "$built/source-tools-before.json" "$built/source-tools-after.json"
cmp "$guest/source-tools-before.json" "$guest/source-tools-after.json"
cmp "$guest/inputs-before.json" "$guest/inputs-after.json"
cmp "$guest/native-inputs-before.json" "$guest/native-inputs-after.json"
cmp "$guest/selected-tools-before.json" "$guest/selected-tools-after.json"
sha256sum --quiet -c <<'PINS'
305719e677faf28a5e13febd0c56f89f880a5cf4889b61fa920f9b2bd07f5f54  /home/sean/default-regression-after661.E4XtC9/run.sh
cb3aa4acb6227f46839bfc307fb0603154644bac8989ace2559b1a42a43390bf  /home/sean/default-regression-after661.E4XtC9/PREPARATION.txt
d2900b93e4bf8d36641ac4c41ab53823359bffe4220deec4fbd92beb9cc520a3  /home/sean/stable-lint-style-host-gates.6u6nEI/run.sh
1fc13b4aeb78bb147f8eae44505caa4c8a68816ed30c85f0fe95fbe405a13aac  /home/sean/tpm-fifo-diagnostic-recipes.z1LD5d/build.sh
11e5b73328a1f1ca82b4d92d3914bce39d2d87fe67bccc8160325b489156a669  /home/sean/tpm-fifo-diagnostic-recipes.z1LD5d/run-two.sh
35e3eb7d9b7d82b60da90c0548b3196fbb3f42e75249e7fe87c58843b0c4eaa7  /home/sean/tpm-fifo-diagnostic-recipes.z1LD5d/receipt.py
0198d9f1c7be3a5854f55289f5b36511f98c7a82d6d0429d1534996c47c2d6a8  /home/sean/tpm-fifo-diagnostic-recipes.z1LD5d/README.md
PINS
mkdir -p "$destination"
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
for label in default661 stable-style diagnostic-build diagnostic-guest; do
	case "$label" in
	default661) directory=$default ;;
	stable-style) directory=$stable ;;
	diagnostic-build) directory=$built ;;
	diagnostic-guest) directory=$guest ;;
	esac
	while IFS= read -r -d '' path; do
		case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff)
			copy_file "$label" "$path" ;;
		esac
	done < <(find "$directory" -maxdepth 1 -type f -print0 | sort -z)
done
copy_file default661 "$default/run.sh"
copy_file stable-style "$stable/run.sh"
for name in manifest.json serial.log debugcon.log qemu.log qemu.status \
    qemu-command.txt swtpm.log cdk2-config.txt coreboot-config.txt; do
	copy_file diagnostic-present "$guest/tpm-linux/$name"
done
for name in build.sh run-two.sh receipt.py README.md; do
	copy_file diagnostic-recipe "$diagnostic/$name"
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
