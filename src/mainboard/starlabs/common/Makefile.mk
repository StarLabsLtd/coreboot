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
