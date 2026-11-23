# SPDX-License-Identifier: GPL-2.0-only

CPPFLAGS_common += -I$(src)/mainboard/$(MAINBOARDDIR)/include

bootblock-$(CONFIG_BOARD_STARLABS_ADL_SERIES) += bootblock.c
bootblock-$(CONFIG_BOARD_STARLABS_STARFIGHTER_SERIES) += bootblock.c

ifneq ($(filter y,$(CONFIG_BOARD_STARLABS_LITE_SERIES) $(CONFIG_BOARD_STARLABS_STARBOOK_SERIES)),)
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
subdirs-$(CONFIG_STARLABS_TOUCHPAD) += touchpad
subdirs-$(CONFIG_VENDOR_STARLABS) += powercap
subdirs-$(CONFIG_VENDOR_STARLABS) += fsp_params
subdirs-$(CONFIG_VENDOR_STARLABS) += pin_mux
subdirs-$(CONFIG_VENDOR_STARLABS) += smbios

CPPFLAGS_common += -I$(src)/mainboard/starlabs/common/include

ramstage-$(CONFIG_STARLABS_ACPI_EFI_OPTION_SMI) += gnvs.c
ramstage-$(CONFIG_STARLABS_AUTOMATIC_START) += automatic_start.c
smm-$(CONFIG_STARLABS_AUTOMATIC_START) += automatic_start.c
smm-$(CONFIG_STARLABS_SMM_OPTION_HANDLER) += smihandler.c

ifeq ($(CONFIG_PAYLOAD_EDK2),y)
# FSP's native splash renderer has no display-density selection.
ifeq ($(CONFIG_BOOT_SPLASH_HANDOFF)$(CONFIG_USE_COREBOOT_FOR_BMP_RENDERING),)
ifeq ($(CONFIG_BMP_LOGO_SOURCE_SCALE),2)
starlabs-logo-resize := -filter box -resize 50%
endif
endif
$(obj)/starlabs-logo.bmp: $(call strip_quotes,$(CONFIG_EDK2_BOOTSPLASH_FILE)) \
		$(obj)/config.h $(src)/mainboard/starlabs/common/Makefile.mk
	@printf "    CONVERT    %s\n" "$@"
	@if test -n "$(starlabs-logo-resize)"; then \
		parity=$$(convert -ping "$<" -format '%[fx:w%2+h%2]' info:) || exit 1; \
		test "$$parity" = 0 || \
		{ printf '%s\n' 'Double-density boot logo requires even dimensions' >&2; exit 1; }; \
	fi
	convert -background None "$<" $(starlabs-logo-resize) -type TrueColor "BMP3:$@"
endif
smm-y += smi_apmc.c
