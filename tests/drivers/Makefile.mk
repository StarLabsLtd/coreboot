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
tests-y += efi-store-migration-test
efi-store-migration-test-srcs += tests/drivers/efi-store-migration.c \
	src/drivers/efi/efivars.c src/lib/crc_byte.c src/commonlib/region.c tests/stubs/console.c
efi-store-migration-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include \
	-I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32 \
	-I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include

tests-y += efi-store-migration-reboot-test
efi-store-migration-reboot-test-srcs += tests/drivers/efi-store-migration-reboot.c \
	tests/stubs/console.c
efi-store-migration-reboot-test-stage := ramstage
efi-store-migration-reboot-test-config += CONFIG_ASS=1 CONFIG_HAVE_SMI_HANDLER=1 \
	CONFIG_SOC_INTEL_COMMON_BLOCK_SMM=1 CONFIG_SECURITY_FAIL_CLOSED=0

tests-y += designware-smm-i2c-test
designware-smm-i2c-test-srcs += tests/drivers/designware-smm-i2c-test.c \
	src/commonlib/region.c tests/stubs/console.c
designware-smm-i2c-test-stage := smm
designware-smm-i2c-test-config += CONFIG_HAVE_MONOTONIC_TIMER=1 \
	CONFIG_DRIVERS_I2C_DESIGNWARE_CLOCK_MHZ=100
designware-smm-i2c-test-cflags += -I src/soc/intel/common/block/include
