## SPDX-License-Identifier: GPL-2.0-only

subdirs-y += variants/$(VARIANT_DIR)

smm-$(CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS) += variants/$(VARIANT_DIR)/gpio.c

bootblock-y += bootblock.c

ramstage-$(CONFIG_DRIVERS_OPTION_CFR) += cfr.c
ramstage-y += devtree.c
ramstage-y += mainboard.c
