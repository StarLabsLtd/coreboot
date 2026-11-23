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

tests-y += smmstore-ass-test
smmstore-ass-test-srcs += tests/drivers/smmstore-ass.c src/drivers/smmstore/smi.c \
	src/commonlib/region.c tests/stubs/console.c
smmstore-ass-test-stage := smm
smmstore-ass-test-config += CONFIG_ASS=1 CONFIG_SMMSTORE_BLOCK_SIZE=0x10000

tests-y += opal-s3-test
opal-s3-test-srcs += tests/drivers/opal-s3.c src/commonlib/region.c tests/stubs/console.c
opal-s3-test-stage := smm
opal-s3-test-config += CONFIG_TCG_OPAL_S3_UNLOCK=1 CONFIG_SMM_OPAL_S3_SCRATCH_CBMEM=1 \
	CONFIG_ASS=1 CONFIG_SMM_PCI_RESOURCE_STORE=1 \
	CONFIG_SMM_OPAL_S3_STATE_SMRAM=1 CONFIG_SMM_OPAL_S3_STATE_SMRAM_SIZE=0x1000 \
	CONFIG_ECAM_MMCONF_SUPPORT=1 CONFIG_ECAM_MMCONF_BASE_ADDRESS=0xe0000000 \
	CONFIG_ECAM_MMCONF_BUS_NUMBER=1 CONFIG_ECAM_MMCONF_LENGTH=0x100000

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

tests-y += fsp-framebuffer-test
fsp-framebuffer-test-stage := ramstage
fsp-framebuffer-test-srcs += tests/drivers/fsp-framebuffer-test.c
fsp-framebuffer-test-srcs += src/drivers/intel/fsp2_0/graphics.c
fsp-framebuffer-test-srcs += tests/stubs/console.c
fsp-framebuffer-test-cflags += -I tests/include/tests/drivers/fsp-framebuffer
fsp-framebuffer-test-cflags += -I src/drivers/intel/fsp2_0/include

tests-y += vbnv-flash-redundant-test
vbnv-flash-redundant-test-srcs += tests/drivers/vbnv-flash.c src/commonlib/region.c \
	tests/stubs/console.c 3rdparty/vboot/firmware/2lib/2nvstorage.c \
	3rdparty/vboot/firmware/2lib/2crc8.c
vbnv-flash-redundant-test-cflags += -I tests/drivers/vbnv-flash-include \
	-I 3rdparty/vboot/firmware/2lib/include
vbnv-flash-redundant-test-config += CONFIG_ROM_SIZE=0x2000

$(call copy-test,efi-variable-service-init-test,efi-variable-service-vboot-test)
tests-y += intel-fast-spi-resource-test

tests-y += opal-s3-resources-test
opal-s3-resources-test-srcs += tests/drivers/opal-s3-resources.c src/commonlib/region.c \
	tests/stubs/console.c
opal-s3-resources-test-stage := ramstage
opal-s3-resources-test-config += CONFIG_TCG_OPAL_S3_UNLOCK=1 CONFIG_HAVE_ACPI_RESUME=1 \
	CONFIG_SMM_PCI_RESOURCE_STORE=1 CONFIG_SMM_PCI_RESOURCE_STORE_NUM_SLOTS=2
opal-s3-resources-test-cflags += -Wno-address-of-packed-member

tests-y += efi-variable-service-amd-test
efi-variable-service-amd-test-srcs += tests/drivers/efi-variable-service-amd.c \
	src/drivers/efi_variable_service/capsule_store.c src/commonlib/region.c \
	tests/stubs/console.c
efi-variable-service-amd-test-stage := smm
efi-variable-service-amd-test-config += CONFIG_ASS=1 CONFIG_ROM_SIZE=0x2000000 \
	CONFIG_SOC_AMD_PHOENIX_OPENSIL=1 CONFIG_EARLY_RESERVED_DRAM_BASE=0x10000000
efi-variable-service-amd-test-cflags += -I tests/drivers/efi-variable-service-amd-include \
	-I src/soc/amd/common/block/include -I src/soc/amd/phoenix/include

tests-y += designware-smm-i2c-test
designware-smm-i2c-test-srcs += tests/drivers/designware-smm-i2c-test.c \
	src/commonlib/region.c tests/stubs/console.c
designware-smm-i2c-test-stage := smm
designware-smm-i2c-test-config += CONFIG_HAVE_MONOTONIC_TIMER=1 \
	CONFIG_DRIVERS_I2C_DESIGNWARE_CLOCK_MHZ=100
designware-smm-i2c-test-cflags += -I src/soc/intel/common/block/include
