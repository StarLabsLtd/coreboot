#!/bin/bash
set -euo pipefail
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
receipt=/home/sean/capsule-window-report-link-gates.fDb8NU
source=/home/sean/Documents/.cdk2-worktrees/capsule-window-report-link-after658
header=/home/sean/default-regression-after656.yHBo5L/build/include/cdk2/config.h
cd "$source"
mkdir "$receipt/tmp"
export TMPDIR=$receipt/tmp
printf '1\n' > "$receipt/aggregate.status"
finish()
{
	local execution=$? closure=0
	trap - EXIT
	set +e
	printf '%s\n' "$execution" > "$receipt/execution.status"
	sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log" 2>&1 || closure=1
	sha256sum -c "$receipt/tools-before.sha256" > "$receipt/tools-after-check.log" 2>&1 || closure=1
	git rev-parse HEAD > "$receipt/head-after.txt"
	git status --porcelain > "$receipt/status-after.txt"
	git diff --binary > "$receipt/source-after.diff"
	cmp "$receipt/head-before.txt" "$receipt/head-after.txt" || closure=1
	cmp "$receipt/status-before.txt" "$receipt/status-after.txt" || closure=1
	cmp "$receipt/frozen.diff" "$receipt/source-after.diff" || closure=1
	printf '%s\n' "$closure" > "$receipt/closure.status"
	if test "$execution" = 0 && test "$closure" = 0; then printf '0\n' > "$receipt/aggregate.status"; exit 0; fi
	exit 1
}
trap finish EXIT
git rev-parse HEAD > "$receipt/head-before.txt"
test "$(cat "$receipt/head-before.txt")" = b37a977ec5f81b6fa0bce527ab74acd743040a24
git verify-commit HEAD > "$receipt/signature.log" 2>&1
git status --porcelain > "$receipt/status-before.txt"
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = 379376fc32b478d1607c9ba479c4deb3595587c921adc741d9b783a24530204c
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
sha256sum "$header" >> "$receipt/source-before.sha256"
sha256sum "$0" /usr/bin/time > "$receipt/tools-before.sha256"
for tool in bash sh gcc git awk sha256sum cmp cut cat mkdir env readlink mktemp rm cp grep; do
	sha256sum "$(readlink -f "$(type -P "$tool")")" >> "$receipt/tools-before.sha256"
done
for tool in cc1 as ld collect2 lto-wrapper; do
	selected=$(gcc "-print-prog-name=$tool")
	case "$selected" in /*) ;; *) selected=$(type -P "$selected") ;; esac
	sha256sum "$(readlink -f "$selected")" >> "$receipt/tools-before.sha256"
done
for support in libgcc.a libgcc_s.so libasan.so libubsan.so liblto_plugin.so; do
	sha256sum "$(readlink -f "$(gcc "-print-file-name=$support")")" >> "$receipt/tools-before.sha256"
done
status=0
/usr/bin/time -o "$receipt/window.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
	env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C HOSTCC=gcc \
	sh tests/capsule_ram_window_core_test.sh "$header" > "$receipt/window.log" 2>&1 || status=$?
printf '%s\n' "$status" > "$receipt/window.status"
test "$status" = 0
