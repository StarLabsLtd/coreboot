#!/bin/bash
set -euo pipefail
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
receipt=/home/sean/protected-boot-hob-final.ZS59fV
source=/home/sean/Documents/.cdk2-worktrees/protected-boot-hob-fixture-after658
prior=/home/sean/default-regression-after656.yHBo5L
header=$prior/build/include/cdk2/config.h
config=$prior/build/.config
cd "$source"
mkdir "$receipt/tmp"
export TMPDIR=$receipt/tmp
ulimit -c 0
printf '1\n' > "$receipt/aggregate.status"
finish()
{
	local execution=$? closure=0
	trap - EXIT
	set +e
	printf '%s\n' "$execution" > "$receipt/execution.status"
	sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log" 2>&1 || closure=1
	sha256sum -c "$receipt/tools-before.sha256" > "$receipt/tools-after-check.log" 2>&1 || closure=1
	sha256sum -c "$receipt/config-before.sha256" > "$receipt/config-after-check.log" 2>&1 || closure=1
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
test "$(cat "$prior/closure.status")" = 0
git rev-parse HEAD > "$receipt/head-before.txt"
test "$(cat "$receipt/head-before.txt")" = b37a977ec5f81b6fa0bce527ab74acd743040a24
git verify-commit HEAD > "$receipt/signature.log" 2>&1
git status --porcelain > "$receipt/status-before.txt"
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = b1fdeefa9232afeb98ece78902b92087e824ff595284634967d127f03425d7ca
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
sha256sum "$config" "$header" > "$receipt/config-before.sha256"
test "$(sha256sum "$config" | cut -d ' ' -f1)" = 4edfabf04f47d30e6fc0ffdb82d80f31a3967742df57a9879c32431c093216ab
test "$(sha256sum "$header" | cut -d ' ' -f1)" = eb6f9c5213c63d577dda51761c00c1018a4f04736e1adc878799869c04b80f02
sha256sum "$0" /usr/bin/time > "$receipt/tools-before.sha256"
for tool in bash sh gcc git awk sha256sum cmp cut cat mkdir env readlink mktemp rm cp grep dirname; do
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
# Exact shared Make source inputs at src/boot/Makefile:7611 and :7909 for
# the retained genuine standalone DEFAULT656 header. Composition is off,
# strict direct is off, and the capsule-profile/system-FMP pair is off.
# The unchanged test shell derives its three local caller-component profiles.
for key in AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPOSITION STRICT_DIRECT_RUNTIME COREBOOT_CAPSULE_PROFILE NATIVE_SYSTEM_FMP; do
	grep -qx "#define CONFIG_CDK2_$key 0" "$header"
done
sources=(
	"$source/src/modules/dxe_core/lifecycle.c"
	"$source/src/lib/capsule_report.c"
	"$source/src/lib/coreboot_hob.c"
	"$source/src/boot/coreboot.c"
	"$source/src/boot/coreboot_checksum.c"
	"$source/src/boot/coreboot_resource.c"
	"$source/src/modules/system_fmp/transport.c"
	"$source/src/modules/authvar_transport/native_x86.c"
	"$source/src/modules/dxe_core/presence_lifecycle.c"
	"$source/src/modules/dxe_core/core.c"
	"$source/src/modules/dxe_core/linked_primary.c"
	"$source/src/modules/dxe_core/database.c"
	"$source/src/modules/dxe_core/event.c"
	"$source/src/modules/dxe_core/memory.c"
	"$source/src/modules/dxe_core/image.c"
	"$source/src/modules/dxe_core/gcd.c"
	"$source/src/modules/dxe_core/fv_protocol.c"
	"$source/src/modules/dxe_core/dispatcher.c"
	"$source/src/modules/dxe_core/direct_dispatch.c"
	"$source/src/lib/linear_boot.c"
	"$source/src/lib/boot_logo.c"
	"$source/src/lib/capsule_disk.c"
	"$source/src/modules/graphics_output/graphics_output.c"
	"$source/src/modules/graphics_output/driver.c"
	"$source/src/lib/tpm2_acpi_hob.c"
	"$source/src/lib/direct_image_table.c"
	"$source/src/lib/diagnostic.c"
	"$source/src/lib/deadline.c"
	"$source/src/boot/pe.c"
	"$source/src/lib/pe_image_view.c"
)
printf '%s\n' "${sources[@]}" > "$receipt/shared-make-inputs.list"
command=(env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C HOSTCC=gcc
	sh tests/protected_variable_boot_gate_test.sh "$header" "${sources[@]}")
printf '%q ' "${command[@]}" > "$receipt/protected.command"
printf '\n' >> "$receipt/protected.command"
status=0
/usr/bin/time -o "$receipt/protected.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
	"${command[@]}" > "$receipt/protected.log" 2>&1 || status=$?
printf '%s\n' "$status" > "$receipt/protected.status"
test "$status" = 0
