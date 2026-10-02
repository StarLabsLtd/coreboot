#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/q35-dma-cold.XXXXXX")
board="$root/src/mainboard/emulation/qemu-q35"
printf 'HOST current-join artifacts: %s\n' "$temporary"
sha256sum "$board/q35_dma_cold.c" "$board/q35_dma_cold.h" \
	"$board/public_service.c" "$board/public_service.h" "$board/native_service_receiver.c" \
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
	# Expose the exact static inventory only inside the HOST test translation unit.
	awk '{ print } END {
		print "bool host_inventory(bool clear_master) { return inventory(clear_master); }"
		print "size_t host_scope_snapshot(void *output, size_t capacity) {"
		print "if (!output || capacity < sizeof(capsule_scope)) return 0;"
		print "memcpy(output, &capsule_scope, sizeof(capsule_scope));"
		print "return sizeof(capsule_scope); }"
	}' \
		"$1" > "$2-source.c"
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
		"$root/tests/lib/q35_dma_cold_test.c" "$2-source.c" \
		"$board/q35_dma_controller_cancel.c" "$board/q35_dma_table_image.c" \
		"$board/q35_dma_policy.c" "$board/vtd_registers.c" -o "$2"
}

scope_mutate()
{
	MODE="$1" REVERSE="$2" perl -0777 -pe '
		my %guards = (
			first => ["cancel_queues = !capsule_scope.retired;", "cancel_queues = false; /* HOST_SCOPE_FIRST */"],
			reuse => ["cancel_queues = !capsule_scope.retired;", "cancel_queues = true; /* HOST_SCOPE_REUSE */"],
			generation => ["q35_public_capsule_generation() == capsule_scope.generation;", "true /* HOST_SCOPE_GENERATION */;"],
			poison => ["else\n\t\tcapsule_scope.poisoned = true;", "else\n\t\tcapsule_scope.poisoned = false; /* HOST_SCOPE_POISON */"],
			bme => ["inventory(cancel_queues)", "inventory(true /* HOST_SCOPE_BME */)"],
			leave => ["memset(&capsule_scope, 0, sizeof(capsule_scope));", "(void)0; /* HOST_SCOPE_LEAVE */"],
			nested => ["if (capsule_scope.active) {", "if (false /* HOST_SCOPE_NESTED */) {"],
			controller => ["cancel_queues ? cancel[i](&io) : disabled[i](&io)", "cancel_queues ? cancel[i](&io) : (disabled[i](&io) || true /* HOST_SCOPE_CONTROLLER */)"],
			late => ["if (!controllers_retired(wave, false))", "if (false /* HOST_SCOPE_LATE */)"],
			edu => ["if (!cancel_queues)\n\t\t\treturn retired && !(command & 1U);", "if (false /* HOST_SCOPE_EDU */)\n\t\t\treturn retired && !(command & 1U);"],
			protection => ["smm_invocation_runtime_range_is_protected(view, &capsule_scope,\n\t\t\tsizeof(capsule_scope)) == CB_SUCCESS;", "true /* HOST_SCOPE_PROTECTION */;"],
			lateprotection => ["valid = dma_current(DMA_CAPSULE_RAM) && scope_current();\n\tif (!scope_protected())", "valid = dma_current(DMA_CAPSULE_RAM) && scope_current();\n\tif (false /* HOST_SCOPE_LATE_PROTECTION */)"],
		);
		die "unknown scope mutation\n" unless exists $guards{$ENV{MODE}};
		my ($old, $new) = @{$guards{$ENV{MODE}}};
		($old, $new) = ($new, $old) if $ENV{REVERSE};
		die "scope match is not unique\n" unless s/\Q$old\E/$new/g == 1;
	' "$3"
}

export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ulimit -c 0
for optimization in 0 2; do
	compile "$board/q35_dma_cold.c" "$temporary/positive-O$optimization"
	"$temporary/positive-O$optimization" > "$temporary/positive-O$optimization.log" 2>&1
	for mode in scope-metadata scope-nested scope-partial scope-reuse scope-generation \
		scope-owner scope-bme scope-nvme scope-xhci scope-ahci scope-root scope-table \
		scope-resource scope-window scope-drain scope-late-nvme scope-late-xhci \
		scope-late-ahci scope-late-edu scope-protected-enter scope-protected-current \
		scope-protected-leave scope-late-protection; do
		"$temporary/positive-O$optimization" "$mode" \
			> "$temporary/positive-$mode-O$optimization.log" 2>&1
		printf 'PASS HOST protected-lifetime model %s O%s\n' "$mode" "$optimization"
	done
	for mode in ats root pmr memory protected phase ram-held ram-window ram-final \
		inventory-fast inventory-bridge inventory-count inventory-pxb inventory-bme \
		inventory-ecam inventory-cleanup; do
		"$temporary/positive-O$optimization" "$mode" \
			> "$temporary/positive-$mode-O$optimization.log" 2>&1
		MODE="$mode" perl -0777 -pe '
			my ($old, $new);
			if ($ENV{MODE} eq "inventory-fast") {
				$old = "if (valid && count == ARRAY_SIZE(topology))";
				$new = "if (false /* HOST_CAUSAL_FAST */)";
			} elsif ($ENV{MODE} eq "inventory-bridge") {
				$old = "(read8((void *)(config + PCI_HEADER_TYPE)) & 0x7fU) != PCI_HEADER_TYPE_NORMAL";
				$new = "false /* HOST_CAUSAL_BRIDGE */";
			} elsif ($ENV{MODE} eq "inventory-count") {
				$old = "if (valid && count == ARRAY_SIZE(topology))";
				$new = "if (valid /* HOST_CAUSAL_COUNT */)";
			} elsif ($ENV{MODE} eq "inventory-pxb") {
				$old = "devfn <= UINT8_MAX";
				$new = "devfn < UINT8_MAX /* HOST_CAUSAL_LAST */";
			} elsif ($ENV{MODE} eq "inventory-bme") {
				$old = "if (read16((void *)(config + PCI_COMMAND)) & PCI_COMMAND_MASTER)";
				$new = "if (false /* HOST_CAUSAL_BME */)";
			} elsif ($ENV{MODE} eq "inventory-ecam") {
				$old = "return ecam_current();";
				$new = "return true /* HOST_CAUSAL_ECAM */;";
			} elsif ($ENV{MODE} eq "inventory-cleanup") {
				$old = "if (!clear_master)";
				$new = "if (true /* HOST_CAUSAL_CLEANUP */)";
			} elsif ($ENV{MODE} eq "ats") {
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
				$old = "\tif (!wave_current(wave))\n\t\tgoto out;\n\tif (!inventory(cancel_queues)";
				$new = "\tif (false /* HOST_CAUSAL_PHASE */)\n\t\tgoto out;\n\tif (!inventory(cancel_queues)";
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
			if ($ENV{MODE} eq "inventory-fast") {
				$new = "if (false /* HOST_CAUSAL_FAST */)";
				$old = "if (valid && count == ARRAY_SIZE(topology))";
			} elsif ($ENV{MODE} eq "inventory-bridge") {
				$new = "false /* HOST_CAUSAL_BRIDGE */";
				$old = "(read8((void *)(config + PCI_HEADER_TYPE)) & 0x7fU) != PCI_HEADER_TYPE_NORMAL";
			} elsif ($ENV{MODE} eq "inventory-count") {
				$new = "if (valid /* HOST_CAUSAL_COUNT */)";
				$old = "if (valid && count == ARRAY_SIZE(topology))";
			} elsif ($ENV{MODE} eq "inventory-pxb") {
				$new = "devfn < UINT8_MAX /* HOST_CAUSAL_LAST */";
				$old = "devfn <= UINT8_MAX";
			} elsif ($ENV{MODE} eq "inventory-bme") {
				$new = "if (false /* HOST_CAUSAL_BME */)";
				$old = "if (read16((void *)(config + PCI_COMMAND)) & PCI_COMMAND_MASTER)";
			} elsif ($ENV{MODE} eq "inventory-ecam") {
				$new = "return true /* HOST_CAUSAL_ECAM */;";
				$old = "return ecam_current();";
			} elsif ($ENV{MODE} eq "inventory-cleanup") {
				$new = "if (true /* HOST_CAUSAL_CLEANUP */)";
				$old = "if (!clear_master)";
			} elsif ($ENV{MODE} eq "ats") {
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
		if test "$mode" = inventory-fast; then
			grep -Fx 'DMA_COLD_ASSERT: !other_identity_reads' "$temporary/$mode-O$optimization.log"
		elif test "$mode" = inventory-cleanup; then
			grep -Fx 'DMA_COLD_ASSERT: !(extra_pci[1][PCI_COMMAND] & PCI_COMMAND_MASTER)' \
				"$temporary/$mode-O$optimization.log"
		elif test "${mode#inventory-}" != "$mode"; then
			grep -Fx 'DMA_COLD_ASSERT: !host_inventory(true)' "$temporary/$mode-O$optimization.log"
		elif test "$mode" = phase; then
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
	for mutation in first reuse generation poison bme leave nested controller late edu protection lateprotection; do
		scope_mutate "$mutation" 0 "$board/q35_dma_cold.c" > "$temporary/scope-$mutation.c"
		scope_mutate "$mutation" 1 "$temporary/scope-$mutation.c" > "$temporary/scope-$mutation-inverse.c"
		cmp "$board/q35_dma_cold.c" "$temporary/scope-$mutation-inverse.c"
		compile "$temporary/scope-$mutation.c" "$temporary/scope-$mutation-O$optimization"
		case "$mutation" in
			first) mode=scope-reuse; assertion='q35_capsule_ram_dma_current()';;
			reuse) mode=scope-reuse; assertion='nvme_resets == 1U && xhci_resets == 1U && ahci_resets == 1U && drains == 4U';;
			generation) mode=scope-generation; assertion='!accepted';;
			poison) mode=scope-partial; assertion='!q35_capsule_ram_dma_current()';;
			bme) mode=scope-bme; assertion='!accepted';;
			leave) mode=scope-metadata; assertion='!q35_capsule_ram_dma_current()';;
			nested) mode=scope-nested; assertion='!q35_capsule_dma_scope_enter()';;
			controller) mode=scope-xhci; assertion='!accepted';;
			late) mode=scope-late-xhci; assertion='!accepted';;
			edu) mode=scope-late-edu; assertion='!accepted';;
			protection) mode=scope-protected-enter; assertion='!q35_capsule_dma_scope_enter()';;
			lateprotection) mode=scope-late-protection; assertion='!memcmp(scope_before, scope_after, scope_size)';;
		esac
		status=0
		"$temporary/scope-$mutation-O$optimization" "$mode" \
			> "$temporary/scope-$mutation-O$optimization.log" 2>&1 || status=$?
		test "$status" = 134
		grep -Fx "DMA_COLD_ASSERT: $assertion" "$temporary/scope-$mutation-O$optimization.log"
		grep -q 'Assertion.*condition.*failed' "$temporary/scope-$mutation-O$optimization.log"
		if grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
		    "$temporary/scope-$mutation-O$optimization.log"; then
			exit 1
		fi
		printf 'PASS exact HOST scope %s cause O%s (targeted assertion 134)\n' "$mutation" "$optimization"
	done
	printf 'PASS HOST modeled current join O%s (not firmware admission)\n' "$optimization"
done
sha256sum -c "$temporary/source-before.sha256"
