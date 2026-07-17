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

tests-y += smmstore-test

smmstore-test-srcs += tests/drivers/smmstore.c
smmstore-test-srcs += tests/stubs/console.c
smmstore-test-srcs += src/commonlib/region.c
smmstore-test-config += CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=1
smmstore-test-config += CONFIG_SMMSTORE_BLOCK_SIZE=4096
smmstore-test-cflags += -I tests/include/tests/drivers/smmstore
