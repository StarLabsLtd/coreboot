# SPDX-License-Identifier: GPL-2.0-only

tests-y += efivars-test

efivars-test-srcs += tests/drivers/efivars.c
efivars-test-srcs += src/drivers/efi/efivars.c
efivars-test-srcs += tests/stubs/console.c
efivars-test-srcs += src/commonlib/region.c

efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Pi/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include/

winbond-sr2-values := 0x100 0x00 0x02 0x40
tests-y += $(foreach value,$(winbond-sr2-values),winbond-sr2-$(value)-test)

define winbond-sr2-test
winbond-sr2-$(1)-test-srcs += tests/drivers/winbond-sr2-test.c
winbond-sr2-$(1)-test-config += CONFIG_SPI_FLASH_WINBOND_SR2_PROVISIONED_VALUE=$(1)
endef

$(foreach value,$(winbond-sr2-values),$(eval $(call winbond-sr2-test,$(value))))
