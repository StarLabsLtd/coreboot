#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -eu
proof=$1
candidate=$2
ulimit -c 0
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
for profile in p0 p1; do
	for optimization in 0 2 s; do
		for variant in baseline candidate; do
			source=$candidate
			if test "$variant" = baseline; then source=$proof/baseline; fi
			cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" \
				-m64 -fshort-wchar -fno-pie -no-pie \
				$(test "$profile" = p1 && printf '%s' '-DCDK2_DEBUG -DCDK2_DIAGNOSTIC') \
				-I"$proof/$profile/include" -I"$source/include" \
				"$proof/representation.c" \
				"$source/src/modules/acpi_table/acpi_table.c" \
				"$source/src/modules/acpi_table/diagnostic.c" \
				-o "$proof/$profile-$variant-o$optimization"
			"$proof/$profile-$variant-o$optimization" \
				> "$proof/$profile-$variant-o$optimization.bytes" \
				2> "$proof/$profile-$variant-o$optimization.layout"
		done
		cmp "$proof/$profile-baseline-o$optimization.bytes" \
			"$proof/$profile-candidate-o$optimization.bytes"
		cmp "$proof/$profile-baseline-o$optimization.layout" \
			"$proof/$profile-candidate-o$optimization.layout"
		printf '%s O%s actual native layout/padding/all256/high64/ACPI output PASS\n' \
			"$profile" "$optimization"
	done
	for optimization in 0 2; do
		cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" \
			-m64 -fshort-wchar -fno-pie -no-pie \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			$(test "$profile" = p1 && printf '%s' '-DCDK2_DEBUG -DCDK2_DIAGNOSTIC') \
			-I"$proof/$profile/include" -I"$candidate/include" \
			"$proof/representation.c" \
			"$candidate/src/modules/acpi_table/acpi_table.c" \
			"$candidate/src/modules/acpi_table/diagnostic.c" \
			-o "$proof/$profile-sanitized-o$optimization"
		"$proof/$profile-sanitized-o$optimization" \
			> "$proof/$profile-sanitized-o$optimization.bytes" \
			2> "$proof/$profile-sanitized-o$optimization.layout"
		cmp "$proof/$profile-candidate-o$optimization.bytes" \
			"$proof/$profile-sanitized-o$optimization.bytes"
		cmp "$proof/$profile-candidate-o$optimization.layout" \
			"$proof/$profile-sanitized-o$optimization.layout"
		for fixture in acpi_table acpi_table_driver; do
			cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" \
				-m64 -fshort-wchar -fno-pie -no-pie \
				-fsanitize=address,undefined -fno-sanitize-recover=all \
				-I"$proof/$profile/include" -I"$candidate/include" \
				"$candidate/tests/${fixture}_test.c" \
				"$candidate/src/modules/acpi_table/acpi_table.c" \
				$(test "$fixture" = acpi_table_driver && printf '%s' "$candidate/src/modules/acpi_table/driver.c") \
				-o "$proof/$profile-$fixture-sanitized-o$optimization"
			"$proof/$profile-$fixture-sanitized-o$optimization"
		done
		printf '%s O%s strict ASAN/UBSAN actual native bytes+both real fixtures PASS\n' \
			"$profile" "$optimization"
	done
done
