#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
edk2="${EDK2_2609_SOURCE:-/home/sean/Documents/edk2}"
output="${1:?output directory required}"
snapshots="${2:-}"
commit=aab7b589fc59b7e2b8fb7eb79519bf1a5e5a5272
module=MdeModulePkg/Universal/FaultTolerantWriteDxe
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM

test "$(git -C "$edk2" rev-parse HEAD)" = "$commit"
test "$(git -C "$edk2" rev-parse HEAD:$module/FaultTolerantWrite.c)" = \
	10a67767ebc58f99fad33fffe9d780631c6be6c0
test "$(git -C "$edk2" rev-parse HEAD:$module/FtwMisc.c)" = \
	859d8ba5ee6bb11a091c216f0d41fdf6bf17a986
test "$(git -C "$edk2" rev-parse HEAD:$module/UpdateWorkingBlock.c)" = \
	caa87e95d529e83b4ff7523fd556dd921e1f0864
test "$(git -C "$edk2" rev-parse HEAD:$module/FaultTolerantWrite.h)" = \
	6e3a24b2e63b4e937dcaaa19e34a208bbed314e5
test "$(git -C "$edk2" rev-parse HEAD:MdeModulePkg/Include/Guid/SystemNvDataGuid.h)" = \
	9bfd8fafc75354bc79cd7a43f6df3c6b00f3ceba
test "$(git -C "$edk2" rev-parse HEAD:UefiPayloadPkg/SmmStoreFvb/SmmStoreFvbRuntime.c)" = \
	fca063403d9f6f4c594cf5c002e138f830bb06e8

for source in FaultTolerantWrite.c FtwMisc.c UpdateWorkingBlock.c \
	FaultTolerantWrite.h; do
	git -C "$edk2" show "HEAD:$module/$source" > "$tmp/$source"
done
git -C "$edk2" archive "$commit" MdePkg/Include MdeModulePkg/Include | \
	tar -x -C "$tmp"
printf '%s\n' '#include <Uefi.h>' \
	'extern EFI_GUID gEfiCallerIdGuid;' > "$tmp/shim.h"

include="-I$tmp -I$tmp/MdePkg/Include -I$tmp/MdePkg/Include/X64"
include="$include -I$tmp/MdeModulePkg/Include"
flags="-std=gnu11 -O2 -ffunction-sections -fdata-sections -fshort-wchar"
flags="$flags -fno-stack-protector -fno-builtin -include $tmp/shim.h"
flags="$flags -D_PCD_GET_MODE_BOOL_PcdFullFtwServiceEnable=1"
for source in FaultTolerantWrite.c FtwMisc.c UpdateWorkingBlock.c; do
	# shellcheck disable=SC2086
	cc $flags $include -c "$tmp/$source" -o "$tmp/${source%.c}.o"
done
# shellcheck disable=SC2086
cc $flags $include -c \
	"$root/tests/lib/payload_mm_authvar_ftw_edk2_capture.c" \
	-o "$tmp/capture.o"
cc -Wl,--gc-sections -Wl,--wrap=IsBootBlock \
	-Wl,--wrap=FlushSpareBlockToTargetBlock \
	"$tmp/capture.o" "$tmp/FaultTolerantWrite.o" "$tmp/FtwMisc.o" \
	"$tmp/UpdateWorkingBlock.o" -o "$tmp/capture"
mkdir -p "$output"
if [ -n "$snapshots" ]; then
	"$tmp/capture" "$output" "$snapshots"
else
	"$tmp/capture" "$output"
fi
