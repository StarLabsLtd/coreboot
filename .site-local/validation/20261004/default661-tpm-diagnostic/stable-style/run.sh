#!/bin/bash
set -euo pipefail
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
receipt=/home/sean/stable-lint-style-host-gates.6u6nEI
source=/home/sean/Documents/.cdk2-worktrees/stable-lint-style-after661
header=/home/sean/fresh-normal-focus-after657.CftZZg/build/include/cdk2/config.h
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
test "$(cat "$receipt/head-before.txt")" = 6faf053c28d7b33de30daeaeae8408340685d006
git verify-commit HEAD > "$receipt/signature.log" 2>&1
git status --porcelain > "$receipt/status-before.txt"
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = 9ed8d0a90aa13854a4f478d2c7ba3a8cb9f9f77a8f9090bd6461740d2024c3cb
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
sha256sum "$header" >> "$receipt/source-before.sha256"
sha256sum "$0" /usr/bin/time > "$receipt/tools-before.sha256"
for tool in bash sh gcc git awk sha256sum cmp cut cat mkdir env readlink mktemp rm cp grep dirname find sed sort xargs perl; do
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
for key in CDK2_COREBOOT_CAPSULE_PROFILE CDK2_NATIVE_SYSTEM_FMP PAYLOAD_DMA_HANDOFF; do
	grep -qx "#define CONFIG_$key 1" "$header"
done
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
	test "$status" = 0 || result=1
	return 0
}
common=(env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C HOSTCC=gcc)
for lint in 000-license-headers 021-coreboot-lowercase 027-attribute-helpers 028-camelcase; do
	gate "$lint" "${common[@]}" sh "util/lint/lint-stable-$lint"
	test ! -s "$receipt/$lint.log" || result=1
done
gate modeled-stage "${common[@]}" sh tests/dxe_core_capsule_disk_stage_test.sh "$header" gcc
test "$result" = 0
