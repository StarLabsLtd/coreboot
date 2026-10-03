#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
proof=/home/sean/phase-owned-lvgl-proof.CuEvQK/joined-619
root=/home/sean/Documents/.cdk2-worktrees/phase-owned-lvgl-status-after614
sha256sum "$root/src/modules/dxe_core/entry.c" "$root/src/boot/Makefile" \
	"/home/sean/phase-owned-lvgl-proof.CuEvQK/run-joined-native-entry-objects.sh" > "$proof/native-entry-before.sha256"
for profile in closure-p0 p1 renderer-without-hotkey; do
	configuration=/home/sean/phase-owned-lvgl-proof.CuEvQK/$profile
	sha256sum "$configuration/resolved.config" "$configuration/include/cdk2/config.h" \
		>> "$proof/native-entry-before.sha256"
	flags=()
	grep -qx 'CONFIG_CDK2_BUILD_DEBUG=y' "$configuration/resolved.config" && flags+=(-DCDK2_DEBUG)
	grep -qx 'CONFIG_CDK2_DIAGNOSTIC=y' "$configuration/resolved.config" && flags+=(-DCDK2_DIAGNOSTIC)
	# Same actual dxe-core entry rule flags, with real generated headers; this
	# compiles a production TU only, not a linked Core or native dispatch proof.
	cc -ffreestanding -fno-builtin -fno-stack-protector -fpie -mcmodel=small \
		-fvisibility=hidden -include "$root/include/cdk2/native_visibility.h" \
		-fno-asynchronous-unwind-tables -fno-unwind-tables -fshort-wchar \
		-m64 -mno-red-zone -mno-sse -mno-mmx -Os -Wall -Werror \
		-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args "${flags[@]}" \
		-I"$configuration/include" -I"$root/include" -MMD -MP \
		-MF "$proof/native-entry-$profile.d" \
		-c "$root/src/modules/dxe_core/entry.c" -o "$proof/native-entry-$profile.o"
done
sha256sum -c "$proof/native-entry-before.sha256"
echo 'Production entry TU: genuine P0 / P1 / renderer-without-hotkey compile PASS'
