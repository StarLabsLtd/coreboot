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

tests-y += efi-private-store-test
efi-private-store-test-srcs += tests/drivers/efi-private-store.c
efi-private-store-test-srcs += src/drivers/efi_variable_service/private_store.c
efi-private-store-test-srcs += src/drivers/efi_variable_service/capsule_store.c
efi-private-store-test-srcs += src/commonlib/region.c

tests-y += smmstore-srk-test smmstore-srk-customer-test
$(foreach test,smmstore-srk-test smmstore-srk-customer-test,\
	$(eval $(test)-srcs += tests/drivers/smmstore-srk.c src/drivers/smmstore/store.c \
		src/drivers/smmstore/smi.c src/commonlib/region.c tests/stubs/console.c)\
	$(eval $(test)-stage := smm)\
	$(eval $(test)-config += CONFIG_SMMSTORE_SRK_PIN=1 CONFIG_SMMSTORE_BLOCK_SIZE=0x10000 \
		CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=1 CONFIG_ROM_SIZE=0x40000)\
	$(eval $(test)-cflags += -I tests/drivers/smmstore-srk-include \
		-I src/soc/intel/common/block/include))
smmstore-srk-test-config += CONFIG_SMMSTORE_SRK_PROVISION=1
smmstore-srk-customer-test-config += CONFIG_SMMSTORE_SRK_PROVISION=0

tests-y += smmstore-fixed-mm-test
smmstore-fixed-mm-test-srcs += tests/drivers/smmstore-fixed-mm.c src/drivers/smmstore/smi.c \
	src/commonlib/region.c tests/stubs/console.c
smmstore-fixed-mm-test-stage := smm
smmstore-fixed-mm-test-config += CONFIG_ASS=1 CONFIG_SMMSTORE_BLOCK_SIZE=0x10000

tests-y += opal-s3-test
opal-s3-test-srcs += tests/drivers/opal-s3.c src/commonlib/region.c tests/stubs/console.c
opal-s3-test-stage := smm
opal-s3-test-config += CONFIG_TCG_OPAL_S3_UNLOCK=1 CONFIG_SMM_OPAL_S3_SCRATCH_CBMEM=1 \
	CONFIG_ASS=1 CONFIG_SMM_PCI_RESOURCE_STORE=1 \
	CONFIG_SMM_OPAL_S3_STATE_SMRAM=1 CONFIG_SMM_OPAL_S3_STATE_SMRAM_SIZE=0x1000 \
	CONFIG_ECAM_MMCONF_SUPPORT=1 CONFIG_ECAM_MMCONF_BASE_ADDRESS=0xe0000000 \
	CONFIG_ECAM_MMCONF_BUS_NUMBER=1 CONFIG_ECAM_MMCONF_LENGTH=0x100000

tests-y += efi-variable-service-init-test
efi-variable-service-init-test-srcs += tests/drivers/efi-variable-service-init.c \
	src/drivers/efi/efivars.c src/drivers/efi_variable_service/private_store.c \
	src/commonlib/region.c tests/stubs/console.c
efi-variable-service-init-test-stage := smm
efi-variable-service-init-test-config += CONFIG_ASS=1 CONFIG_SMMSTORE_BLOCK_SIZE=0x10000 \
	CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=0 CONFIG_ROM_SIZE=0x80000
efi-variable-service-init-test-cflags += -I tests/drivers/efi-variable-service-include \
	-I src/soc/intel/common/block/include -I src/soc/intel/alderlake/include \
	-I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include \
	-I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32 \
	-I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include

tests-y += efi-variable-service-resume-test
efi-variable-service-resume-test-srcs += tests/drivers/efi-variable-service-resume.c \
	tests/stubs/console.c
efi-variable-service-resume-test-stage := ramstage
efi-variable-service-resume-test-config += CONFIG_ASS=1 CONFIG_HAVE_ACPI_RESUME=1

tests-y += efi-store-migration-test
efi-store-migration-test-srcs += tests/drivers/efi-store-migration.c \
	src/drivers/efi/efivars.c src/lib/crc_byte.c src/commonlib/region.c tests/stubs/console.c
efi-store-migration-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include \
	-I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32 \
	-I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include
