## SPDX-License-Identifier: GPL-2.0-only

romstage-y += cache.c
ramstage-y += cache.c
smm-$(CONFIG_SOC_AMD_COMMON_BLOCK_PSP_ROM_ARMOR3) += cache.c
