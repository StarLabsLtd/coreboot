#!/bin/bash
set -euo pipefail
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
receipt=/home/sean/default-regression-after652.hdkK9T
source=/home/sean/Documents/cdk2
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
mbedtls=$producer/3rdparty/mbedtls
build=$receipt/build
expected=${1:?exact published signed regression HEAD required}
[[ $expected =~ ^[0-9a-f]{40}$ ]]
roots=("$source" "$producer" "$mbedtls" "$source/3rdparty/bearssl" "$source/3rdparty/lvgl")

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
for tool in bash make cc gcc ld objcopy objdump nm ar python3 kconfig-conf awk sha256sum; do
	tool_paths+=("$(readlink -f "$(command -v "$tool")")")
done
for tool in cc1 as ld collect2 lto-wrapper; do
	selected=$(gcc "-print-prog-name=$tool")
	case "$selected" in /*) ;; *) selected=$(command -v "$selected") ;; esac
	tool_paths+=("$(readlink -f "$selected")")
done
for archive in libgcc.a liblto_plugin.so; do
	tool_paths+=("$(readlink -f "$(gcc "-print-file-name=$archive")")")
done
sha256sum "${tool_paths[@]}" /usr/bin/time > "$receipt/tools-before.sha256"
mkdir "$receipt/tmp"
common=(env -i PATH="$PATH" TMPDIR="$receipt/tmp" LANG=C LC_ALL=C make -C "$source" -j1
	CDK2_BUILD_DIR="$build" CDK2_DEFCONFIG="$source/defconfig"
	COREBOOT_CONFIG= COREBOOT_TREE="$producer" MBEDTLS_SOURCE="$mbedtls"
	CC=gcc HOSTCC=gcc)
printf '%q ' "${common[@]}" defconfig > "$receipt/configure.command"
printf '\n' >> "$receipt/configure.command"
"${common[@]}" defconfig > "$receipt/configure.log" 2>&1
test ! -s "$build/kconfig/coreboot-source.tmp"
test "$(head -n 1 "$build/kconfig/coreboot-input.identity")" = standalone
sha256sum "$build/.config" "$build/include/cdk2/config.h" \
	"$build/kconfig/coreboot-source.tmp" "$build/kconfig/coreboot-input.identity" \
	> "$receipt/config-before.sha256"
printf '%q ' "${common[@]}" review-profile-check check native-stage > "$receipt/regression.command"
printf '\n' >> "$receipt/regression.command"
/usr/bin/time -o "$receipt/regression.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
	"${common[@]}" review-profile-check check native-stage > "$receipt/regression.log" 2>&1
