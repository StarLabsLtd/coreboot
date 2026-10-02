#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/q35-dma-ahci-cancel.XXXXXX")
source="$root/src/mainboard/emulation/qemu-q35/q35_dma_controller_cancel.c"
printf 'HOST AHCI-cancel artifacts: %s\n' "$temporary"
sha256sum "$source" "$root/src/mainboard/emulation/qemu-q35/q35_dma_controller_cancel.h" \
	"$root/tests/lib/q35_dma_ahci_cancel_test.c" "$0" > "$temporary/source-before.sha256"

compile()
{
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -Wvla \
		-fno-pie -no-pie -O"$optimization" -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer -I"$root/src/mainboard/emulation/qemu-q35" \
		"$root/tests/lib/q35_dma_ahci_cancel_test.c" "$1" -o "$2"
}

mutate()
{
	awk -v mutant="$1" -v reverse="$2" '
		BEGIN {
			if (mutant == "request") {
				old = "\t\t(command & ~AHCI_INTERRUPT_ENABLE) | AHCI_RESET);"
				new = "\t\t(command & ~AHCI_INTERRUPT_ENABLE));"
			} else if (mutant == "engines") {
				old = "\t\tif (engines == UINT32_MAX || (engines & AHCI_PORT_ENGINES) ||"
				new = "\t\tif (engines == UINT32_MAX ||"
			} else if (mutant == "active") {
				old = "\t\t    io->read32(io->context, base + AHCI_PORT_ACTIVE) ||"
				new = "\t\t    false ||"
			} else if (mutant == "issue") {
				old = "\t\t    io->read32(io->context, base + AHCI_PORT_ISSUE))"
				new = "\t\t    false)"
			} else if (mutant == "identity") {
				old = "\t\tio->read32(io->context, AHCI_CAP) == capability &&"
				new = "\t\ttrue &&"
			} else if (mutant == "ports") {
				old = "\tif (capability == UINT32_MAX || !ports || (ports & ~legal_ports) ||"
				new = "\tif (capability == UINT32_MAX || !ports || ((void)legal_ports, false) ||"
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
	printf 'PASS HOST AHCI reset model O%s (not DMA authority)\n' "$optimization"
	for mutant in request engines active issue identity ports; do
		mutate "$mutant" 0 "$source" "$temporary/$mutant.c"
		mutate "$mutant" 1 "$temporary/$mutant.c" "$temporary/restored.c"
		cmp "$source" "$temporary/restored.c"
		compile "$temporary/$mutant.c" "$temporary/$mutant-O$optimization"
		status=0
		(ulimit -c 0; "$temporary/$mutant-O$optimization") \
			> "$temporary/$mutant-O$optimization.log" 2>&1 || status=$?
		test "$status" -eq 134
		case "$mutant" in
		request) expression='q35_dma_ahci_cancel(&io)' ;;
		*) expression='!q35_dma_ahci_cancel(&io)' ;;
		esac
		grep -Fxq "AHCI_CANCEL_ASSERT: $expression" "$temporary/$mutant-O$optimization.log"
		! grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
			"$temporary/$mutant-O$optimization.log"
		printf 'PASS HOST AHCI %s mutant O%s targeted134/noSAN\n' "$mutant" "$optimization"
	done
done
sha256sum -c "$temporary/source-before.sha256"
