# SPDX-License-Identifier: GPL-2.0-only

CPPFLAGS_common += -I$(src)/mainboard/$(MAINBOARDDIR)/include

ifeq ($(CONFIG_STARLABS_ENHANCED_SECURITY),y)
ifneq ($(CONFIG_BOOTMEDIA_LOCK_CHIP),y)
$(error Enhanced-security builds require chip-level boot-media protection)
endif
ifneq ($(CONFIG_BOOTMEDIA_LOCK_WPRO_VBOOT_RO),y)
$(error Enhanced-security builds require WP_RO boot-media protection)
endif
ifneq ($(CONFIG_BOOTMEDIA_SPI_LOCK_PLATFORM),y)
$(error Enhanced-security builds require platform-enforced SPI status locking)
endif
ifeq ($(CONFIG_BOOTMEDIA_SMM_BWP_RUNTIME_OPTION),y)
$(error Enhanced-security builds cannot expose runtime BIOS write protection)
endif
endif

ifeq ($(CONFIG_STARLABS_ENHANCED_SECURITY),y)
starlabs_edk2_repository := $(subst ",,$(CONFIG_EDK2_REPOSITORY))
starlabs_edk2_revision := $(subst ",,$(CONFIG_EDK2_TAG_OR_REV))
starlabs_edk2_params := $(subst ",,$(CONFIG_EDK2_CUSTOM_BUILD_PARAMS))
starlabs_vboot_key_paths := $(subst ",,$(CONFIG_VBOOT_ROOT_KEY) \
	$(CONFIG_VBOOT_RECOVERY_KEY) $(CONFIG_VBOOT_FIRMWARE_PRIVKEY) \
	$(CONFIG_VBOOT_KERNEL_KEY) $(CONFIG_VBOOT_KEYBLOCK) \
	$(CONFIG_VBOOT_GSCVD_ROOT_PUBKEY) \
	$(CONFIG_VBOOT_GSCVD_PLATFORM_PRIVKEY) \
	$(CONFIG_VBOOT_GSCVD_PLATFORM_KEYBLOCK))
starlabs_allowed_edk2_defines := \
	BOOT_KEY_FACTORY_PROVISIONING=TRUE \
	SECURE_BOOT_SIMPLE_UI=TRUE \
	TPM_SIMPLE_UI=TRUE \
	TPM1_ENABLE=FALSE \
	OPAL_PASSWORD_ENABLE=TRUE \
	TCG_STORAGE_SIMPLE_UI=TRUE \
	PAYLOAD_FB_HIDPI_WIDE_ASPECT_CAP_SUPPORT=TRUE \
	PAYLOAD_FB_HIDPI_WIDE_ASPECT_CAP_WIDTH=3 \
	PAYLOAD_FB_HIDPI_WIDE_ASPECT_CAP_WIDTH=16 \
	PAYLOAD_FB_HIDPI_WIDE_ASPECT_CAP_HEIGHT=2 \
	PAYLOAD_FB_HIDPI_WIDE_ASPECT_CAP_HEIGHT=10 \
	CONNECT_ALL_DEVICES=TRUE \
	MEMORY_TEST=NULL \
	MEMORY_TYPE_INFORMATION_BIN_BASE=0x0c000000 \
	MEMORY_TYPE_INFORMATION_BIN_SIZE=0x00800000 \
	MEMORY_TYPE_EFI_ACPI_RECLAIM_MEMORY=0x1f4
starlabs_allowed_edk2_pcds := \
	gEfiMdeModulePkgTokenSpaceGuid.PcdFastPS2Detection=TRUE \
	gEfiMdeModulePkgTokenSpaceGuid.PcdPs2KbdExtendedVerification=FALSE

# Return "invalid" unless every option is a permitted marker/value pair.
starlabs_check_edk2_params = $(if $(strip $(1)), \
	$(if $(filter -D,$(firstword $(1))), \
		$(if $(filter $(starlabs_allowed_edk2_defines),$(word 2,$(1))), \
			$(call starlabs_check_edk2_params,$(wordlist 3,$(words $(1)),$(1))),invalid), \
		$(if $(filter --pcd,$(firstword $(1))), \
			$(if $(filter $(starlabs_allowed_edk2_pcds),$(word 2,$(1))), \
				$(call starlabs_check_edk2_params,$(wordlist 3,$(words $(1)),$(1))),invalid), \
			invalid)))
starlabs_edk2_invalid_params := $(call starlabs_check_edk2_params, \
	$(starlabs_edk2_params))

ifneq ($(CONFIG_PAYLOAD_EDK2),y)
$(error Enhanced-security builds require the EDK II payload)
endif
ifneq ($(CONFIG_EDK2_UEFIPAYLOAD),y)
$(error Enhanced-security builds require UefiPayloadPkg)
endif
ifneq ($(CONFIG_EDK2_REPO_CUSTOM),y)
$(error Enhanced-security builds require the Star Labs EDK II repository)
endif
ifneq ($(starlabs_edk2_repository),https://github.com/starlabsltd/edk2)
$(error Enhanced-security builds require https://github.com/starlabsltd/edk2)
endif
ifneq ($(starlabs_edk2_revision),origin/26.09)
$(error Enhanced-security builds require the origin/26.09 EDK II revision)
endif
ifneq ($(CONFIG_EDK2_RELEASE),y)
$(error Enhanced-security builds require a release EDK II payload)
endif
ifneq ($(CONFIG_EDK2_SECURE_BOOT_SUPPORT),y)
$(error Enhanced-security builds require EDK II Secure Boot support)
endif
ifneq ($(CONFIG_EDK2_SECURE_BOOT_DEFAULT_ENABLE),y)
$(error Enhanced-security builds require Secure Boot enabled by default)
endif
ifneq ($(CONFIG_SOC_INTEL_COMMON_BLOCK_RTC_LOCK_PROTECTED_MEMORY),y)
$(error Enhanced-security builds require protected RTC CMOS locking)
endif
ifeq ($(CONFIG_GBB_FLAG_DISABLE_FW_ROLLBACK_CHECK),y)
$(error Enhanced-security builds require vboot firmware rollback checking)
endif
ifneq ($(findstring /tests/devkeys,$(starlabs_vboot_key_paths)),)
$(error Enhanced-security builds cannot use vboot development keys)
endif
ifneq ($(filter y,$(CONFIG_EDK2_HAVE_EFI_SHELL) $(CONFIG_EDK2_SERIAL_SUPPORT) \
	$(CONFIG_EDK2_CBMEM_LOGGING) $(CONFIG_EDK2_LOAD_OPTION_ROMS) \
	$(CONFIG_EDK2_DISABLE_TPM) $(CONFIG_EDK2_ENABLE_IPXE) \
	$(CONFIG_EDK2_USE_EDK2_PLATFORMS) $(CONFIG_EDK2_GOP_DRIVER) \
	$(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES)),)
$(error Enhanced-security EDK II configuration enables a forbidden feature)
endif
ifneq ($(strip $(starlabs_edk2_invalid_params)),)
$(error Enhanced-security EDK II custom parameters are not allowlisted)
endif
endif

bootblock-$(CONFIG_BOARD_STARLABS_ADL_SERIES) += bootblock.c
bootblock-$(CONFIG_BOARD_STARLABS_STARFIGHTER_SERIES) += bootblock.c

ifneq ($(filter y,$(CONFIG_BOARD_STARLABS_LITE_SERIES) \
		$(CONFIG_BOARD_STARLABS_STARBOOK_SERIES) \
		$(CONFIG_BOARD_STARLABS_ADL_HORIZON) \
		$(CONFIG_BOARD_STARLABS_STARFIGHTER_MTL)),)
verstage-$(CONFIG_VBOOT) += vboot.c
romstage-$(CONFIG_VBOOT) += vboot.c
endif

ramstage-$(CONFIG_BOARD_STARLABS_STARBOOK_SERIES) += mainboard.c
ramstage-$(CONFIG_BOARD_STARLABS_STARFIGHTER_SERIES) += mainboard.c
ramstage-$(CONFIG_VENDOR_STARLABS) += fadt.c

ifneq ($(filter y,$(CONFIG_BOARD_STARLABS_LITE_SERIES) \
		$(CONFIG_BOARD_STARLABS_LABTOP_KBL) $(CONFIG_BOARD_STARLABS_LABTOP_CML)),)
ramstage-$(CONFIG_MAINBOARD_USE_LIBGFXINIT) += gma-mainboard.ads
endif

subdirs-$(CONFIG_VENDOR_STARLABS) += cfr
subdirs-$(CONFIG_VENDOR_STARLABS) += hda
subdirs-$(CONFIG_STARLABS_TOUCHPAD_RUNTIME) += touchpad
subdirs-$(CONFIG_VENDOR_STARLABS) += powercap
subdirs-$(CONFIG_VENDOR_STARLABS) += fsp_params
subdirs-$(CONFIG_VENDOR_STARLABS) += pin_mux
subdirs-$(CONFIG_VENDOR_STARLABS) += smbios

ramstage-$(CONFIG_STARLABS_NVME_POWER_SEQUENCE) += nvme_seq.c
ramstage-$(CONFIG_ENABLE_EARLY_DMA_PROTECTION) += dma.c
ramstage-$(CONFIG_ENABLE_EARLY_DMA_PROTECTION) += pci_root_bridge.c

CPPFLAGS_common += -I$(src)/mainboard/starlabs/common/include

ramstage-$(CONFIG_STARLABS_ACPI_EFI_OPTION_SMI) += gnvs.c
ramstage-$(CONFIG_STARLABS_AUTOMATIC_START) += automatic_start.c
smm-$(CONFIG_STARLABS_AUTOMATIC_START) += automatic_start.c
smm-$(CONFIG_STARLABS_SMM_OPTION_HANDLER) += smihandler.c
