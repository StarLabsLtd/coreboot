# SPDX-License-Identifier: GPL-2.0-only

ramstage-$(CONFIG_DRIVERS_OPTION_CFR) += cfr.c

ramstage-$(CONFIG_DRIVERS_OPTION_CFR_SMM) += cfr_settings_policy.c
ramstage-$(CONFIG_DRIVERS_OPTION_CFR_SMM) += cfr_settings_table.c
smm-$(CONFIG_DRIVERS_OPTION_CFR_SMM) += cfr_settings_policy.c
smm-$(CONFIG_DRIVERS_OPTION_CFR_SMM) += cfr_settings_smm.c

ifeq ($(CONFIG_COMPILER_GCC),y)
$(obj)/smm/drivers/option/cfr_settings_policy.o: CFLAGS_smm += \
	-fstack-usage -Wframe-larger-than=256 -Wstack-usage=256
$(obj)/smm/drivers/option/cfr_settings_smm.o: CFLAGS_smm += \
	-fstack-usage -Wframe-larger-than=256 -Wstack-usage=256
endif

all-$(CONFIG_USE_CBFS_FILE_OPTION_BACKEND) += cbfs_file_option.c
