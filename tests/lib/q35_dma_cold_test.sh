#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/q35-dma-cold.XXXXXX")
board="$root/src/mainboard/emulation/qemu-q35"
printf 'HOST current-join artifacts: %s\n' "$temporary"
sha256sum "$board/q35_dma_cold.c" "$board/q35_dma_cold.h" \
	"$board/q35_dma_controller_cancel.c" "$board/q35_dma_controller_cancel.h" \
	"$board/q35_dma_table_image.c" "$board/q35_dma_table_image.h" \
	"$board/q35_dma_policy.c" "$board/q35_dma_policy.h" \
	"$board/vtd_registers.c" "$board/vtd_registers.h" \
	"$board/native_service.h" \
	"$root/src/include/cpu/x86/smm.h" "$root/tests/lib/q35_dma_cold_test.c" \
	"$root/tests/lib/q35_dma_cold_test_platform.h" "$0" > "$temporary/source-before.sha256"
mkdir -p "$temporary/include/arch" "$temporary/include/cpu/x86" \
	"$temporary/include/device" "$temporary/include/console" "$temporary/include/boot"
cp "$root/tests/lib/q35_dma_cold_test_platform.h" "$temporary/include/host-platform.h"
awk '
	$0 == "bool q35_capsule_ram_transaction_current(void);" { print; count++ }
	END { if (count != 1) exit 1 }
' "$board/native_service.h" > "$temporary/include/native-entry-point.h"
awk '
	/^struct smm_pci_resource_info \{/ || /^struct smm_dma_owned_memory \{/ { copy = 1; count++ }
	copy { print }
	copy && /^};$/ { copy = 0 }
	END { if (count != 2 || copy) exit 1 }
' "$root/src/include/cpu/x86/smm.h" > "$temporary/include/owned-structures.h"
printf '%s\n' '#define SMM_PCI_RESOURCE_STORE_NUM_RESOURCES 6' \
	'#define DEVTREE_CONST' \
	> "$temporary/include/structure-prefix.h"
for header in arch/pci_io_cfg.h arch/hlt.h cpu/x86/cache.h cpu/x86/smm.h \
	cpu/x86/smm_invocation_runtime.h device/mmio.h console/console.h bootstate.h \
	boot/payload_mm_authvar_service_receiver.h; do
	printf '%s\n' '#include "host-platform.h"' > "$temporary/include/$header"
done
printf '%s\n' '#define CONFIG_ECAM_MMCONF_BUS_NUMBER 256' \
	'#define CONFIG_ECAM_MMCONF_BASE_ADDRESS 0xb0000000U' \
	'#define CONFIG_SMM_TSEG_SIZE 0x800000U' '#define CONFIG_ROM_SIZE 0x800000U' \
	'#define CONFIG_SMM_PCI_RESOURCE_STORE_NUM_SLOTS 8' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"

compile()
{
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wshadow -Wvla \
		-fno-pie -no-pie -O"$optimization" -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer -D__TEST__ -D__COREBOOT__ -D__SMM__ -fno-builtin \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-include "$temporary/include/structure-prefix.h" \
		-include "$temporary/include/host-platform.h" \
		-I"$temporary/include" -I"$board" -idirafter "$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/q35_dma_cold_test.c" "$1" \
		"$board/q35_dma_controller_cancel.c" "$board/q35_dma_table_image.c" \
		"$board/q35_dma_policy.c" "$board/vtd_registers.c" -o "$2"
}

export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ulimit -c 0
for optimization in 0 2; do
	compile "$board/q35_dma_cold.c" "$temporary/positive-O$optimization"
	"$temporary/positive-O$optimization" > "$temporary/positive-O$optimization.log" 2>&1
	for mode in ats root pmr memory protected phase ram-held ram-window ram-final; do
		"$temporary/positive-O$optimization" "$mode" \
			> "$temporary/positive-$mode-O$optimization.log" 2>&1
		MODE="$mode" perl -0777 -pe '
			my ($old, $new);
			if ($ENV{MODE} eq "ats") {
				$old = "capability == PCIE_EXT_CAP_ID_ATS";
				$new = "false /* HOST_CAUSAL_ATS */";
			} elsif ($ENV{MODE} eq "root") {
				$old = "before.root != memory.table_base";
				$new = "false /* HOST_CAUSAL_ROOT */";
			} elsif ($ENV{MODE} eq "pmr") {
				$old = "(before.pmr.enable & (Q35_VTD_PMR_ENABLE | Q35_VTD_PMR_STATUS))";
				$new = "false /* HOST_CAUSAL_PMR */";
			} elsif ($ENV{MODE} eq "memory") {
				$old = "memcmp(&memory, &repeated_memory, sizeof(memory))";
				$new = "0 /* HOST_CAUSAL_MEMORY */";
			} elsif ($ENV{MODE} eq "protected") {
				$old = "smm_invocation_runtime_range_is_protected(view, (const void *)stored,\n\t\tPCI_FUNCTIONS * sizeof(*slots)) != CB_SUCCESS";
				$new = "false /* HOST_CAUSAL_PROTECTED */";
			} elsif ($ENV{MODE} eq "phase") {
				$old = "\tif (!wave_current(wave))\n\t\tgoto out;\n\tif (!inventory(true)";
				$new = "\tif (false /* HOST_CAUSAL_PHASE */)\n\t\tgoto out;\n\tif (!inventory(true)";
			} elsif ($ENV{MODE} eq "ram-held" || $ENV{MODE} eq "ram-window") {
				$old = "return q35_capsule_ram_transaction_current();";
				$new = "return true /* HOST_CAUSAL_RAM */;";
			} elsif ($ENV{MODE} eq "ram-final") {
				$old = "\tif (!wave_current(wave))\n\t\tgoto out;\n\tvalid = true;\nout:";
				# The two functions share this ending; scope the final scanner.
				my $start = index($_, "static bool dma_current(enum dma_wave wave)");
				die "missing final scanner\n" if $start < 0;
				my $prefix = substr($_, 0, $start);
				my $body = substr($_, $start);
				$new = "\tif (false /* HOST_CAUSAL_RAM_FINAL */)\n\t\tgoto out;\n\tvalid = true;\nout:";
				die "final match is not unique\n" unless $body =~ s/\Q$old\E/$new/g == 1;
				$_ = $prefix . $body;
				next;
			} else { die "unknown causal mode\n"; }
			die "causal match is not unique\n" unless s/\Q$old\E/$new/g == 1;
		' "$board/q35_dma_cold.c" > "$temporary/$mode.c"
		MODE="$mode" perl -0777 -pe '
			my ($new, $old);
			if ($ENV{MODE} eq "ats") {
				$new = "false /* HOST_CAUSAL_ATS */";
				$old = "capability == PCIE_EXT_CAP_ID_ATS";
			} elsif ($ENV{MODE} eq "root") {
				$new = "false /* HOST_CAUSAL_ROOT */";
				$old = "before.root != memory.table_base";
			} elsif ($ENV{MODE} eq "pmr") {
				$new = "false /* HOST_CAUSAL_PMR */";
				$old = "(before.pmr.enable & (Q35_VTD_PMR_ENABLE | Q35_VTD_PMR_STATUS))";
			} elsif ($ENV{MODE} eq "memory") {
				$new = "0 /* HOST_CAUSAL_MEMORY */";
				$old = "memcmp(&memory, &repeated_memory, sizeof(memory))";
			} elsif ($ENV{MODE} eq "protected") {
				$new = "false /* HOST_CAUSAL_PROTECTED */";
				$old = "smm_invocation_runtime_range_is_protected(view, (const void *)stored,\n\t\tPCI_FUNCTIONS * sizeof(*slots)) != CB_SUCCESS";
			} elsif ($ENV{MODE} eq "phase") {
				$new = "false /* HOST_CAUSAL_PHASE */";
				$old = "!wave_current(wave)";
			} elsif ($ENV{MODE} eq "ram-held" || $ENV{MODE} eq "ram-window") {
				$new = "return true /* HOST_CAUSAL_RAM */;";
				$old = "return q35_capsule_ram_transaction_current();";
			} elsif ($ENV{MODE} eq "ram-final") {
				$new = "false /* HOST_CAUSAL_RAM_FINAL */";
				$old = "!wave_current(wave)";
			} else { die "unknown reverse mode\n"; }
			die "reverse match is not unique\n" unless s/\Q$new\E/$old/g == 1;
		' "$temporary/$mode.c" > "$temporary/$mode-reversed.c"
		cmp "$board/q35_dma_cold.c" "$temporary/$mode-reversed.c"
		compile "$temporary/$mode.c" "$temporary/$mode-O$optimization"
		status=0
		"$temporary/$mode-O$optimization" "$mode" \
			> "$temporary/$mode-O$optimization.log" 2>&1 || status=$?
		test "$status" = 134
		if test "$mode" = phase; then
			grep -Fx 'DMA_COLD_ASSERT: !nvme_resets && !xhci_resets && !ahci_resets' \
				"$temporary/$mode-O$optimization.log"
		else
			grep -Fx 'DMA_COLD_ASSERT: !accepted' "$temporary/$mode-O$optimization.log"
		fi
		grep -q 'Assertion.*condition.*failed' "$temporary/$mode-O$optimization.log"
		if grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
		    "$temporary/$mode-O$optimization.log"; then
			exit 1
		fi
		printf 'PASS exact HOST %s cause O%s (assertion 134)\n' "$mode" "$optimization"
	done
	printf 'PASS HOST modeled current join O%s (not firmware admission)\n' "$optimization"
done
sha256sum -c "$temporary/source-before.sha256"
