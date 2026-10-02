#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/q35-vtd-drain.XXXXXX")
source="$root/src/mainboard/emulation/qemu-q35/vtd_registers.c"
printf 'HOST drain artifacts: %s\n' "$temporary"
sha256sum "$source" "$root/src/mainboard/emulation/qemu-q35/vtd_registers.h" \
	"$root/tests/lib/q35_vtd_drain_test.c" "$0" > "$temporary/source-before.sha256"

compile()
{
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -Wvla \
		-fno-pie -no-pie -O"$optimization" -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/mainboard/emulation/qemu-q35" \
		"$root/tests/lib/q35_vtd_drain_test.c" "$1" -o "$2"
}

mutate()
{
	awk -v mutant="$1" -v reverse="$2" '
		BEGIN {
			if (mutant == "reads") {
				old = "\t\tVTD_DRAIN_READS | VTD_DRAIN_WRITES);"
				new = "\t\tVTD_DRAIN_WRITES);"
			} else if (mutant == "writes") {
				old = "\t\tVTD_DRAIN_READS | VTD_DRAIN_WRITES);"
				new = "\t\tVTD_DRAIN_READS);"
			} else if (mutant == "capability") {
				old = "\tif ((capability & required) != required ||"
				new = "\tif (((void)required, 0) ||"
			} else if (mutant == "queued") {
				old = "\t    (status & Q35_VTD_QUEUED_INVALIDATION_ENABLE) ||"
				new = "\t    0 ||"
			} else if (mutant == "pending") {
				old = "\t    (io->read32(io->context, Q35_VTD_CCMD + 4U) & VTD_INVALIDATE) ||"
				new = "\t    0 ||"
			} else if (mutant == "root") {
				old = "\t    read_pair(io, Q35_VTD_RTADDR) != root ||"
				new = "\t    0 ||"
			} else exit 1
			if (reverse) { swap = old; old = new; new = swap }
		}
		/^int q35_vtd_invalidate_drain\(/ { active = 1 }
		active && $0 == old { print new; count++; next }
		{ print }
		active && /^}$/ { active = 0 }
		END { if (count != 1) exit 1 }
	' "$3" > "$4"
}

export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
for optimization in 0 2; do
	compile "$source" "$temporary/positive-O$optimization"
	"$temporary/positive-O$optimization" > "$temporary/positive-O$optimization.log" 2>&1
	printf 'PASS HOST drain request/ack positive O%s (not DMA authority)\n' "$optimization"
	for mutant in reads writes capability queued pending root; do
		mutate "$mutant" 0 "$source" "$temporary/$mutant.c"
		mutate "$mutant" 1 "$temporary/$mutant.c" "$temporary/restored.c"
		cmp "$source" "$temporary/restored.c"
		compile "$temporary/$mutant.c" "$temporary/$mutant-O$optimization"
		status=0
		(ulimit -c 0; "$temporary/$mutant-O$optimization") \
			> "$temporary/$mutant-O$optimization.log" 2>&1 || status=$?
		test "$status" -eq 134
		if test "$mutant" = reads || test "$mutant" = writes; then
			grep -Fxq 'VTD_DRAIN_ASSERT: mock.drained_request == (INVALIDATE | GLOBAL_IOTLB | DRAIN_READS | DRAIN_WRITES)' \
				"$temporary/$mutant-O$optimization.log"
		elif test "$mutant" = root; then
			grep -Fxq 'VTD_DRAIN_ASSERT: q35_vtd_invalidate_drain(&io) == -1 && mock.drain_order' \
				"$temporary/$mutant-O$optimization.log"
		else
			grep -Fxq 'VTD_DRAIN_ASSERT: q35_vtd_invalidate_drain(&io) == -1 && !mock.writes' \
				"$temporary/$mutant-O$optimization.log"
		fi
		! grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
			"$temporary/$mutant-O$optimization.log"
		printf 'PASS HOST drain %s mutant O%s targeted134/noSAN\n' "$mutant" "$optimization"
	done
done
sha256sum -c "$temporary/source-before.sha256"
