#!/bin/bash
set -euo pipefail
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C
receipt=/home/sean/tpm-fifo-command-ready-host-gates.XFCZpH
source=/home/sean/Documents/.coreboot-worktrees/tpm-fifo-command-ready-after7ee
vendor=$source/3rdparty/vboot
cd "$source"
test ! -e "$receipt/tmp"
mkdir "$receipt/tmp"
export TMPDIR=$receipt/tmp
printf '1\n' > "$receipt/aggregate.status"
tools=(bash sh git ssh-keygen gcc awk sha256sum cmp cut cat mkdir readlink mktemp rm grep dirname basename cp sed)
paths()
{
	local name
	for name in "${tools[@]}"; do
		printf '%s\t%s\n' "$name" "$(readlink -f "$(type -P "$name")")"
	done
}
finish()
{
	local execution=$? closure=0
	trap - EXIT
	set +e
	printf '%s\n' "$execution" > "$receipt/execution.status"
	for kind in source tools old-source; do
		sha256sum -c "$receipt/$kind-before.sha256" > "$receipt/$kind-after-check.log" 2>&1 || closure=1
	done
	git rev-parse HEAD > "$receipt/head-after.txt"
	git status --porcelain > "$receipt/status-after.txt"
	git diff --binary > "$receipt/source-after.diff"
	cmp "$receipt/head-before.txt" "$receipt/head-after.txt" || closure=1
	cmp "$receipt/status-before.txt" "$receipt/status-after.txt" || closure=1
	cmp "$receipt/frozen.diff" "$receipt/source-after.diff" || closure=1
	git -C "$vendor" rev-parse HEAD > "$receipt/vendor-head-after.txt"
	git -C "$vendor" status --porcelain > "$receipt/vendor-status-after.txt"
	cmp "$receipt/vendor-head-before.txt" "$receipt/vendor-head-after.txt" || closure=1
	cmp "$receipt/vendor-status-before.txt" "$receipt/vendor-status-after.txt" || closure=1
	paths > "$receipt/tool-paths-after.tsv"
	cmp "$receipt/tool-paths-before.tsv" "$receipt/tool-paths-after.tsv" || closure=1
	printf '%s\n' "$closure" > "$receipt/closure.status"
	if test "$execution" = 0 && test "$closure" = 0; then
		printf '0\n' > "$receipt/aggregate.status"
		exit 0
	fi
	exit 1
}
trap finish EXIT
git rev-parse HEAD > "$receipt/head-before.txt"
test "$(cat "$receipt/head-before.txt")" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
git verify-commit HEAD > "$receipt/signature.log" 2>&1
git status --porcelain > "$receipt/status-before.txt"
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = 59153b586770838f927622bda747e0d9c357eab9dbca9e64c5dfcb753436530d
git -C "$vendor" rev-parse HEAD > "$receipt/vendor-head-before.txt"
test "$(cat "$receipt/vendor-head-before.txt")" = 5c360ef458b0a013d8a6d47724bb0fffb5accbcf
git -C "$vendor" status --porcelain > "$receipt/vendor-status-before.txt"
test ! -s "$receipt/vendor-status-before.txt"
for tree in "$source" "$vendor"; do
	git -C "$tree" ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
		while IFS= read -r path; do sha256sum "$tree/$path"; done
done > "$receipt/source-before.sha256"
paths > "$receipt/tool-paths-before.tsv"
sha256sum "$0" "$receipt/cc.sh" "$receipt/README.txt" /usr/bin/time > "$receipt/tools-before.sha256"
while IFS=$'\t' read -r name path; do sha256sum "$path"; done < "$receipt/tool-paths-before.tsv" >> "$receipt/tools-before.sha256"
for name in cc1 as ld collect2 lto-wrapper; do
	selected=$(/usr/bin/gcc "-print-prog-name=$name")
	case "$selected" in /*) ;; *) selected=$(type -P "$selected") ;; esac
	sha256sum "$(readlink -f "$selected")" >> "$receipt/tools-before.sha256"
done
for name in libgcc.a liblto_plugin.so libasan.so libubsan.so; do
	sha256sum "$(readlink -f "$(/usr/bin/gcc "-print-file-name=$name")")" >> "$receipt/tools-before.sha256"
done
git show HEAD:src/drivers/pc80/tpm/tis.c > "$receipt/old-tis.c"
sha256sum "$receipt/old-tis.c" > "$receipt/old-source-before.sha256"
gate()
{
	local label=$1 status=0
	shift
	printf '%q ' "$@" > "$receipt/$label.command"
	printf '\n' >> "$receipt/$label.command"
	/usr/bin/time -o "$receipt/$label.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
		"$@" > "$receipt/$label.log" 2>&1 || status=$?
	printf '%s\n' "$status" > "$receipt/$label.status"
}
common=(env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C CC="$receipt/cc.sh")
gate syntax sh -n tests/lib/tpm2_fifo_pre_os_lifecycle_test.sh
gate models "${common[@]}" sh tests/lib/tpm2_fifo_pre_os_lifecycle_test.sh
gate old-predicate "${common[@]}" CAUSAL_OLD_PREDICATE=1 sh -x tests/lib/tpm2_fifo_pre_os_lifecycle_test.sh
test "$(cat "$receipt/syntax.status")" = 0
test "$(cat "$receipt/models.status")" = 0
opposition=0
test "$(cat "$receipt/old-predicate.status")" = 132 || opposition=1
grep -Eq '^\+ .*tis-o0-tis ready-only$' "$receipt/old-predicate.log" || opposition=1
printf '%s\n' "$opposition" > "$receipt/old-predicate-opposition.status"
test "$opposition" = 0
