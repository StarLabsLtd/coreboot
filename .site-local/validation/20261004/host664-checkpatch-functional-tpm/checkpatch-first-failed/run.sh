#!/bin/bash
set -euo pipefail
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C
receipt=/home/sean/checkpatch-constant-host-gates.WbHfdg
source=/home/sean/Documents/.cdk2-worktrees/checkpatch-constant-whitespace-after661
cd "$source"
mkdir "$receipt/tmp" "$receipt/old-source"
export TMPDIR=$receipt/tmp
tools=(bash sh git ssh-keygen gcc perl awk sha256sum cmp cut cat mkdir readlink mktemp rm cp chmod grep dirname sed wc env find sort xargs)
paths()
{
	local tool
	for tool in "${tools[@]}"; do
		printf '%s\t%s\n' "$tool" "$(readlink -f "$(type -P "$tool")")"
	done
}
printf '1\n' > "$receipt/aggregate.status"
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
test "$(cat "$receipt/head-before.txt")" = 6faf053c28d7b33de30daeaeae8408340685d006
git verify-commit HEAD > "$receipt/signature.log" 2>&1
git status --porcelain > "$receipt/status-before.txt"
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = e3a08826441d1793c9128cf0f3b26e1877efb4ceda8e2b7e99dc984cf245d932
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
paths > "$receipt/tool-paths-before.tsv"
sha256sum "$0" "$receipt/README.txt" /usr/bin/time /bin/false > "$receipt/tools-before.sha256"
while IFS=$'\t' read -r name path; do sha256sum "$path"; done < "$receipt/tool-paths-before.tsv" >> "$receipt/tools-before.sha256"
for name in cc1 as ld collect2 lto-wrapper; do
	selected=$(/usr/bin/gcc "-print-prog-name=$name")
	case "$selected" in /*) ;; *) selected=$(type -P "$selected") ;; esac
	sha256sum "$(readlink -f "$selected")" >> "$receipt/tools-before.sha256"
done
for name in libgcc.a liblto_plugin.so; do
	sha256sum "$(readlink -f "$(/usr/bin/gcc "-print-file-name=$name")")" >> "$receipt/tools-before.sha256"
done
mkdir -p "$receipt/old-source/util/lint" "$receipt/old-source/tests"
git show HEAD:util/lint/checkpatch.pl > "$receipt/old-source/util/lint/checkpatch.pl"
cp tests/checkpatch_generic_test.sh tests/checkpatch_generic_test.c "$receipt/old-source/tests/"
find "$receipt/old-source" -type f -print0 | sort -z | xargs -0 sha256sum > "$receipt/old-source-before.sha256"
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
common=(env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C HOSTCC=/usr/bin/gcc)
gate syntax sh -n tests/checkpatch_generic_test.sh
gate perl-syntax perl -c util/lint/checkpatch.pl
gate generic "${common[@]}" sh tests/checkpatch_generic_test.sh
gate driver "${common[@]}" sh tests/checkpatch_lint_driver_test.sh
gate bds perl util/lint/checkpatch.pl --no-tree --show-types --file src/modules/bds/entry.c
gate old-source "${common[@]}" sh "$receipt/old-source/tests/checkpatch_generic_test.sh"
for label in syntax perl-syntax generic driver; do
	test "$(cat "$receipt/$label.status")" = 0
done
# The actual full file may retain unrelated diagnostics. Preserve its raw status
# and require only the reviewed constant-comparison false positive to disappear.
test "$(cat "$receipt/bds.status")" -le 1
! grep -q '^WARNING:CONSTANT_COMPARISON:' "$receipt/bds.log"
opposition=0
test "$(cat "$receipt/old-source.status")" = 1 || opposition=1
grep -Fq 'constant-right parser rejected a macro comparison' "$receipt/old-source.log" || opposition=1
printf '%s\n' "$opposition" > "$receipt/old-source-opposition.status"
test "$opposition" = 0
