#!/bin/bash
set -euo pipefail
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export HOME=/home/sean LANG=C LC_ALL=C
receipt=/home/sean/checkpatch-boundaries-host.mXHLoF
source=/home/sean/Documents/.cdk2-worktrees/checkpatch-source-boundaries-after661
config=/home/sean/fresh-normal-focus-after657.CftZZg/build/include/cdk2/config.h
cd "$source"
mkdir "$receipt/tmp"
export TMPDIR=$receipt/tmp
tools=(bash sh git ssh-keygen cc gcc perl awk sha256sum cmp cut cat mkdir readlink mktemp rm cp chmod grep dirname sed wc env find sort xargs tr ln)
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
	for kind in source tools config compiler-inputs; do
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
	config_state > "$receipt/config-state-after.txt"
	cmp "$receipt/config-state-before.txt" "$receipt/config-state-after.txt" || closure=1
	printf '%s\n' "$closure" > "$receipt/closure.status"
	if test "$execution" = 0 && test "$closure" = 0; then
		printf '0\n' > "$receipt/aggregate.status"
		exit 0
	fi
	exit 1
}
config_state()
{
	local path
	for path in "$source/.checkpatch.conf" /home/sean/.checkpatch.conf "$source/.scripts/.checkpatch.conf"; do
		if test -e "$path" || test -L "$path"; then
			test -f "$path" && test ! -L "$path"
			sha256sum "$path"
		else
			printf 'absent %s\n' "$path"
		fi
	done
}
git rev-parse HEAD > "$receipt/head-before.txt"
test "$(cat "$receipt/head-before.txt")" = 6faf053c28d7b33de30daeaeae8408340685d006
git verify-commit HEAD > "$receipt/signature.log" 2>&1
git status --porcelain > "$receipt/status-before.txt"
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = 82c68f4c39253d91f7c2705736c47668ed809d580eb26a05d011f54a9ef9961f
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
sha256sum "$config" > "$receipt/config-before.sha256"
config_state > "$receipt/config-state-before.txt"
/usr/bin/gcc -std=c11 -M tests/checkpatch_uefi_information_test.c \
	util/lint/cdk2-checkpatch-filter.c tests/checkpatch_generic_test.c > "$receipt/compiler-inputs.d"
perl -MText::ParseWords=shellwords -0777 -ne '
    s/\\\n/ /g;
    for (split /\n/) { s/^[^:]+: // or next; s/\$\$/\$/g;
        print join("\n", shellwords($_)), "\n"; }
' "$receipt/compiler-inputs.d" | sort -u > "$receipt/compiler-inputs.txt"
test -s "$receipt/compiler-inputs.txt"
xargs -d '\n' sha256sum < "$receipt/compiler-inputs.txt" > "$receipt/compiler-inputs-before.sha256"
trap finish EXIT
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
common=(env -i PATH="$PATH" HOME=/home/sean TMPDIR="$TMPDIR" LANG=C LC_ALL=C HOSTCC=/usr/bin/gcc)
gate syntax-filter sh -n tests/checkpatch_filter_test.sh
gate syntax-ownership sh -n tests/checkpatch_source_ownership_test.sh
gate syntax-lint sh -n util/lint/lint-007-checkpatch
gate filter "${common[@]}" sh tests/checkpatch_filter_test.sh
gate driver "${common[@]}" sh tests/checkpatch_lint_driver_test.sh
gate imports "${common[@]}" sh util/lint/lint-007-checkpatch 'src/lib/tcg_hash/vendor/vboot/2sha1.c src/lib/tcg_hash/vendor/vboot/2sha256.c src/lib/tcg_hash/vendor/vboot/2sha512.c'
gate fat "${common[@]}" util/lint/cdk2-checkpatch --no-tree --show-types --file src/modules/fat/protocol.c
for label in syntax-filter syntax-ownership syntax-lint filter driver imports; do
	test "$(cat "$receipt/$label.status")" = 0
done
test ! -s "$receipt/imports.log"
# Preserve unrelated owned-file diagnostics; only the three reviewed wire-tail
# records must disappear. The full hostile fixture proves nearby refusals.
test "$(cat "$receipt/fat.status")" -le 1
! grep -q '^ERROR:FLEXIBLE_ARRAY:' "$receipt/fat.log"
