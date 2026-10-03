#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
stage=/home/sean/pci-bus-mtrr-audit-after614.kFd3bH
source=/home/sean/Documents/.cdk2-worktrees/pci-bus-mtrr-displacement-after614
old=/home/sean/native-private-auth2-after610.FeimaI/artifacts/run.ZKioUc/configured/native
image="$old/pci-bus-immutable-y-debug-y-diagnostic-y-spi-y/PciBusDxe.efi"
export TMPDIR=/home/sean
export CDK2_PE_EXEC_SECTIONS="$old/cdk2-pe-exec-sections"
export OBJDUMP=/usr/bin/objdump
export OBJCOPY=/usr/bin/objcopy

sha256sum "$old/native-direct-images.rsp" "$image" \
	"$old/cdk2-coreboot-stage.elf" "$old/cdk2-coreboot-test" \
	"$old/cdk2-pe-exec-sections" "$old/pe-exec-sections-fixture" \
	"$source/tests/direct_mtrr_manifest.sh" "$source/tests/mtrr_ownership_test.sh" \
	"$source/tests/direct_mtrr_ownership_test.sh" "$stage/original-artifact-audit.sh" \
	> "$stage/original-artifact-before.sha256"
sh "$source/tests/direct_mtrr_manifest.sh" "$old/native-direct-images.rsp" \
	"$stage/original-artifact-manifest" "$old/cdk2-coreboot-stage.elf" \
	"$old/cdk2-coreboot-test" \
	'CDK2_NATIVE_DXE_CORE_PE|false-positive|auto' \
	'CDK2_NATIVE_CPU_PE|c0000080|4' \
	'CDK2_NATIVE_LOCAL_APIC_TIMER_PE|80b,832,838,83e|4' \
	'CDK2_NATIVE_CON_SPLITTER_PE|false-positive|auto' \
	'CDK2_NATIVE_PCI_BUS_PE|false-positive|auto'
sh "$source/tests/mtrr_ownership_test.sh" "$stage/original-artifact-manifest" \
	"$old/cdk2-coreboot-test"
sh "$source/tests/direct_mtrr_ownership_test.sh" \
	"$source/tests/mtrr_ownership_test.sh" "$stage/original-artifact-manifest" \
	"$old/cdk2-coreboot-test" "$old/cdk2-pe-exec-sections" "$image" \
	"$source/tests/direct_mtrr_manifest.sh" "$old/native-direct-images.rsp" \
	"$old/cdk2-coreboot-stage.elf" "$old/pe-exec-sections-fixture" \
	CDK2_NATIVE_PCI_BUS_PE
sha256sum -c "$stage/original-artifact-before.sha256" \
	> "$stage/original-artifact-after-check.log"
