#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
proof=$1
candidate=$2
for profile in p0 p1; do
	for opt in 0 2 s; do
		for module in boot/entry boot/coreboot_handoff boot/payload \
			modules/dxe_core/entry modules/cpu_arch/driver \
			modules/cpu_arch/interrupt modules/local_apic_timer/driver boot/pe; do
			object=$(printf '%s' "$module" | tr / -)
			for variant in baseline candidate; do
				source="$proof/baseline"
				if test "$variant" = candidate; then source=$candidate; fi
				(cd "$source" && cc -ffreestanding -fno-builtin \
					-fno-stack-protector -fpie -mcmodel=small -fvisibility=hidden \
					-include include/cdk2/native_visibility.h \
					-fno-asynchronous-unwind-tables -fno-unwind-tables \
					-fdata-sections -ffunction-sections -fshort-wchar -m64 \
					-mno-red-zone -mno-sse -mno-mmx -Wall -Werror \
					-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-ident \
					-fcf-protection=none -maccumulate-outgoing-args -O"$opt" \
					-I"$proof/$profile-$variant/include" -Iinclude \
					-c "src/$module.c" -o "$proof/$profile-$object-$variant-o$opt.o")
			done
			cmp "$proof/$profile-$object-baseline-o$opt.o" \
				"$proof/$profile-$object-candidate-o$opt.o"
		done
	done
done
sha256sum -c "$proof/source-before.sha256"
printf '%s\n' '48 actual native whole-object pairs with respective generated P0/P1 headers: PASS'
