#!/bin/bash
set -euo pipefail
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export HOME=/home/sean LANG=C LC_ALL=C
receipt=/home/sean/tcg-hash-pack32-host.l71pAn
source=/home/sean/Documents/.cdk2-worktrees/tcg-hash-pack32-after661
config=/home/sean/fresh-normal-focus-after657.CftZZg/build/include/cdk2/config.h
cd "$source"
mkdir "$receipt/tmp" "$receipt/build" "$receipt/old-source"
export TMPDIR=$receipt/tmp
tools=(bash sh git ssh-keygen cc gcc perl awk sha256sum cmp cut cat mkdir readlink mktemp rm cp chmod grep dirname sed wc env find sort xargs tr)
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
	for kind in source tools config compiler-inputs old-source; do
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
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = e737ff0bec516156ba57c7a1bdc9e5c097501226b67de109211babe7bc23932c
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
for name in libgcc.a liblto_plugin.so libasan.so libubsan.so; do
	sha256sum "$(readlink -f "$(/usr/bin/gcc "-print-file-name=$name")")" >> "$receipt/tools-before.sha256"
done
sha256sum "$config" > "$receipt/config-before.sha256"
config_state > "$receipt/config-state-before.txt"
sources=(tests/software_hash_test.c src/lib/tcg_hash/software_hash.c
    src/lib/tcg_hash/vendor/vboot/2sha1.c src/lib/tcg_hash/vendor/vboot/2sha256.c
    src/lib/tcg_hash/vendor/vboot/2sha512.c src/lib/tcg_hash/vendor/linux/lib/crypto/sm3.c)
includes=(-I/home/sean/fresh-normal-focus-after657.CftZZg/build/include -Iinclude
    -Isrc/lib/tcg_hash/vendor/vboot -Isrc/lib/tcg_hash/vendor/linux/include)
/usr/bin/gcc -std=c11 "${includes[@]}" -M "${sources[@]}" > "$receipt/compiler-inputs.d"
git show HEAD:src/lib/tcg_hash/vendor/vboot/2sha_private.h > "$receipt/old-source/2sha_private.h"
sha256sum "$receipt/old-source/2sha_private.h" > "$receipt/old-source-before.sha256"
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
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
common=(env -i PATH="$PATH" HOME=/home/sean TMPDIR="$TMPDIR" LANG=C LC_ALL=C
    HOSTCC=/usr/bin/gcc ASAN_OPTIONS="$ASAN_OPTIONS" UBSAN_OPTIONS="$UBSAN_OPTIONS")
failed=0
for mode in o0 o2 asan ubsan; do
    flags=(-std=c11 -Wall -Wextra -Werror -g -fno-pie -no-pie)
    case "$mode" in
    o0) flags+=(-O0) ;;
    o2) flags+=(-O2) ;;
    asan) flags+=(-O1 -fsanitize=address -fno-sanitize-recover=all) ;;
    ubsan) flags+=(-O1 -fsanitize=undefined -fno-sanitize-recover=all) ;;
    esac
    gate "$mode-compile" "${common[@]}" /usr/bin/gcc "${flags[@]}" "${includes[@]}" "${sources[@]}" -o "$receipt/build/$mode"
    if test "$(cat "$receipt/$mode-compile.status")" = 0; then
        gate "$mode-kat" "${common[@]}" "$receipt/build/$mode"
        test "$(cat "$receipt/$mode-kat.status")" = 0 || failed=1
    else
        failed=1
    fi
done
gate provenance "${common[@]}" sh tests/software_hash_source_test.sh "$source"
test "$(cat "$receipt/provenance.status")" = 0 || failed=1
gate shim perl util/lint/checkpatch.pl --no-tree --show-types --file src/lib/tcg_hash/vendor/vboot/2sha_private.h
test "$(cat "$receipt/shim.status")" -le 1 || failed=1
if grep -q '^WARNING:SINGLE_STATEMENT_DO_WHILE_MACRO:' "$receipt/shim.log"; then failed=1; fi
gate old-shim perl util/lint/checkpatch.pl --no-tree --show-types --file "$receipt/old-source/2sha_private.h"
opposition=0
test "$(cat "$receipt/old-shim.status")" = 1 || opposition=1
grep -q '^WARNING:SINGLE_STATEMENT_DO_WHILE_MACRO:' "$receipt/old-shim.log" || opposition=1
printf '%s\n' "$opposition" > "$receipt/old-shim-opposition.status"
test "$opposition" = 0 || failed=1
test "$failed" = 0
