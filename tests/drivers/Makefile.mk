# SPDX-License-Identifier: GPL-2.0-only

tests-y += efivars-test
tests-y += payload_spi_console-test
tests-y += efi-capsule-ram-handoff-test

payload_spi_console-test-srcs += tests/drivers/payload_spi_console-test.c
payload_spi_console-test-srcs += src/drivers/spi/payload_spi_console.c

efivars-test-srcs += tests/drivers/efivars.c
efivars-test-srcs += src/drivers/efi/efivars.c
efivars-test-srcs += tests/stubs/console.c
efivars-test-srcs += src/commonlib/region.c

efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Pi/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include/

efi-capsule-ram-handoff-test-srcs += tests/drivers/efi_capsule_ram_handoff_test.c
efi-capsule-ram-handoff-test-config += CONFIG_DRIVERS_EFI_CAPSULE_RAM_HANDOFF=1
efi-capsule-ram-handoff-test-no_test_framework = 1
efi-capsule-ram-handoff-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/
efi-capsule-ram-handoff-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32/
efi-capsule-ram-handoff-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Pi/
efi-capsule-ram-handoff-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include/
efi-capsule-ram-handoff-test-cflags += -Wno-attributes

.PHONY: efi-capsule-ram-handoff-host-test
efi-capsule-ram-handoff-host-test:
	@tests/drivers/efi_capsule_ram_handoff_test.sh
