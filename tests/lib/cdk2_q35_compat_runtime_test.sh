#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
fixture=$root/configs/config.emulation_qemu_x86_q35_cdk2_compat_runtime
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

grep -q 'compatibility/runtime acceptance only' "$fixture"
grep -q 'not a production composition' "$fixture"

make -s -C "$root" DOTCONFIG="$temporary/config" \
	KBUILD_DEFCONFIG="$fixture" defconfig

for setting in \
	CONFIG_BOARD_EMULATION_QEMU_X86_Q35=y \
	CONFIG_CPU_QEMU_X86_TSEG_SMM=y \
	CONFIG_PAYLOAD_CDK2=y \
	CONFIG_PAYLOAD_OWNS_PCI_DEVICES=y \
	CONFIG_CDK2_STRICT_DIRECT_RUNTIME=y \
	CONFIG_CDK2_QEMU_ACCEPTANCE_PROFILE=y \
	CONFIG_CDK2_CPU_VENDOR_INTEL=y \
	CONFIG_CDK2_LINEAR_BOOT=y \
	CONFIG_CDK2_BUILD_DEBUG=y \
	CONFIG_CDK2_DIAGNOSTIC=y \
	CONFIG_CDK2_SERIAL=y \
	CONFIG_CDK2_CBMEM_CONSOLE=y \
	CONFIG_CDK2_SPI_CONSOLE=y \
	CONFIG_PAYLOAD_LOCAL_APIC_TIMER_INFO=y \
	CONFIG_PAYLOAD_RESOURCE_HANDOFF=y \
	CONFIG_PAYLOAD_DMA_HANDOFF=y \
	CONFIG_Q35_VTD_DMA_TEST_BACKEND=y \
	CONFIG_CONSOLE_SPI_FLASH=y \
	CONFIG_PAYLOAD_SPI_FLASH_CONSOLE=y \
	CONFIG_SMMSTORE=y \
	CONFIG_DRIVERS_EFI_VARIABLE_STORE=y; do
	grep -qx "$setting" "$temporary/config" || {
		echo "Q35 compatibility fixture lacks resolved setting: $setting" >&2
		exit 1
	}
done

grep -qx '# CONFIG_CDK2_SETUP_UI is not set' "$temporary/config"
grep -qx 'CONFIG_CDK2_BOOT_TIMEOUT=0' "$temporary/config"
grep -qx 'CONFIG_FMDFILE="src/mainboard/emulation/qemu-q35/spi-console.fmd"' \
	"$temporary/config"

# The compatibility provider belongs only to this explicitly named fixture.
! grep -q 'config.emulation_qemu_x86_q35_cdk2_compat_runtime' \
	"$root/payloads/external/cdk2/Kconfig" \
	"$root/payloads/external/cdk2/Kconfig.name" \
	"$root/payloads/external/Makefile.mk"

printf '%s\n' 'CDK2 Q35 compatibility/runtime configuration: PASS'
