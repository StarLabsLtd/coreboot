#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -eu
proof=$1
candidate=$2
for profile in p0 p1; do
	configuration="$proof/$profile/include"
	for optimization in 0 2 s; do
		for variant in baseline candidate; do
			source="$proof/baseline"
			if test "$variant" = candidate; then source="$candidate"; fi
			for module in lib/linear_boot boot/coreboot_handoff modules/dxe_core/entry; do
				object=$(printf '%s' "$module" | tr / -)
				(cd "$source" && cc -ffreestanding -fno-builtin \
					-fno-stack-protector -fpie -mcmodel=small -fvisibility=hidden \
					-include include/cdk2/native_visibility.h \
					-fno-asynchronous-unwind-tables -fno-unwind-tables \
					-fdata-sections -ffunction-sections -fshort-wchar -m64 \
					-mno-red-zone -mno-sse -mno-mmx -Wall -Werror \
					-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-ident \
					-fcf-protection=none -maccumulate-outgoing-args -O"$optimization" \
					-I"$configuration" -Iinclude -c "src/$module.c" \
					-o "$proof/$profile-$object-$variant-o$optimization.o")
			 done
			for fixture in linear_boot splash_status_report protected_variable_boot_gate \
				dxe_core_entry capsule_ram_window_core capsule_certified_refusal_core \
				dxe_core_entry_mixed_partition dxe_core_capsule_disk_handoff; do
				if test "$profile:$fixture" = p0:capsule_certified_refusal_core; then continue; fi
				(cd "$source" && cc -std=gnu11 -DCDK2_HOST_TEST \
					-DCDK2_SPLASH_TEST_DEBUG=0 -Wall -Wextra -Werror \
					-O"$optimization" -fshort-wchar -pthread \
					-I"$configuration" -Iinclude -Isrc/boot -Isrc/modules/dxe_core \
					-Isrc/lib/tcg_hash/vendor/linux/include -c "tests/${fixture}_test.c" \
					-o "$proof/$profile-tests-$fixture-$variant-o$optimization.o")
			 done
			cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" \
				-fno-pie -no-pie -fshort-wchar -m64 \
				-I"$configuration" -I"$source/include" \
				"$proof/representation.c" "$source/src/lib/linear_boot.c" \
				-o "$proof/$profile-$variant-o$optimization"
			"$proof/$profile-$variant-o$optimization" \
				> "$proof/$profile-$variant-o$optimization.bin" \
				2> "$proof/$profile-$variant-o$optimization.layout"
		done
		for object in lib-linear_boot boot-coreboot_handoff modules-dxe_core-entry \
			tests-linear_boot tests-splash_status_report tests-protected_variable_boot_gate \
			tests-dxe_core_entry tests-capsule_ram_window_core tests-capsule_certified_refusal_core \
			tests-dxe_core_entry_mixed_partition tests-dxe_core_capsule_disk_handoff; do
			if test "$profile:$object" = p0:tests-capsule_certified_refusal_core; then continue; fi
			cmp "$proof/$profile-$object-baseline-o$optimization.o" \
				"$proof/$profile-$object-candidate-o$optimization.o"
		done
		cmp "$proof/$profile-baseline-o$optimization.bin" "$proof/$profile-candidate-o$optimization.bin"
		cmp "$proof/$profile-baseline-o$optimization.layout" "$proof/$profile-candidate-o$optimization.layout"
		printf '%s O%s supported consumer objects / native layouts / all256 byte flags / high64 trace / actual1112 HOB PASS\n' "$profile" "$optimization"
	done
done
