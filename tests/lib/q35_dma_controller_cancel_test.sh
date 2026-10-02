#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/q35-dma-controller-cancel.XXXXXX")
source="$root/src/mainboard/emulation/qemu-q35/q35_dma_controller_cancel.c"
printf 'HOST controller-cancel artifacts: %s\n' "$temporary"
sha256sum "$source" "$root/src/mainboard/emulation/qemu-q35/q35_dma_controller_cancel.h" \
	"$root/tests/lib/q35_dma_controller_cancel_test.c" "$0" > "$temporary/source-before.sha256"

compile()
{
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -Wvla \
		-fno-pie -no-pie -O"$optimization" -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer -I"$root/src/mainboard/emulation/qemu-q35" \
		"$root/tests/lib/q35_dma_controller_cancel_test.c" "$1" -o "$2"
}

mutate()
{
	awk -v mutant="$1" -v reverse="$2" '
		BEGIN {
			if (mutant == "nvme-disable") {
				old = "\tio->write32(io->context, NVME_CC, configuration & ~NVME_CC_ENABLE);"
				new = "\tio->write32(io->context, NVME_CC, configuration);"
			} else if (mutant == "nvme-fatal") {
				old = "\t\tstatus != UINT32_MAX && !(status & (NVME_CSTS_READY | NVME_CSTS_FATAL)) &&"
				new = "\t\tstatus != UINT32_MAX && !(status & NVME_CSTS_READY) &&"
			} else if (mutant == "nvme-identity") {
				old = "\t\tio->read32(io->context, NVME_CAP) == capability_low &&"
				new = "\t\t((void)capability_low, true) &&"
			} else if (mutant == "xhci-reset") {
				old = "\tio->write32(io->context, operational + XHCI_USBCMD, command | XHCI_RESET);"
				new = "\tio->write32(io->context, operational + XHCI_USBCMD, command);"
			} else if (mutant == "xhci-ready") {
				old = "\t    !wait_mask(io, operational + XHCI_USBSTS, XHCI_NOT_READY, 0))"
				new = "\t    false)"
			} else if (mutant == "xhci-identity") {
				old = "\t\t\tXHCI_HALTED && io->read32(io->context, 0) == capability;"
				new = "\t\t\tXHCI_HALTED;"
			} else exit 1
			if (reverse) { swap = old; old = new; new = swap }
		}
		mutant == "xhci-ready" && $0 == "\t    (capability >> 16) < 0x100U || (capability >> 16) > 0x120U ||" {
			scope = 1; markers++
		}
		$0 == old && (mutant != "xhci-ready" || scope) {
			print new; count++; scope = 0; next
		}
		{ print }
		END { if (count != 1 || (mutant == "xhci-ready" && (markers != 1 || scope))) exit 1 }
	' "$3" > "$4"
}

export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
for optimization in 0 2; do
	compile "$source" "$temporary/positive-O$optimization"
	"$temporary/positive-O$optimization" > "$temporary/positive-O$optimization.log" 2>&1
	printf 'PASS HOST controller cancellation model O%s (not DMA authority)\n' "$optimization"
	for mutant in nvme-disable nvme-fatal nvme-identity xhci-reset xhci-ready xhci-identity; do
		mutate "$mutant" 0 "$source" "$temporary/$mutant.c"
		mutate "$mutant" 1 "$temporary/$mutant.c" "$temporary/restored.c"
		cmp "$source" "$temporary/restored.c"
		compile "$temporary/$mutant.c" "$temporary/$mutant-O$optimization"
		status=0
		(ulimit -c 0; "$temporary/$mutant-O$optimization") \
			> "$temporary/$mutant-O$optimization.log" 2>&1 || status=$?
		test "$status" -eq 134
		case "$mutant" in
		nvme-disable) expression='q35_dma_nvme_cancel(&io)' ;;
		nvme-fatal|nvme-identity) expression='!q35_dma_nvme_cancel(&io)' ;;
		xhci-reset) expression='device.resets == 1U && device.pending == 0' ;;
		xhci-ready|xhci-identity) expression='!q35_dma_xhci_cancel(&io)' ;;
		esac
		grep -Fxq "DMA_CANCEL_ASSERT: $expression" "$temporary/$mutant-O$optimization.log"
		! grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
			"$temporary/$mutant-O$optimization.log"
		printf 'PASS HOST controller %s mutant O%s targeted134/noSAN\n' "$mutant" "$optimization"
	done
done
sha256sum -c "$temporary/source-before.sha256"
