#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -eu
proof=$1
candidate=$2
for profile in p0 p1; do
	for optimization in 0 2 s; do
		for variant in baseline candidate; do
			source=$candidate
			if test "$variant" = baseline; then source=$proof/baseline; fi
			for module in acpi_table driver diagnostic; do
				(cd "$source" && cc -ffreestanding -fno-builtin \
					-fno-stack-protector -fpie -mcmodel=small -fvisibility=hidden \
					-include include/cdk2/native_visibility.h \
					-fno-asynchronous-unwind-tables -fno-unwind-tables \
					-fdata-sections -ffunction-sections -fshort-wchar -m64 \
					-mno-red-zone -mno-sse -mno-mmx -Wall -Werror \
					-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-ident \
					-fcf-protection=none -maccumulate-outgoing-args -O"$optimization" \
					$(test "$profile" = p1 && printf '%s' '-DCDK2_DEBUG -DCDK2_DIAGNOSTIC') \
					-I"$proof/$profile/include" -Iinclude \
					-c "src/modules/acpi_table/$module.c" \
					-o "$proof/$profile-$module-$variant-o$optimization.o")
			 done
			for fixture in acpi_table acpi_table_driver; do
				(cd "$source" && cc -std=gnu11 -DCDK2_HOST_TEST \
					-Wall -Wextra -Werror -O"$optimization" -fshort-wchar -m64 \
					-I"$proof/$profile/include" -Iinclude \
					-c "tests/${fixture}_test.c" \
					-o "$proof/$profile-$fixture-test-$variant-o$optimization.o")
			 done
		done
		for module in acpi_table diagnostic; do
			cmp "$proof/$profile-$module-baseline-o$optimization.o" \
				"$proof/$profile-$module-candidate-o$optimization.o"
		 done
		for module in driver acpi_table-test acpi_table_driver-test; do
			status=0
			cmp "$proof/$profile-$module-baseline-o$optimization.o" \
				"$proof/$profile-$module-candidate-o$optimization.o" \
				> "$proof/$profile-$module-o$optimization.cmp" 2>&1 || status=$?
			printf '%s O%s %s actual whole-object comparison status=%s\n' \
				"$profile" "$optimization" "$module" "$status"
			for variant in baseline candidate; do
				objdump -dr "$proof/$profile-$module-$variant-o$optimization.o" \
					> "$proof/$profile-$module-$variant-o$optimization.disassembly"
				size "$proof/$profile-$module-$variant-o$optimization.o"
			 done
		 done
		printf '%s O%s actual native owner/diagnostic whole-object parity PASS\n' \
			"$profile" "$optimization"
	done
done
