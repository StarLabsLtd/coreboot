# SPDX-License-Identifier: GPL-2.0-only

tests-y += efivars-test

tests-y += smmstore-full-flash-test
smmstore-full-flash-test-srcs += tests/drivers/smmstore-full-flash.c
smmstore-full-flash-test-srcs += src/drivers/smmstore/store.c
smmstore-full-flash-test-srcs += src/lib/boot_device.c
smmstore-full-flash-test-srcs += src/commonlib/region.c
smmstore-full-flash-test-srcs += tests/stubs/console.c
smmstore-full-flash-test-stage := smm
smmstore-full-flash-test-config += CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=1 \
	CONFIG_SMMSTORE_BLOCK_SIZE=65536 CONFIG_ROM_SIZE=0x2000000
smmstore-full-flash-test-cflags += -Itests/drivers/smmstore-full-flash


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

tests-y += efi-variable-service-init-test efi-variable-service-4k-test
$(foreach test,efi-variable-service-init-test efi-variable-service-4k-test,\
	$(eval $(test)-srcs += tests/drivers/efi-variable-service-init.c \
		src/drivers/efi/efivars.c src/drivers/efi/option.c \
		src/drivers/efi_variable_service/private_store.c \
		src/lib/crc_byte.c src/commonlib/region.c tests/stubs/console.c)\
	$(eval $(test)-stage := smm)\
	$(eval $(test)-config += CONFIG_ASS=1 CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=0 \
		CONFIG_ROM_SIZE=0x80000 CONFIG_USE_UEFI_VARIABLE_STORE=1 \
		CONFIG_OPTION_BACKEND_NONE=0)\
	$(eval $(test)-cflags += -I tests/drivers/efi-variable-service-include \
		-I src/soc/intel/common/block/include -I src/soc/intel/alderlake/include \
		-I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include \
		-I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32 \
		-I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include))
efi-variable-service-init-test-config += CONFIG_SMMSTORE_BLOCK_SIZE=0x10000
efi-variable-service-4k-test-config += CONFIG_SMMSTORE_BLOCK_SIZE=0x1000

tests-y += efi-variable-service-ipl-test efi-variable-service-ipl64-test
$(foreach test,efi-variable-service-ipl-test efi-variable-service-ipl64-test,\
	$(eval $(test)-srcs += tests/drivers/efi-variable-service-ipl.c)\
	$(eval $(test)-stage := ramstage))
efi-variable-service-ipl-test-config += CONFIG_ARCH_SMM_X86_64=0
efi-variable-service-ipl64-test-config += CONFIG_ARCH_SMM_X86_64=1

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
tests-y += efi-variable-service-vboot-test
efi-variable-service-vboot-test-config += CONFIG_VBOOT_SLOTS_RW_AB=1 \
	CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=1
efi-variable-service-vboot-test-srcs += src/drivers/efi_variable_service/capsule_store.c \
	3rdparty/vboot/firmware/2lib/2nvstorage.c 3rdparty/vboot/firmware/2lib/2crc8.c
efi-variable-service-vboot-test-cflags += -I 3rdparty/vboot/firmware/2lib/include
$(call copy-test,efi-variable-service-init-test,efi-variable-service-init64-test)
$(call copy-test,efi-variable-service-4k-test,efi-variable-service-4k64-test)
$(call copy-test,efi-variable-service-vboot-test,efi-variable-service-vboot64-test)
tests-y += efi-variable-service-init64-test efi-variable-service-4k64-test \
	efi-variable-service-vboot64-test
$(foreach test,efi-variable-service-init64-test efi-variable-service-4k64-test \
	efi-variable-service-vboot64-test,\
	$(eval $(test)-config += CONFIG_ARCH_SMM_X86_32=0 CONFIG_ARCH_SMM_X86_64=1)\
	$(eval $(test)-cflags := $(subst /Include/Ia32,/Include/X64,$($(test)-cflags))))
intel-fast-spi-resource-test-stage := ramstage
intel-fast-spi-resource-test-srcs += tests/drivers/intel-fast-spi-resource-test.c \
	src/device/device_util.c tests/stubs/console.c
intel-fast-spi-resource-test-cflags += -I src/soc/intel/common/block/include \
	-I src/soc/intel/alderlake/include -I src/soc/intel/common/block/fast_spi

tests-y += intel-fast-spi-resource-test

tests-y += opal-s3-resources-test
opal-s3-resources-test-srcs += tests/drivers/opal-s3-resources.c src/commonlib/region.c \
	tests/stubs/console.c
opal-s3-resources-test-stage := ramstage
opal-s3-resources-test-config += CONFIG_TCG_OPAL_S3_UNLOCK=1 CONFIG_HAVE_ACPI_RESUME=1 \
	CONFIG_SMM_PCI_RESOURCE_STORE=1 CONFIG_SMM_PCI_RESOURCE_STORE_NUM_SLOTS=2
opal-s3-resources-test-cflags += -Wno-address-of-packed-member

tests-y += efi-variable-service-entry-amd-test efi-variable-service-entry-intel-test
$(foreach test,efi-variable-service-entry-amd-test efi-variable-service-entry-intel-test,\
	$(eval $(test)-srcs += tests/drivers/efi-variable-service-entry.c)\
	$(eval $(test)-stage := smm)\
	$(eval $(test)-config += CONFIG_ARCH_SMM_X86_64=1))
efi-variable-service-entry-amd-test-config += CONFIG_SOC_AMD_COMMON=1
efi-variable-service-entry-intel-test-config += CONFIG_SOC_AMD_COMMON=0

tests-y += efi-variable-service-hob-test
efi-variable-service-hob-test-srcs += tests/drivers/efi-variable-service-hob.c \
	src/lib/uuid.c src/lib/hexstrtobin.c src/commonlib/region.c tests/stubs/console.c
efi-variable-service-hob-test-stage := ramstage
efi-variable-service-hob-test-config += CONFIG_ASS=1 CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=0 \
	CONFIG_ROM_SIZE=0x80000 CONFIG_SMMSTORE_BLOCK_SIZE=0x10000 \
	CONFIG_DRIVERS_EFI_MAIN_FW_GUID=\"12345678-1234-5678-9abc-def012345678\"
efi-variable-service-hob-test-cflags += -I tests/drivers/efi-variable-service-include \
	-I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include \
	-I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/X64 \
	-DFMAP_SECTION_FLASH_START=0 -DFMAP_SECTION_FLASH_SIZE=0x80000

$(call copy-test,efi-variable-service-hob-test,efi-variable-service-hob-capsule-test)
tests-y += efi-variable-service-hob-capsule-test
efi-variable-service-hob-capsule-test-config += CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=1
