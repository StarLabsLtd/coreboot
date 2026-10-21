## SPDX-License-Identifier: GPL-2.0-only

subdirs-$(CONFIG_HAVE_SPD_IN_CBFS) += ./spd
subdirs-y += variants/$(VARIANT_DIR)

smm-$(CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS) += variants/$(VARIANT_DIR)/gpio.c

ramstage-$(CONFIG_DRIVERS_OPTION_CFR) += cfr.c
ramstage-y += mainboard.c
