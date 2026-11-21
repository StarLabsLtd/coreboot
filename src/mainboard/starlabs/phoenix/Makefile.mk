## SPDX-License-Identifier: GPL-2.0-or-later

CPPFLAGS_common += -I$(src)/mainboard/$(MAINBOARDDIR)/include
subdirs-y += variants/$(VARIANT_DIR)

bootblock-y += bootblock.c

romstage-y += romstage.c

ramstage-y += mainboard.c
ramstage-y += apcb.c
ramstage-$(CONFIG_DRIVERS_OPTION_CFR) += cfr.c

smm-y += smihandler.c
smm-y += power_budget_smm.c

ifeq ($(CONFIG_ADD_APCB_SOURCES),y)
APCB_SOURCES = $(call strip_quotes, $(CONFIG_APCB_SOURCES_PATH))/APCB_FP7.bin
APCB_SOURCES_RECOVERY = $(call strip_quotes, $(CONFIG_APCB_SOURCES_PATH))/APCB_FP7_DefaultRecovery.bin
APCB_SOURCES_68 = $(call strip_quotes, $(CONFIG_APCB_SOURCES_PATH))/APCB_FP7_Updatable.bin
endif

cbfs-files-y += apcb-factory
apcb-factory-file := $(APCB_SOURCES)
apcb-factory-type := raw

regions-for-file-apu/amdfw := AMDFW

# Authenticate writable primary APCBs using the existing directory parser.
OPT_PSP_APCB_WRITABLE = --apcb-writable
ramstage-y += ../../../vendorcode/amd/psp/psp_fw_parser.c
ramstage-y += ../../../vendorcode/amd/psp/psp_fw_bhd.c
ramstage-y += ../../../vendorcode/amd/psp/psp_fw_psp.c
ramstage-y += ../../../vendorcode/amd/psp/psp_fw_efs.c
ramstage-y += ../../../vendorcode/amd/psp/psp_fw_cmb.c
ramstage-y += ../../../vendorcode/amd/psp/psp_fw_ish.c
ramstage-y += ../../../vendorcode/amd/psp/psp_fw_util.c
