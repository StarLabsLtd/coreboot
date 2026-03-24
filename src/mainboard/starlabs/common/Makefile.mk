# SPDX-License-Identifier: GPL-2.0-only

subdirs-$(CONFIG_VENDOR_STARLABS) += cfr
subdirs-$(CONFIG_VENDOR_STARLABS) += hda
subdirs-$(CONFIG_BOARD_STARLABS_STARFIGHTER_SERIES) += touchpad
subdirs-$(CONFIG_VENDOR_STARLABS) += powercap
subdirs-$(CONFIG_VENDOR_STARLABS) += pin_mux
subdirs-$(CONFIG_VENDOR_STARLABS) += smbios

ramstage-$(CONFIG_STARLABS_NVME_POWER_SEQUENCE) += nvme_seq.c
bootblock-$(CONFIG_STARLABS_AB_COREBOOT) += ab_slot.c
romstage-$(CONFIG_STARLABS_AB_COREBOOT) += ab_slot.c
postcar-$(CONFIG_STARLABS_AB_COREBOOT) += ab_slot.c
ramstage-$(CONFIG_STARLABS_AB_COREBOOT) += ab_slot.c

CPPFLAGS_common += -I$(src)/mainboard/starlabs/common/include

ramstage-$(CONFIG_STARLABS_ACPI_EFI_OPTION_SMI) += gnvs.c
smm-$(CONFIG_STARLABS_ACPI_EFI_OPTION_SMI) += smihandler.c

ifeq ($(CONFIG_STARLABS_AB_COREBOOT),y)
$(call add_intermediate, mirror_coreboot_slot_b, $(CBFSTOOL))

mirror_coreboot_slot_b:
	@printf "    MIRROR     COREBOOT -> COREBOOT_B\n"
	dd if=$(obj)/coreboot.pre of=$(obj)/coreboot_slot_a.bin bs=4096 \
		skip=$$(($$(printf '%d' 0x$$(sed -n 's/^#define FMAP_SECTION_COREBOOT_START 0x//p' $(obj)/fmap_config.h)) / 4096)) \
		count=$$(($$(printf '%d' 0x$$(sed -n 's/^#define FMAP_SECTION_COREBOOT_SIZE 0x//p' $(obj)/fmap_config.h)) / 4096)) \
		status=none
	dd if=$(obj)/coreboot_slot_a.bin of=$(obj)/coreboot.pre bs=4096 \
		seek=$$(($$(printf '%d' 0x$$(sed -n 's/^#define FMAP_SECTION_COREBOOT_B_START 0x//p' $(obj)/fmap_config.h)) / 4096)) \
		conv=notrunc status=none
	rm -f $(obj)/coreboot_slot_a.bin
endif
