#!/bin/bash
set -euo pipefail
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
receipt=/home/sean/loader-matrix-path-retry.27wkn8
source=/home/sean/Documents/cdk2
prior=/home/sean/default-regression-after656.yHBo5L
build=$prior/build
expected=14a42004d0b4932dccf5393a2e1cf7bc380494b4
test -f "$prior/closure.status"
test "$(cat "$prior/closure.status")" = 0
test "$(git -C "$source" rev-parse HEAD)" = "$expected"
test -z "$(git -C "$source" status --porcelain)"
git -C "$source" verify-commit "$expected" > "$receipt/signature.log" 2>&1
mkdir "$receipt/tmp"
export TMPDIR=$receipt/tmp
cd "$source"
printf '1\n' > "$receipt/aggregate.status"
finish()
{
	local execution=$? closure=0
	trap - EXIT
	set +e
	printf '%s\n' "$execution" > "$receipt/execution.status"
	sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log" 2>&1 || closure=1
	sha256sum -c "$receipt/tools-before.sha256" > "$receipt/tools-after-check.log" 2>&1 || closure=1
	sha256sum -c "$receipt/artifacts-before.sha256" > "$receipt/artifacts-after-check.log" 2>&1 || closure=1
	test "$(git rev-parse HEAD)" = "$expected" || closure=1
	test -z "$(git status --porcelain)" || closure=1
	printf '%s\n' "$closure" > "$receipt/closure.status"
	if test "$execution" = 0 && test "$closure" = 0; then printf '0\n' > "$receipt/aggregate.status"; exit 0; fi
	exit 1
}
trap finish EXIT
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
sha256sum "$0" /usr/bin/time > "$receipt/tools-before.sha256"
for tool in bash sh python3 mkfs.vfat sgdisk mcopy mmd sbsign sbverify sha256sum realpath awk cut grep sed sort cmp cp dd mkdir mktemp rm chmod cat dirname env readlink jq truncate wc ln; do
	sha256sum "$(readlink -f "$(type -P "$tool")")" >> "$receipt/tools-before.sha256"
done
sha256sum "$build/.config" "$build/include/cdk2/config.h" \
	"$build/native/SecureBootEnroll.efi" "$build/native/SecureBootLoaderProbe.efi" \
	> "$receipt/artifacts-before.sha256"
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
common=(env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1)
gate secure-matrix "${common[@]}" sh tests/secure_boot_loader_matrix_test.sh \
	"$build/native/SecureBootEnroll.efi" "$build/native/SecureBootLoaderProbe.efi"
gate efi-matrix "${common[@]}" sh tests/efi_loader_matrix_test.sh
test "$result" = 0
