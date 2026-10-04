#!/bin/bash
set -euo pipefail
receipt=/home/sean/dxe-header-coverage-gates.BguG71
source=/home/sean/Documents/.cdk2-worktrees/dxe-header-coverage-after655
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
mkdir "$receipt/tmp"
export TMPDIR=$receipt/tmp LANG=C LC_ALL=C
cd "$source"
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = 156615f84452c12491dbc64a85fcbd442475c78058fb910aac78db70e3862976
git rev-parse HEAD > "$receipt/head-before.txt"
git status --porcelain > "$receipt/status-before.txt"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
sha256sum "$0" /usr/bin/time > "$receipt/tools-before.sha256"
for tool in bash sh gcc make git awk sed rg grep sha256sum cmp cut mktemp; do
	sha256sum "$(readlink -f "$(type -P "$tool")")" >> "$receipt/tools-before.sha256"
done
for tool in cc1 as ld collect2 lto-wrapper; do
	selected=$(gcc "-print-prog-name=$tool")
	case "$selected" in /*) ;; *) selected=$(type -P "$selected") ;; esac
	sha256sum "$(readlink -f "$selected")" >> "$receipt/tools-before.sha256"
done
for archive in libgcc.a libasan.so libubsan.so libtsan.so; do
	sha256sum "$(readlink -f "$(gcc "-print-file-name=$archive")")" >> "$receipt/tools-before.sha256"
done
sha256sum "$(readlink -f "$(gcc -m32 -print-file-name=libgcc.a)")" >> "$receipt/tools-before.sha256"
result=0 closure=0
sh -n tests/authvar_presence_lifecycle_close_boundary_test.sh
sed -n "/^awk '/,/^' .*src\/boot\/Makefile/p" \
	tests/authvar_presence_lifecycle_close_boundary_test.sh | sed '1d;$d' > "$receipt/dependency.awk"
test -s "$receipt/dependency.awk"
awk '/dxe_core[.]h/ && !removed { print; getline; removed = 1; next } { print }' \
	src/boot/Makefile > "$receipt/missing-pair.mk"
awk '!/dxe_core[.]h/' src/boot/Makefile > "$receipt/zero-header.mk"
for fixture in real missing-pair zero-header; do
	case "$fixture" in real) input=src/boot/Makefile; expected=0 ;; *) input=$receipt/$fixture.mk; expected=1 ;; esac
	status=0
	awk -f "$receipt/dependency.awk" "$input" > "$receipt/$fixture.log" 2>&1 || status=$?
	printf '%s\n' "$status" > "$receipt/$fixture.status"
	test "$status" = "$expected" || result=1
done
boundary_result=0
/usr/bin/time -o "$receipt/boundary.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
	env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C HOSTCC=gcc \
	sh tests/authvar_presence_lifecycle_close_boundary_test.sh > "$receipt/boundary.log" 2>&1 || boundary_result=$?
printf '%s\n' "$boundary_result" > "$receipt/boundary.status"
test "$boundary_result" = 0 || result=1
sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log" 2>&1 || closure=1
sha256sum -c "$receipt/tools-before.sha256" > "$receipt/tools-after-check.log" 2>&1 || closure=1
git rev-parse HEAD > "$receipt/head-after.txt"
git status --porcelain > "$receipt/status-after.txt"
git diff --binary > "$receipt/source-after.diff"
cmp "$receipt/frozen.diff" "$receipt/source-after.diff" || closure=1
cmp "$receipt/head-before.txt" "$receipt/head-after.txt" || closure=1
cmp "$receipt/status-before.txt" "$receipt/status-after.txt" || closure=1
printf '%s\n' "$closure" > "$receipt/closure.status"
test "$result" = 0 && test "$closure" = 0 && aggregate=0 || aggregate=1
printf '%s\n' "$aggregate" > "$receipt/aggregate.status"
exit "$aggregate"
