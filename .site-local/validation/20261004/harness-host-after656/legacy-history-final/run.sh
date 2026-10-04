#!/bin/bash
set -euo pipefail
receipt=/home/sean/legacy-lvgl-history-gates-retry.bKSyEC
source=/home/sean/Documents/.cdk2-worktrees/legacy-lvgl-history-after654
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=$receipt/tmp LANG=C LC_ALL=C
cd "$source"
trap 'printf "%s\n" "$?" > "$receipt/aggregate.status"' EXIT
mkdir "$TMPDIR"
git rev-parse HEAD > "$receipt/head-before.txt"
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = 473c637f9e8271c7b1574a1025800cd4a95e20032f8daa442a786e15b6198efb
git status --porcelain > "$receipt/status-before.txt"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
sha256sum "$0" > "$receipt/tools-before.sha256"
for tool in sh bash git awk sed grep make sha256sum cmp cut mktemp time \
	xargs find readlink rm mkdir sort tr head tail env; do
	sha256sum "$(readlink -f "$(type -P "$tool")")" >> "$receipt/tools-before.sha256"
done
gate()
{
	local name=$1 result=0
	shift
	/usr/bin/time -o "$receipt/$name.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
		"$@" > "$receipt/$name.log" 2>&1 || result=$?
	printf '%s\n' "$result" > "$receipt/$name.status"
	return "$result"
}
gate syntax sh -n tests/legacy_boundary_inventory_test.sh
gate historical sh tests/legacy_boundary_inventory_test.sh
gate source-boundary sh tests/lvgl_source_boundary_test.sh
git diff --check > "$receipt/diff-check.log"
sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log"
sha256sum -c "$receipt/tools-before.sha256" > "$receipt/tools-after-check.log"
git rev-parse HEAD > "$receipt/head-after.txt"
git status --porcelain > "$receipt/status-after.txt"
git diff --binary > "$receipt/source-after.diff"
cmp "$receipt/frozen.diff" "$receipt/source-after.diff"
cmp "$receipt/head-before.txt" "$receipt/head-after.txt"
cmp "$receipt/status-before.txt" "$receipt/status-after.txt"
printf '0\n' > "$receipt/closure.status"
