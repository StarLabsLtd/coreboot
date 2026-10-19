# SPDX-License-Identifier: GPL-2.0-only

tests-y += efivars-test
tests-y += starlabs-touchpad-cst-retry-test
tests-y += starlabs-touchpad-pixart-retry-test
tests-y += dw-i2c-smm-test
tests-y += starlabs-smi-mailbox-test
tests-y += starlabs-smi-mailbox-pixart-test

efivars-test-srcs += tests/drivers/efivars.c
efivars-test-srcs += src/drivers/efi/efivars.c
efivars-test-srcs += tests/stubs/console.c
efivars-test-srcs += src/commonlib/region.c

efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Pi/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include/

starlabs-touchpad-cst-retry-test-srcs += tests/drivers/starlabs-touchpad-retry.c
starlabs-touchpad-cst-retry-test-srcs += src/mainboard/starlabs/common/touchpad/touchpad.c
starlabs-touchpad-cst-retry-test-stage := smm
starlabs-touchpad-cst-retry-test-config += CONFIG_STARLABS_TOUCHPAD_CST=1 \
	CONFIG_STARLABS_TOUCHPAD_PIXART=0 CONFIG_STARLABS_TOUCHPAD_RUNTIME=1 \
	CONFIG_HAVE_MONOTONIC_TIMER=1
starlabs-touchpad-cst-retry-test-cflags += -I src/mainboard/starlabs/common/include

$(eval $(call copy-test,starlabs-touchpad-cst-retry-test,starlabs-touchpad-pixart-retry-test))
starlabs-touchpad-pixart-retry-test-config += CONFIG_STARLABS_TOUCHPAD_CST=0 \
	CONFIG_STARLABS_TOUCHPAD_PIXART=1 CONFIG_STARLABS_TOUCHPAD_RUNTIME=1

dw-i2c-smm-test-srcs += tests/drivers/dw-i2c-smm.c
dw-i2c-smm-test-srcs += src/drivers/i2c/designware/dw_i2c.c
dw-i2c-smm-test-srcs += tests/stubs/console.c
dw-i2c-smm-test-stage := smm
dw-i2c-smm-test-config += CONFIG_PCI_CONFIG_32BIT_READS_ONLY=0
dw-i2c-smm-test-cflags += -I src/soc/intel/common/block/include

starlabs-smi-mailbox-test-srcs += tests/drivers/starlabs-smi-mailbox.c
starlabs-smi-mailbox-test-srcs += src/mainboard/starlabs/common/smihandler.c
starlabs-smi-mailbox-test-srcs += tests/stubs/console.c
starlabs-smi-mailbox-test-stage := smm
starlabs-smi-mailbox-test-config += CONFIG_STARLABS_ACPI_EFI_OPTION_SMI=1 \
	CONFIG_STARLABS_TOUCHPAD_RUNTIME=1 CONFIG_STARLABS_TOUCHPAD_PIXART=0 \
	CONFIG_STARLABS_TOUCHPAD_CST=1 CONFIG_SOC_INTEL_COMMON_BLOCK_FAST_SPI=0 \
	CONFIG_SOC_AMD_COMMON_BLOCK_ACPIMMIO=0 CONFIG_OPTION_BACKEND_NONE=0 \
	CONFIG_USE_UEFI_VARIABLE_STORE=1
starlabs-smi-mailbox-test-cflags += -I src/mainboard/starlabs/common/include
starlabs-smi-mailbox-test-cflags += -I src/soc/intel/common/block/include
starlabs-smi-mailbox-test-cflags += -I src/soc/intel/apollolake/include
starlabs-smi-mailbox-test-cflags += -I src/ec/starlabs/merlin/variants/merlin

$(eval $(call copy-test,starlabs-smi-mailbox-test,starlabs-smi-mailbox-pixart-test))
starlabs-smi-mailbox-pixart-test-config += CONFIG_STARLABS_TOUCHPAD_CST=0 \
	CONFIG_STARLABS_TOUCHPAD_PIXART=1
