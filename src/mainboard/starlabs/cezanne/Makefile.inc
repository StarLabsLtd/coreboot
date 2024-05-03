## SPDX-License-Identifier: GPL-2.0-only

CPPFLAGS_common += -I$(src)/mainboard/$(MAINBOARDDIR)/include
subdirs-y += variants/$(VARIANT_DIR)

bootblock-y += bootblock.c

ramstage-y += mainboard.c
ramstage-y += smbios.c
ramstage-y += hda_verb.c

romstage-y += romstage.c

ifeq ($(CONFIG_ADD_APCB_SOURCES),y)
APCB_SOURCES = $(call strip_quotes, $(CONFIG_APCB_SOURCES_PATH))/APCB_CZN_D4_Updatable.bin
APCB_SOURCES_68 = $(call strip_quotes,$(CONFIG_APCB_SOURCES_PATH))/APCB_CZN_D4_Updatable_68.bin
APCB_SOURCES_RECOVERY = $(call strip_quotes,$(CONFIG_APCB_SOURCES_PATH))/APCB_CZN_D4_DefaultRecovery.bin
endif
