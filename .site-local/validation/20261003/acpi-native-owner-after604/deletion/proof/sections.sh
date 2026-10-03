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
				-c src/modules/acpi_table/diagnostic.c \
				-o "$proof/$profile-$variant-o$optimization.o")
		 done
		if test "$profile" = p1; then
			nm "$proof/$profile-baseline-o$optimization.o" | \
				awk '$3 == "cdk2_acpi_diagnostic_bytes" { found = 1 } END { exit !found }'
			objcopy --remove-section=.text.cdk2_acpi_diagnostic_bytes \
				--strip-symbol=cdk2_diag_bytes \
				"$proof/$profile-baseline-o$optimization.o" \
				"$proof/$profile-baseline-stripped-o$optimization.o"
			objcopy "$proof/$profile-candidate-o$optimization.o" \
				"$proof/$profile-candidate-normalized-o$optimization.o"
			cmp "$proof/$profile-baseline-stripped-o$optimization.o" \
				"$proof/$profile-candidate-normalized-o$optimization.o"
		else
			cmp "$proof/$profile-baseline-o$optimization.o" \
				"$proof/$profile-candidate-o$optimization.o"
		fi
		if nm "$proof/$profile-candidate-o$optimization.o" | \
			awk '$3 == "cdk2_acpi_diagnostic_bytes" { found = 1 } END { exit !found }'; then
			exit 1
		fi
		printf '%s O%s unused-symbol removal and remaining whole object PASS\n' \
			"$profile" "$optimization"
	done
done
