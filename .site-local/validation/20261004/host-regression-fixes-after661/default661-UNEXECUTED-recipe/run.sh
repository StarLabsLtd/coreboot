#!/bin/bash
set -euo pipefail
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
receipt=/home/sean/default-regression-after661.E4XtC9
source=/home/sean/Documents/cdk2
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
mbedtls=$producer/3rdparty/mbedtls
build=$receipt/build
expected=${1:?exact published signed regression HEAD required}
[[ $expected =~ ^[0-9a-f]{40}$ ]]
test "$expected" = 6faf053c28d7b33de30daeaeae8408340685d006
roots=("$source" "$producer" "$mbedtls" "$source/3rdparty/bearssl" "$source/3rdparty/lvgl")
tool_names=(bash sh git ssh-keygen make cc gcc ld objcopy objdump nm ar python3 kconfig-conf awk sha256sum
	mkfs.vfat sgdisk mcopy mmd mdir mdel mmove openssl sbsign sbverify sbattach
	jq truncate wc ln head cmp readlink env mkdir mktemp rm stat cp cut grep sed sort)

resolved_tools()
{
	local tool alias actual
	for tool in "${tool_names[@]}"; do
		alias=$(command -v "$tool") || return
		actual=$(readlink -f "$alias") || return
		test -f "$actual" && test -x "$actual" || return
		printf '%s\t%s\t%s\n' "$tool" "$alias" "$actual"
	done
}

snapshot()
{
	local tree
	for tree in "${roots[@]}"; do
		git -C "$tree" ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
			while IFS= read -r path; do sha256sum "$tree/$path"; done
	done
}

finish()
{
	local original=$? closure=0 tree
	trap - EXIT
	set +e
	printf '%s\n' "$original" > "$receipt/execution.status"
	if test -f "$receipt/source-before.sha256"; then
		sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log" 2>&1 || closure=1
	fi
	if test -f "$receipt/tools-before.sha256"; then
		sha256sum -c "$receipt/tools-before.sha256" > "$receipt/tools-after-check.log" 2>&1 || closure=1
	fi
	if test -f "$receipt/tools-resolved-before.tsv"; then
		resolved_tools > "$receipt/tools-resolved-after.tsv" || closure=1
		cmp "$receipt/tools-resolved-before.tsv" "$receipt/tools-resolved-after.tsv" > "$receipt/tools-resolution-check.log" 2>&1 || closure=1
	fi
	if test -f "$receipt/config-before.sha256"; then
		sha256sum -c "$receipt/config-before.sha256" > "$receipt/config-after-check.log" 2>&1 || closure=1
	fi
	for tree in "${roots[@]}"; do
		git -C "$tree" rev-parse HEAD
		test -z "$(git -C "$tree" status --porcelain)" || closure=1
	done > "$receipt/heads-after.txt"
	if test -f "$receipt/heads-before.txt"; then
		cmp "$receipt/heads-before.txt" "$receipt/heads-after.txt" || closure=1
	fi
	printf '%s\n' "$closure" > "$receipt/closure.status"
	if test "$original" -eq 0 && test "$closure" -eq 0; then
		printf '0\n' > "$receipt/aggregate.status"
		exit 0
	else
		printf '1\n' > "$receipt/aggregate.status"
		exit 1
	fi
}
trap finish EXIT
test "$(git -C "$source" rev-parse HEAD)" = "$expected"
git -C "$source" verify-commit "$expected" > "$receipt/signature.log" 2>&1
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test "$(git -C "$mbedtls" rev-parse HEAD)" = 0bebf8b8c7f07abe3571ded48a11aa907a1ffb20
test "$(git -C "$source/3rdparty/bearssl" rev-parse HEAD)" = 8ef7680081c61b486622f2d983c0d3d21e83caad
test "$(git -C "$source/3rdparty/lvgl" rev-parse HEAD)" = 85aa60d18b3d5e5588d7b247abf90198f07c8a63
test ! -e "$build"
for tree in "${roots[@]}"; do
	test -z "$(git -C "$tree" status --porcelain)"
	git -C "$tree" rev-parse HEAD
done > "$receipt/heads-before.txt"
snapshot > "$receipt/source-before.sha256"
tool_paths=("$(readlink -f "$0")")
resolved_tools > "$receipt/tools-resolved-before.tsv"
while IFS=$'\t' read -r tool alias actual; do tool_paths+=("$actual"); done < "$receipt/tools-resolved-before.tsv"
for tool in cc1 as ld collect2 lto-wrapper; do
	selected=$(gcc "-print-prog-name=$tool")
	case "$selected" in /*) ;; *) selected=$(command -v "$selected") ;; esac
	tool_paths+=("$(readlink -f "$selected")")
done
for archive in libgcc.a liblto_plugin.so libasan.so libubsan.so libtsan.so; do
	tool_paths+=("$(readlink -f "$(gcc "-print-file-name=$archive")")")
done
sha256sum "${tool_paths[@]}" /usr/bin/time > "$receipt/tools-before.sha256"
mkdir "$receipt/tmp"
common=(env -i PATH="$PATH" TMPDIR="$receipt/tmp" LANG=C LC_ALL=C make -C "$source" -j1 -k
	CDK2_BUILD_DIR="$build" CDK2_DEFCONFIG="$source/defconfig"
	COREBOOT_CONFIG= COREBOOT_TREE="$producer" MBEDTLS_SOURCE="$mbedtls"
	CC=gcc HOSTCC=gcc)
printf '%q ' "${common[@]}" defconfig > "$receipt/configure.command"
printf '\n' >> "$receipt/configure.command"
configure_status=0
"${common[@]}" defconfig > "$receipt/configure.log" 2>&1 || configure_status=$?
printf '%s\n' "$configure_status" > "$receipt/configure.status"
test "$configure_status" = 0
test ! -s "$build/kconfig/coreboot-source.tmp"
test "$(head -n 1 "$build/kconfig/coreboot-input.identity")" = standalone
sha256sum "$build/.config" "$build/include/cdk2/config.h" \
	"$build/kconfig/coreboot-source.tmp" "$build/kconfig/coreboot-input.identity" \
	> "$receipt/config-before.sha256"
printf '%q ' "${common[@]}" review-profile-check check native-stage > "$receipt/regression.command"
printf '\n' >> "$receipt/regression.command"
regression_status=0
/usr/bin/time -o "$receipt/regression.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
	"${common[@]}" review-profile-check check native-stage > "$receipt/regression.log" 2>&1 || regression_status=$?
printf '%s\n' "$regression_status" > "$receipt/regression.status"
exit "$regression_status"
