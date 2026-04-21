# SPDX-License-Identifier: GPL-2.0-or-later

bootblock-y += bootblock.c

romstage-y += port_descriptors.c
romstage-y += romstage.c

ramstage-y += mainboard.c
ramstage-y += port_descriptors.c

subdirs-y += variants/baseboard
subdirs-y += variants/$(VARIANT_DIR)

CPPFLAGS_common += -I$(src)/mainboard/$(MAINBOARDDIR)/variants/baseboard/include
CPPFLAGS_common += -I$(src)/mainboard/$(MAINBOARDDIR)/variants/$(VARIANT_DIR)/include

ifneq ($(wildcard $(MAINBOARD_BLOBS_DIR)/APCB_FP7_Updatable.bin),)
APCB_SOURCES = $(MAINBOARD_BLOBS_DIR)/APCB_FP7_Updatable.bin
endif

ifneq ($(wildcard $(MAINBOARD_BLOBS_DIR)/APCB_FP7_DefaultRecovery.bin),)
APCB_SOURCES_RECOVERY = $(MAINBOARD_BLOBS_DIR)/APCB_FP7_DefaultRecovery.bin
endif

ifneq ($(wildcard $(MAINBOARD_BLOBS_DIR)/APCB_FP7_Updatable_68.bin),)
APCB_SOURCES_68 = $(MAINBOARD_BLOBS_DIR)/APCB_FP7_Updatable_68.bin
endif
