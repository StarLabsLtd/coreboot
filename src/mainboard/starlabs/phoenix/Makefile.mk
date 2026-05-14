## SPDX-License-Identifier: GPL-2.0-or-later

CPPFLAGS_common += -I$(src)/mainboard/$(MAINBOARDDIR)/include
subdirs-y += variants/$(VARIANT_DIR)

bootblock-y += bootblock.c

romstage-y += romstage.c

ramstage-y += mainboard.c

ifeq ($(CONFIG_ADD_APCB_SOURCES),y)
APCB_SOURCES = $(call strip_quotes, $(CONFIG_APCB_SOURCES_PATH))/APCB_FP7.bin
APCB_SOURCES_RECOVERY = $(call strip_quotes, $(CONFIG_APCB_SOURCES_PATH))/APCB_FP7_DefaultRecovery.bin
APCB_SOURCES_68 = $(call strip_quotes, $(CONFIG_APCB_SOURCES_PATH))/APCB_FP7_Updatable.bin
endif

regions-for-file-apu/amdfw := AMDFW
