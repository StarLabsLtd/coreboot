#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
ulimit -c 0

make -C "$root" obj="$temporary/build" CMAKE="${CMAKE:-cmake}" \
	build-tests/lib/bootsplash-retained-test
"$temporary/build/tests/tests/lib/bootsplash-retained-test/run"

sed "s|../../src/|$root/src/|g" "$root/tests/lib/bootsplash_retained-test.c" \
	> "$temporary/fixture.c"
sed 's/requested = ALIGN_UP(size, DYN_CBMEM_ALIGN_SIZE);/requested = size;/' \
	"$root/src/lib/bmp_logo.c" > "$temporary/undersized-owner.c"
sed 's/config->panel_orientation != LB_FB_ORIENTATION_NORMAL) {/false) {/' \
	"$root/src/lib/render_bmp.c" > "$temporary/unrotated-owner.c"

compile()
{
	name=$1
	optimization=$2
	source=$3
	"${CC:-cc}" -std=gnu23 -Wall -Wextra -Werror -Wundef \
		-Wno-unused-parameter -Wno-sign-compare -fno-builtin -fno-pie -fno-pic \
		"$optimization" -fsanitize=address,undefined -fno-sanitize-recover=all \
		-ffunction-sections -fdata-sections -D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/build/tests/tests/lib/bootsplash-retained-test" \
		-I"$temporary/build/tests" -I"$root/tests/include/mocks" \
		-I"$root/tests/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$root/src/lib" \
		-I"${VBOOT_SOURCE:-$root/3rdparty/vboot}/firmware/include" \
		-I"$root/3rdparty/cmocka/include" "$source" \
		"$root/src/lib/imd.c" "$root/src/lib/memrange.c" \
		"$root/src/device/device_util.c" "$root/tests/stubs/console.c" \
		"$root/tests/stubs/die.c" -Wl,--gc-sections -no-pie \
		-L"$temporary/build/tests/util/cmocka/src" \
		-Wl,-rpath,"$temporary/build/tests/util/cmocka/src" -lcmocka \
		-o "$temporary/$name"
}

for optimization in -O0 -O2; do
	compile "baseline$optimization" "$optimization" "$temporary/fixture.c"
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/baseline$optimization"
	for mutation in undersized-owner unrotated-owner; do
		case "$mutation" in
		undersized-owner)
			expected_status=1
			sed "s|$root/src/lib/bmp_logo.c|$temporary/$mutation.c|" \
				"$temporary/fixture.c" > "$temporary/mutant.c"
			;;
		unrotated-owner)
			expected_status=2
			sed "s|$root/src/lib/render_bmp.c|$temporary/$mutation.c|" \
				"$temporary/fixture.c" > "$temporary/mutant.c"
			;;
		esac
		compile "$mutation$optimization" "$optimization" "$temporary/mutant.c"
		status=0
		ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/$mutation$optimization" > "$temporary/mutant.log" 2>&1 \
			|| status=$?
		if [ "$status" != "$expected_status" ] ||
		   ! rg -q '\[  FAILED  \] test_(rotated_retention|offset_small_root)' \
			"$temporary/mutant.log" ||
		   rg -q 'runtime error:|Sanitizer|Segmentation fault' "$temporary/mutant.log"; then
			cat "$temporary/mutant.log"
			echo "Retained splash mutation escaped its exact assertion: $mutation" >&2
			exit 1
		fi
	done
done
echo 'Actual retained splash owners, memory map and causal mutants PASS'
