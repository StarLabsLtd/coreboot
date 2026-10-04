#!/bin/bash
set -euo pipefail
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
receipt=/home/sean/native-boundary-source-filter-host-gates.VtwT74
source=/home/sean/Documents/.cdk2-worktrees/native-boundary-source-filters-after661
cd "$source"
mkdir "$receipt/tmp" "$receipt/old-source"
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
	sha256sum -c "$receipt/old-source-before.sha256" > "$receipt/old-source-after-check.log" 2>&1 || closure=1
	git rev-parse HEAD > "$receipt/head-after.txt"
	git status --porcelain > "$receipt/status-after.txt"
	git diff --binary > "$receipt/source-after.diff"
	cmp "$receipt/head-before.txt" "$receipt/head-after.txt" || closure=1
	cmp "$receipt/status-before.txt" "$receipt/status-after.txt" || closure=1
	cmp "$receipt/frozen.diff" "$receipt/source-after.diff" || closure=1
	for tool in bash sh git make env awk sha256sum cmp cut cat mkdir readlink mktemp rm cp chmod grep dirname find sed sort xargs perl wc; do
		printf '%s\t%s\n' "$tool" "$(readlink -f "$(type -P "$tool")")"
	done > "$receipt/tool-paths-after.tsv"
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
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = c4fe15b215114e97ba11f91cfabe70c44889454a20074333e2a1ec31032cda08
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
sha256sum "$0" "$receipt/README.txt" /usr/bin/time > "$receipt/tools-before.sha256"
for tool in bash sh git make env awk sha256sum cmp cut cat mkdir readlink mktemp rm cp chmod grep dirname find sed sort xargs perl wc; do
	selected=$(readlink -f "$(type -P "$tool")")
	printf '%s\t%s\n' "$tool" "$selected" >> "$receipt/tool-paths-before.tsv"
	sha256sum "$selected" >> "$receipt/tools-before.sha256"
done
# The old-source opposition uses only seven required source inputs. Every
# helper/scanner is from signed661; only the exact final test is copied over.
for path in src/boot/Makefile util/lint/helper_functions.sh \
    util/lint/lint-stable-025-native-boundary util/lint/lint-stable-027-attribute-helpers \
    util/lint/lint-stable-028-camelcase util/lint/lint-stable-029-native-types; do
	mkdir -p "$receipt/old-source/$(dirname "$path")"
	git show "HEAD:$path" > "$receipt/old-source/$path"
done
mkdir "$receipt/old-source/tests"
cp "$source/tests/native_boundary_style_lint_test.sh" "$receipt/old-source/tests/"
# Fixtures invoke copied linter scripts directly, preserving their Git modes.
chmod 755 "$receipt/old-source/util/lint/lint-stable-025-native-boundary" \
	"$receipt/old-source/util/lint/lint-stable-027-attribute-helpers" \
	"$receipt/old-source/util/lint/lint-stable-028-camelcase" \
	"$receipt/old-source/util/lint/lint-stable-029-native-types"
cmp "$source/tests/native_boundary_style_lint_test.sh" \
	"$receipt/old-source/tests/native_boundary_style_lint_test.sh"
find "$receipt/old-source" -type f -print0 | sort -z | xargs -0 sha256sum > "$receipt/old-source-before.sha256"
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
	return 0
}
common=(env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C)
gate models "${common[@]}" sh tests/native_boundary_style_lint_test.sh
test "$(cat "$receipt/models.status")" = 0 || result=1
gate actual-boundary "${common[@]}" sh util/lint/lint-stable-025-native-boundary
test "$(cat "$receipt/actual-boundary.status")" = 0 || result=1
test ! -s "$receipt/actual-boundary.log" || result=1
gate old-source "${common[@]}" sh "$receipt/old-source/tests/native_boundary_style_lint_test.sh"
opposition=0
test "$(cat "$receipt/old-source.status")" = 1 || opposition=1
grep -q 'Invalid build source reference.*__CDK2_UNSUPPORTED__' "$receipt/old-source.log" || opposition=1
printf '%s\n' "$opposition" > "$receipt/old-source-opposition.status"
test "$opposition" = 0 || result=1
test "$result" = 0
