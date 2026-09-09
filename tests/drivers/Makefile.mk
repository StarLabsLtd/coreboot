# SPDX-License-Identifier: GPL-2.0-only

tests-y += efivars-test
tests-y += smmstore-mm-capsule-test
smmstore-mm-capsule-test-srcs += tests/drivers/smmstore-variable.c
smmstore-mm-capsule-test-srcs += src/drivers/smmstore/smi.c
smmstore-mm-capsule-test-srcs += src/drivers/efi/option.c
smmstore-mm-capsule-test-srcs += tests/stubs/console.c
smmstore-mm-capsule-test-srcs += src/commonlib/region.c
smmstore-mm-capsule-test-stage := smm
smmstore-mm-capsule-test-config += CONFIG_SMMSTORE=1 CONFIG_USE_UEFI_VARIABLE_STORE=1 \
	CONFIG_PAYLOAD_MM_INTERFACE=1 \
	CONFIG_OPTION_BACKEND_NONE=0 CONFIG_USE_CBFS_FILE_OPTION_BACKEND=0
smmstore-mm-capsule-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/
smmstore-mm-capsule-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32/

tests-y += payload-mm-test
tests-y += payload-mm-flash-map-test
tests-y += tseg-subregion-test
tseg-subregion-test-srcs += tests/drivers/tseg-subregion-test.c
tseg-subregion-test-srcs += src/cpu/x86/smm/tseg_region.c
tseg-subregion-test-srcs += tests/stubs/console.c
tseg-subregion-test-config += CONFIG_SMM_TSEG=1 CONFIG_SMM_ASEG=0 \
	CONFIG_SMM_TSEG_SIZE=0x1000000 CONFIG_IED_REGION_SIZE=0x400000 \
	CONFIG_SMM_RESERVED_SIZE=0x200000 CONFIG_SMM_OPAL_S3_STATE_SMRAM_SIZE=0x1000 \
	CONFIG_PAYLOAD_MM_SMRAM_SIZE=0x400000

payload-mm-test-srcs += tests/drivers/payload-mm-test.c
payload-mm-test-srcs += src/commonlib/region.c
payload-mm-test-srcs += tests/stubs/console.c
payload-mm-test-stage := smm

payload-mm-flash-map-test-srcs += tests/drivers/payload-mm-flash-map-test.c
payload-mm-flash-map-test-srcs += src/drivers/payload_mm_interface/flash_map.c
payload-mm-flash-map-test-srcs += src/lib/boot_device.c
payload-mm-flash-map-test-srcs += src/commonlib/region.c

efivars-test-srcs += tests/drivers/efivars.c
efivars-test-srcs += src/drivers/efi/efivars.c
efivars-test-srcs += tests/stubs/console.c
efivars-test-srcs += src/commonlib/region.c

efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Pi/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include/
