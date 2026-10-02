#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -eu
proof=$1
candidate=$2
for profile in p0 p1; do
	config="$proof/$profile/include"
	for opt in 0 2 s; do
		for variant in baseline candidate; do
			source="$proof/baseline"
			if test "$variant" = candidate; then source="$candidate"; fi
			for module in lib/diagnostic lib/diagnostic_event boot/coreboot_handoff \
				boot/entry boot/payload modules/dxe_core/entry; do
				object=$(printf '%s' "$module" | tr / -)
				(cd "$source" && cc -ffreestanding -fno-builtin \
					-fno-stack-protector -fpie -mcmodel=small -fvisibility=hidden \
					-include include/cdk2/native_visibility.h \
					-fno-asynchronous-unwind-tables -fno-unwind-tables \
					-fdata-sections -ffunction-sections -fshort-wchar -m64 \
					-mno-red-zone -mno-sse -mno-mmx -Wall -Werror \
					-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-ident \
					-fcf-protection=none -maccumulate-outgoing-args -O"$opt" \
					-I"$config" -Iinclude -c "src/$module.c" \
					-o "$proof/$profile-$object-$variant-o$opt.o")
			done
			for fixture in src/boot/services_test src/boot/coreboot_test tests/diagnostic_test \
				tests/canonical_image_policy_test tests/diagnostic_runtime_transition_test \
				tests/tcg2_diagnostic_coalesce_test tests/smmstore_diagnostic_test \
				tests/native_entry_test tests/graphics_console_diagnostic_test \
				tests/con_splitter_entry_test \
				tests/nvme_diagnostic_test; do
				object=$(printf '%s' "$fixture" | tr / -)
				gnu_flags=
				if test "$fixture" = tests/canonical_image_policy_test; then
					gnu_flags=-D_GNU_SOURCE
				fi
				if test "$fixture" = src/boot/coreboot_test; then
					gnu_flags=-DCDK2_COREBOOT_BACKEND_TEST
				fi
				case "$fixture" in
					tests/tcg2_diagnostic_coalesce_test|tests/smmstore_diagnostic_test)
						gnu_flags=-DCDK2_DIAGNOSTIC ;;
					tests/graphics_console_diagnostic_test)
						gnu_flags=-DCDK2_DIAGNOSTIC ;;
					tests/nvme_diagnostic_test) gnu_flags=-DCDK2_DEBUG ;;
				esac
				(cd "$source" && cc -std=gnu11 $gnu_flags -DCDK2_HOST_TEST \
					-DCDK2_DIAG_UNIT_TEST -DCDK2_DIAG_DIRECT_SERIAL \
					-Wall -Wextra -Werror -O"$opt" -fshort-wchar -pthread \
					-I"$config" -I"$source/include" \
					-I"$source/src/boot" \
					-I"$source/src/modules/dxe_core" -c "$fixture.c" \
					-o "$proof/$profile-$object-$variant-o$opt.o")
			done
			cc -std=gnu11 -Wall -Wextra -Werror -O"$opt" -fno-pie -no-pie \
				-fshort-wchar -DCDK2_DIAG_UNIT_TEST -pthread \
				-I"$config" -I"$source/include" \
				-DDIAGNOSTIC_FIXTURE_SOURCE="\"$source/tests/diagnostic_test.c\"" \
				"$proof/representation.c" "$source/src/lib/diagnostic.c" \
				-o "$proof/$profile-$variant-o$opt"
			"$proof/$profile-$variant-o$opt" > "$proof/$profile-$variant-o$opt.bin" \
				2> "$proof/$profile-$variant-o$opt.layout"
		done
		for fixture in src-boot-services_test src-boot-coreboot_test tests-diagnostic_test \
			tests-canonical_image_policy_test tests-diagnostic_runtime_transition_test \
			tests-tcg2_diagnostic_coalesce_test tests-smmstore_diagnostic_test \
			tests-native_entry_test tests-graphics_console_diagnostic_test \
			tests-con_splitter_entry_test \
			tests-nvme_diagnostic_test; do
			cmp "$proof/$profile-$fixture-baseline-o$opt.o" "$proof/$profile-$fixture-candidate-o$opt.o"
		done
		for module in lib-diagnostic lib-diagnostic_event boot-coreboot_handoff \
			boot-entry boot-payload modules-dxe_core-entry; do
			cmp "$proof/$profile-$module-baseline-o$opt.o" "$proof/$profile-$module-candidate-o$opt.o"
		done
		cmp "$proof/$profile-baseline-o$opt.bin" "$proof/$profile-candidate-o$opt.bin"
		cmp "$proof/$profile-baseline-o$opt.layout" "$proof/$profile-candidate-o$opt.layout"
		printf '%s O%s six native/eleven HOST complete objects/config padding+bytes/actual console+TIME+HOST HOB/native phase+message APIs PASS\n' "$profile" "$opt"
	done
done
