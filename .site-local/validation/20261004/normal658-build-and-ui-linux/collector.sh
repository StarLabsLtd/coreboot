#!/bin/bash
set -euo pipefail
recipe=/home/sean/normal658-evidence-collector.hNqsbf
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/normal658-build-and-ui-linux
built=/home/sean/fresh-normal-focus-after657.CftZZg
guests=/home/sean/fresh-normal-focus-guest-after657.M2fS2l
test ! -e "$destination"
test "$(cat "$built/aggregate.status")" = 0
test "$(cat "$built/build/outer.status")" = 0
for lane in ui-cancel linux-reset; do
	test "$(cat "$guests/$lane/outer.status")" = 0
done
sha256sum --quiet -c <<'PINS'
b6906bb5c5ad75c430ae9d0ee97b483e2b2c8bb01a841f87f5b90e517ae755be  /home/sean/fresh-normal-focus-after657.CftZZg/build.sh
13a5b7cc433bcf84ef760a34c7a7619b331266cfe4744db00d51102b47c68401  /home/sean/fresh-normal-focus-guest-after657.M2fS2l/guest-common.sh
63264ef45791c5d6880b8721fefc53927a0d69636f996056342cc507bd1bbce0  /home/sean/fresh-normal-focus-guest-after657.M2fS2l/run-ui-cancel.sh
4da74dffad3f08dbe17a5da7b80af96ba584ce2c426aeca149bcf5a208033fd2  /home/sean/fresh-normal-focus-guest-after657.M2fS2l/run-linux-reset.sh
5c045bbe1f4371475f521bb3e838fd2c0da91c16a74623dc863f045c8d9354fa  /home/sean/ready658-ui-independent-visual.ZQ2lIj/REVIEW.txt
7735b90241a52748c8da3099db9736a70e9301697fd6a6f4b9df7bb7d2f49f2f  /home/sean/ready658-linux-independent-visual.15sZaj/REVIEW.txt
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
flat_receipts()
{
	local label=$1 directory=$2 path
	while IFS= read -r -d '' path; do
		case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.ppm|*.png|*/cbmem-console.bin|*/cbmem-table-0.bin|*/cbmem-table-1.bin)
			copy_file "$label" "$path" ;;
		esac
	done < <(find "$directory" -maxdepth 1 -type f -print0 | sort -z)
}
flat_receipts build "$built"
flat_receipts build-detail "$built/build"
for lane in ui-cancel linux-reset; do
	flat_receipts "$lane" "$guests/$lane"
	flat_receipts "$lane-lifecycle" "$guests/$lane/lifecycle"
	case "$lane" in ui-cancel) vm=$guests/$lane/lifecycle/ui-cancel ;; linux-reset) vm=$guests/$lane/lifecycle/same-vm ;; esac
	flat_receipts "$lane-vm" "$vm"
done
flat_receipts ui-visual /home/sean/ready658-ui-independent-visual.ZQ2lIj
flat_receipts linux-visual /home/sean/ready658-linux-independent-visual.15sZaj
copy_file build-recipe "$built/build.sh"
for name in guest-common.sh run-ui-cancel.sh run-linux-reset.sh; do
	copy_file guest-recipe "$guests/$name"
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
