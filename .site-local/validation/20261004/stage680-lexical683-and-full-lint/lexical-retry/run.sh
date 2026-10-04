#!/bin/bash
set -euo pipefail
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
receipt=/home/sean/lint-lexical-followup-host-retry.JmMduj
source=/home/sean/Documents/.cdk2-worktrees/lint-lexical-followup-after677
tools=(bash sh git make env awk sha256sum cmp cut cat mkdir readlink mktemp rm cp chmod grep dirname find sed sort xargs perl wc)
cd "$source"
mkdir "$receipt/tmp" "$receipt/old-source" "$receipt/lexer-fixture"
export TMPDIR=$receipt/tmp
printf '1\n' > "$receipt/aggregate.status"
paths()
{
	local tool
	for tool in "${tools[@]}"; do
		printf '%s\t%s\n' "$tool" "$(readlink -f "$(type -P "$tool")")"
	done
}
finish()
{
	local execution=$? closure=0
	trap - EXIT
	set +e
	printf '%s\n' "$execution" > "$receipt/execution.status"
	for name in source tools fixtures; do
		sha256sum -c "$receipt/$name-before.sha256" > "$receipt/$name-after-check.log" 2>&1 || closure=1
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
git rev-parse HEAD > "$receipt/head-before.txt"
test "$(cat "$receipt/head-before.txt")" = dda20d8b40bf15c3a11a4aa8856c4ac88ec419be
git verify-commit HEAD > "$receipt/signature.log" 2>&1
git status --porcelain > "$receipt/status-before.txt"
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = 6244a0dd55f3661217877ecf056b1d057a217897d5e26976d1b92b7c9eb7ce48
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
sha256sum "$0" "$receipt/README.txt" /usr/bin/time > "$receipt/tools-before.sha256"
paths > "$receipt/tool-paths-before.tsv"
while IFS=$'\t' read -r tool selected; do
	sha256sum "$selected" >> "$receipt/tools-before.sha256"
done < "$receipt/tool-paths-before.tsv"
# Signed677 scanners with the exact final test: no firmware or imported source.
for path in src/boot/Makefile util/lint/helper_functions.sh \
    util/lint/lint-stable-025-native-boundary util/lint/lint-stable-027-attribute-helpers \
    util/lint/lint-stable-028-camelcase util/lint/lint-stable-029-native-types; do
	mkdir -p "$receipt/old-source/$(dirname "$path")"
	git show "HEAD:$path" > "$receipt/old-source/$path"
done
mkdir "$receipt/old-source/tests"
cp tests/native_boundary_style_lint_test.sh "$receipt/old-source/tests/"
chmod 755 "$receipt/old-source/util/lint/lint-stable-025-native-boundary" \
	"$receipt/old-source/util/lint/lint-stable-027-attribute-helpers" \
	"$receipt/old-source/util/lint/lint-stable-028-camelcase" \
	"$receipt/old-source/util/lint/lint-stable-029-native-types"
cmp tests/native_boundary_style_lint_test.sh "$receipt/old-source/tests/native_boundary_style_lint_test.sh"
# Extract the final lexical fixture verbatim; real CamelCase names must remain.
mkdir "$receipt/lexer-fixture/tests"
awk '
 $0 == "cat > \"$style/tests/lexical_tokens.c\" <<\047EOF\047" { selected = 1; next }
 selected && $0 == "EOF" { exit }
 selected { print }
' tests/native_boundary_style_lint_test.sh > "$receipt/lexer-fixture/tests/lexical_tokens.c"
test -s "$receipt/lexer-fixture/tests/lexical_tokens.c"
find "$receipt/old-source" "$receipt/lexer-fixture" -type f -print0 |
	sort -z | xargs -0 sha256sum > "$receipt/fixtures-before.sha256"
trap finish EXIT
result=0
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
common=(env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C)
gate syntax "${common[@]}" sh -n tests/native_boundary_style_lint_test.sh
test "$(cat "$receipt/syntax.status")" = 0 || result=1
gate models "${common[@]}" sh tests/native_boundary_style_lint_test.sh
test "$(cat "$receipt/models.status")" = 0 || result=1
for scanner in 025-native-boundary 028-camelcase 016-non-ascii; do
	gate "actual-$scanner" "${common[@]}" sh "util/lint/lint-stable-$scanner"
	test "$(cat "$receipt/actual-$scanner.status")" = 0 || result=1
	test ! -s "$receipt/actual-$scanner.log" || result=1
done
gate old-boundary "${common[@]}" sh "$receipt/old-source/tests/native_boundary_style_lint_test.sh"
opposition=0
test "$(cat "$receipt/old-boundary.status")" = 1 || opposition=1
printf '%s\n' "$opposition" > "$receipt/old-boundary-opposition.status"
test "$opposition" = 0 || result=1
# The full old model can fail at a silent hostile assertion. Record that raw
# failure, then prove the actual helper defect independently on the same source.
gate old-actual-boundary "${common[@]}" sh "$receipt/old-source/util/lint/lint-stable-025-native-boundary"
opposition=0
test "$(cat "$receipt/old-actual-boundary.status")" = 0 || opposition=1
printf 'Invalid build source reference: src/boot/Makefile:2911:\t__CDK2_UNSUPPORTED__.c:__CDK2_UNSUPPORTED__.c\n' > "$receipt/old-actual-expected.txt"
grep -Fx -f "$receipt/old-actual-expected.txt" "$receipt/old-actual-boundary.log" > /dev/null || opposition=1
printf '%s\n' "$opposition" > "$receipt/old-actual-boundary-opposition.status"
test "$opposition" = 0 || result=1
cd "$receipt/lexer-fixture"
gate new-lexer "${common[@]}" sh "$source/util/lint/lint-stable-028-camelcase"
gate old-lexer "${common[@]}" sh "$receipt/old-source/util/lint/lint-stable-028-camelcase"
cd "$source"
opposition=0
test "$(cat "$receipt/new-lexer.status")" = 0 || opposition=1
test "$(wc -l < "$receipt/new-lexer.log")" = 3 || opposition=1
for row in '13: CamelCase identifier: firstCamelName' \
    '14: CamelCase identifier: secondCamelName' '15: CamelCase identifier: thirdCamelName'; do
	grep -Fx "tests/lexical_tokens.c:$row" "$receipt/new-lexer.log" > /dev/null || opposition=1
done
test "$(cat "$receipt/old-lexer.status")" = 0 || opposition=1
grep -q 'CamelCase identifier: .*CamelLiteral\|CamelCase identifier: tCHAR16\|CamelCase identifier: nWARNING' "$receipt/old-lexer.log" || opposition=1
printf '%s\n' "$opposition" > "$receipt/old-lexer-opposition.status"
test "$opposition" = 0 || result=1
test "$result" = 0
