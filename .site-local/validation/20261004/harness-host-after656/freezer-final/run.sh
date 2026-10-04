#!/bin/bash
set -euo pipefail
receipt=/home/sean/tpm-empty-diagnostics-final.LA8Itf
source=/home/sean/Documents/.cdk2-worktrees/tpm-empty-diagnostics-after656
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
mkdir "$receipt/tmp"
export TMPDIR=$receipt/tmp
cd "$source"
printf '1\n' > "$receipt/aggregate.status"

finish()
{
	local original=$? closure=0
	trap - EXIT
	set +e
	printf '%s\n' "$original" > "$receipt/execution.status"
	sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log" 2>&1 || closure=1
	sha256sum -c "$receipt/tools-before.sha256" > "$receipt/tools-after-check.log" 2>&1 || closure=1
	git rev-parse HEAD > "$receipt/head-after.txt"
	git status --porcelain > "$receipt/status-after.txt"
	git diff --binary > "$receipt/source-after.diff"
	cmp "$receipt/head-before.txt" "$receipt/head-after.txt" || closure=1
	cmp "$receipt/status-before.txt" "$receipt/status-after.txt" || closure=1
	cmp "$receipt/frozen.diff" "$receipt/source-after.diff" || closure=1
	printf '%s\n' "$closure" > "$receipt/closure.status"
	if test "$original" = 0 && test "$closure" = 0; then
		printf '0\n' > "$receipt/aggregate.status"
		exit 0
	fi
	printf '1\n' > "$receipt/aggregate.status"
	exit 1
}
trap finish EXIT

git rev-parse HEAD > "$receipt/head-before.txt"
test "$(cat "$receipt/head-before.txt")" = 14a42004d0b4932dccf5393a2e1cf7bc380494b4
git verify-commit HEAD > "$receipt/signature.log" 2>&1
git status --porcelain > "$receipt/status-before.txt"
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = f4a0483ec656d09d0eb712ab06049c6938fdca5a130a0788860147ce5f061ba9
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
sha256sum "$0" /usr/bin/time > "$receipt/tools-before.sha256"
for tool in bash python3 git awk sha256sum cmp cut cat mkdir env readlink; do
	sha256sum "$(readlink -f "$(type -P "$tool")")" >> "$receipt/tools-before.sha256"
done

gate_result=0
gate()
{
	local label=$1 status=0
	shift
	printf '%q ' "$@" > "$receipt/$label.command"
	printf '\n' >> "$receipt/$label.command"
	/usr/bin/time -o "$receipt/$label.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
		"$@" > "$receipt/$label.log" 2>&1 || status=$?
	printf '%s\n' "$status" > "$receipt/$label.status"
	test "$status" = 0 || gate_result=1
	return 0
}

common=(env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1)
files=(util/qemu/bin/freeze-tpm-evidence.py util/qemu/bin/freeze-tpm-evidence-selftest.py
	util/qemu/bin/assert-tpm-event-log.py util/qemu/bin/assert-tpm-event-log-selftest.py)
gate syntax-ast "${common[@]}" python3 -B -c \
	'import ast, pathlib, sys; [ast.parse(pathlib.Path(name).read_text(), filename=name) for name in sys.argv[1:]]' "${files[@]}"
gate freezer-selftest "${common[@]}" python3 -B util/qemu/bin/freeze-tpm-evidence-selftest.py
# This checks model refusal robustness only, not optimized runtime admission.
gate freezer-selftest-optimized "${common[@]}" python3 -O -B util/qemu/bin/freeze-tpm-evidence-selftest.py
gate event-log-selftest "${common[@]}" python3 -B util/qemu/bin/assert-tpm-event-log-selftest.py
git diff --check > "$receipt/diff-check.log"
test "$gate_result" = 0
