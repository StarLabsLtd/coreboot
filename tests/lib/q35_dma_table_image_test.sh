#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/q35-dma-table-image.XXXXXX")
source="$root/src/mainboard/emulation/qemu-q35/q35_dma_table_image.c"
printf 'HOST table-image artifacts: %s\n' "$temporary"
sha256sum "$source" "$root/src/mainboard/emulation/qemu-q35/q35_dma_table_image.h" \
	"$root/src/mainboard/emulation/qemu-q35/q35_dma_policy.c" \
	"$root/src/mainboard/emulation/qemu-q35/q35_dma_policy.h" \
	"$root/src/mainboard/emulation/qemu-q35/vtd_dma_handoff.c" \
	"$root/tests/lib/q35_dma_table_image_test.c" "$0" > "$temporary/source-before.sha256"
awk '
	/^static size_t hierarchy_page\(/ || /^static void populate_requester_hierarchy\(/ {
		copy = 1; count++
	}
	copy { print }
	copy && /^}$/ { copy = 0 }
	END { if (count != 2 || copy) exit 1 }
' "$root/src/mainboard/emulation/qemu-q35/vtd_dma_handoff.c" > "$temporary/population.h"

compile()
{
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -Wvla \
		-fno-pie -no-pie -O"$optimization" -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer -I"$temporary" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/mainboard/emulation/qemu-q35" \
		"$root/tests/lib/q35_dma_table_image_test.c" "$1" \
		"$root/src/mainboard/emulation/qemu-q35/q35_dma_policy.c" -o "$2"
}

mutate()
{
	awk -v mutant="$1" -v reverse="$2" '
		BEGIN {
			if (mutant == "table-size") {
				old = "\tif (!table || table_size != TABLE_PAGES * PAGE_SIZE ||"
				new = "\tif (!table ||"
			} else if (mutant == "arena-size") {
				old = "\t    !arena || arena_size != (ARENA_PAGES + 1U) * PAGE_SIZE ||"
				new = "\t    !arena ||"
			} else if (mutant == "entry") {
				old = "\t\t\tif (entries[page * (PAGE_SIZE / sizeof(*entries)) + slot] != expected)"
				new = "\t\t\tif (((void)expected, (void)entries, false))"
			} else exit 1
			if (reverse) { swap = old; old = new; new = swap }
		}
		$0 == old { print new; count++; next }
		{ print }
		END { if (count != 1) exit 1 }
	' "$3" > "$4"
}

export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
for optimization in 0 2; do
	compile "$source" "$temporary/positive-O$optimization"
	"$temporary/positive-O$optimization" > "$temporary/positive-O$optimization.log" 2>&1
	printf 'PASS HOST actual table image/physical destinations O%s (not DMA authority)\n' \
		"$optimization"
	for mutant in table-size arena-size entry; do
		mutate "$mutant" 0 "$source" "$temporary/$mutant.c"
		mutate "$mutant" 1 "$temporary/$mutant.c" "$temporary/restored.c"
		cmp "$source" "$temporary/restored.c"
		compile "$temporary/$mutant.c" "$temporary/$mutant-O$optimization"
		status=0
		(ulimit -c 0; "$temporary/$mutant-O$optimization") \
			> "$temporary/$mutant-O$optimization.log" 2>&1 || status=$?
		test "$status" -eq 134
		case "$mutant" in
		table-size)
			expression='!q35_dma_table_image_valid(tables, sizeof(tables) - 1U, arena, arena_size, &targets)'
			;;
		arena-size)
			expression='!q35_dma_table_image_valid(tables, sizeof(tables), arena, arena_size + Q35_DMA_PAGE_SIZE, &targets)'
			;;
		entry)
			expression='!q35_dma_table_image_valid(tables, sizeof(tables), arena, arena_size, &targets)'
			;;
		esac
		grep -Fxq "DMA_TABLE_ASSERT: $expression" "$temporary/$mutant-O$optimization.log"
		! grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
			"$temporary/$mutant-O$optimization.log"
		printf 'PASS HOST table-image %s mutant O%s targeted134/noSAN\n' \
			"$mutant" "$optimization"
	done
done
sha256sum -c "$temporary/source-before.sha256"
