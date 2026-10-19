# SPDX-License-Identifier: GPL-2.0-only

tests-y += efivars-test
tests-y += tseg-subregion-test
tseg-subregion-test-srcs += tests/drivers/tseg-subregion-test.c
tseg-subregion-test-srcs += src/cpu/x86/smm/tseg_region.c
tseg-subregion-test-srcs += tests/stubs/console.c
tseg-subregion-test-config += CONFIG_SMM_TSEG=1 CONFIG_SMM_ASEG=0 \
	CONFIG_SMM_TSEG_SIZE=0x1000000 CONFIG_IED_REGION_SIZE=0x400000 \
	CONFIG_SMM_RESERVED_SIZE=0x200000 CONFIG_SMM_OPAL_S3_STATE_SMRAM_SIZE=0x1000

tests-y += tseg-subregion-overflow-test
tseg-subregion-overflow-test-srcs += tests/drivers/tseg-subregion-overflow-test.c
tseg-subregion-overflow-test-srcs += src/cpu/x86/smm/tseg_region.c
tseg-subregion-overflow-test-no_test_framework := 1
tseg-subregion-overflow-test-cflags += -m32
tseg-subregion-overflow-test-config += CONFIG_SMM_TSEG=1 CONFIG_SMM_ASEG=0 \
	CONFIG_IED_REGION_SIZE=0x80000000 CONFIG_SMM_RESERVED_SIZE=0x80000000 \
	CONFIG_SMM_OPAL_S3_STATE_SMRAM_SIZE=0

tests-y += smm-module-page-table-test
smm-module-page-table-test-srcs += tests/drivers/smm-module-page-table-test.c
smm-module-page-table-test-srcs += tests/stubs/console.c
smm-module-page-table-test-srcs += src/commonlib/region.c
smm-module-page-table-test-config += CONFIG_CPU_PT_ROM_MAP_GB=1

efivars-test-srcs += tests/drivers/efivars.c
efivars-test-srcs += src/drivers/efi/efivars.c
efivars-test-srcs += tests/stubs/console.c
efivars-test-srcs += src/commonlib/region.c

efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Pi/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include/
