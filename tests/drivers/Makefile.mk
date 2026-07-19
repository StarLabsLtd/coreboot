# SPDX-License-Identifier: GPL-2.0-only

tests-y += efivars-test
tests-y += efi-fw-info-test
tests-y += efi-info-test

efi-fw-info-test-srcs += tests/drivers/efi_fw_info-test.c
efi-fw-info-test-srcs += src/drivers/efi/info.c
efi-fw-info-test-srcs += src/lib/hexstrtobin.c
efi-fw-info-test-srcs += src/lib/uuid.c
efi-fw-info-test-srcs += tests/stubs/console.c
efi-fw-info-test-config += CONFIG_DRIVERS_EFI_CAPSULE_POLICY=0 \
	CONFIG_DRIVERS_EFI_MAIN_FW_GUID=\"975cd0e6-c540-4e2b-906c-72c0d0d1e40d\" \
	CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=0x001a0009 \
	CONFIG_DRIVERS_EFI_MAIN_FW_LSV=0x001a0008 \
	CONFIG_LOCALVERSION=\"26.09\" CONFIG_ROM_SIZE=0x01000000

efi-info-test-srcs += tests/drivers/efi_info-test.c
efi-info-test-srcs += src/drivers/efi/info.c
efi-info-test-srcs += src/lib/hexstrtobin.c
efi-info-test-srcs += src/lib/uuid.c
efi-info-test-srcs += tests/stubs/console.c
efi-info-test-mocks += fmap_locate_area fmap_locate_area_with_flags
efi-info-test-mocks += fmap_region_overlaps_area_with_flags
efi-info-test-mocks += fmap_read_directory vb2_hash_calculate
efi-info-test-cflags += -I tests/include/tests/lib/fmap
efi-info-test-cflags += -I tests/include/tests/drivers/efi
efi-info-test-config += CONFIG_DRIVERS_EFI_CAPSULE_ACCEPT_EMBEDDED_DRIVERS=0 \
	CONFIG_DRIVERS_EFI_CAPSULE_EMBED_FMP_DXE=0 \
	CONFIG_DRIVERS_EFI_CAPSULE_POLICY=1 \
	CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT=\"root.pem\" \
	CONFIG_DRIVERS_EFI_CAPSULE_REGIONS=\"COREBOOT\" \
	CONFIG_DRIVERS_EFI_MAIN_FW_GUID=\"975cd0e6-c540-4e2b-906c-72c0d0d1e40d\" \
	CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=0x001a0009 \
	CONFIG_DRIVERS_EFI_MAIN_FW_LSV=0x001a0008 \
	CONFIG_LOCALVERSION=\"26.09\" CONFIG_ROM_SIZE=0x01000000 \
	CONFIG_MAINBOARD_VENDOR=\"StarLabs\" \
	CONFIG_MAINBOARD_PART_NUMBER=\"TestBoard\"

efivars-test-srcs += tests/drivers/efivars.c
efivars-test-srcs += src/drivers/efi/efivars.c
efivars-test-srcs += tests/stubs/console.c
efivars-test-srcs += src/commonlib/region.c

efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Pi/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include/
