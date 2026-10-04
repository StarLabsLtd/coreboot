#!/bin/bash
set -euo pipefail
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/default656-loader-retry
recipe=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test ! -e "$destination"
sha256sum --quiet -c <<'PINS'
3892728e74698d6f54bfc348535009cfe62e89039d1503cb5d205b57564e395a  /home/sean/default-regression-after656.yHBo5L/run.sh
914dead27c719b45a518b591ad6c6b63520255225aa81d3e61f753a9050bfd60  /home/sean/loader-matrix-path-retry.27wkn8/run.sh
PINS
test "$(cat /home/sean/default-regression-after656.yHBo5L/execution.status)" = 2
test "$(cat /home/sean/default-regression-after656.yHBo5L/closure.status)" = 0
test "$(cat /home/sean/default-regression-after656.yHBo5L/aggregate.status)" = 1
for status in secure-matrix efi-matrix execution closure aggregate; do
	test "$(cat "/home/sean/loader-matrix-path-retry.27wkn8/$status.status")" = 0
done
mkdir -p "$destination"
collect()
{
	local label=$1 source=$2 path target
	while IFS= read -r -d '' path; do
		case "$path" in
			*.log|*.txt|*.time|*.status|*.sha256|*.sh|*.command)
				test ! -L "$path"
				target=$destination/$label/${path##*/}
				mkdir -p "$(dirname "$target")"
				sha256sum "$path" >> "$destination/ORIGINAL_FILES.sha256"
				if test "$(stat -c %s "$path")" -gt 1048576; then
					gzip -n -c "$path" > "$target.gz"
					gzip -dc "$target.gz" | cmp "$path" -
				else
					cp -p "$path" "$target"
					cmp "$path" "$target"
				fi ;;
		esac
	done < <(find "$source" -maxdepth 1 -type f -print0 | sort -z)
}
collect failed-default656 /home/sean/default-regression-after656.yHBo5L
collect loader-host-retry /home/sean/loader-matrix-path-retry.27wkn8
cp -p "$recipe/collect.sh" "$destination/collector.sh"
cp -p "$recipe/README.md" "$destination/README.md"
cmp "$recipe/collect.sh" "$destination/collector.sh"
cmp "$recipe/README.md" "$destination/README.md"
(
	cd "$destination"
	find . -type f ! -name ARCHIVE.sha256 -print0 | sort -z | xargs -0 sha256sum > ARCHIVE.sha256
	sha256sum --quiet -c ARCHIVE.sha256
)
