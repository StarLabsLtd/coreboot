/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_COMMON_BLOCK_CFG_H
#define SOC_INTEL_COMMON_BLOCK_CFG_H

#include <boot/coreboot_tables.h>
#include <bootsplash.h>
#include <intelblocks/gspi.h>
#include <drivers/i2c/designware/dw_i2c.h>
#include <intelblocks/mmc.h>
#include <types.h>

enum {
	CHIPSET_LOCKDOWN_COREBOOT = 0, /* coreboot handles locking */
	CHIPSET_LOCKDOWN_FSP,          /* FSP handles locking per UPDs */
	/*
	 * FSP handles platform lockdown, while coreboot applies final
	 * BIOS lock settings after SMM setup.
	 */
	CHIPSET_LOCKDOWN_FSP_THEN_COREBOOT_BIOS_LOCK,
};

static inline bool chipset_lockdown_uses_fsp_platform_lockdown(int chipset_lockdown)
{
	return chipset_lockdown == CHIPSET_LOCKDOWN_FSP ||
	       chipset_lockdown == CHIPSET_LOCKDOWN_FSP_THEN_COREBOOT_BIOS_LOCK;
}

static inline bool chipset_lockdown_uses_fsp_bios_lock(int chipset_lockdown)
{
	return chipset_lockdown == CHIPSET_LOCKDOWN_FSP;
}

static inline bool chipset_lockdown_uses_late_coreboot_bios_lock(int chipset_lockdown)
{
	return chipset_lockdown == CHIPSET_LOCKDOWN_FSP_THEN_COREBOOT_BIOS_LOCK;
}

/*
 * This structure will hold data required by common blocks.
 * These are soc specific configurations which will be filled by soc.
 * We'll fill this structure once during init and use the data in common block.
 */
struct soc_intel_common_config {
	int chipset_lockdown;
	struct gspi_cfg gspi[CONFIG_SOC_INTEL_COMMON_BLOCK_GSPI_MAX];
	struct dw_i2c_bus_config i2c[CONFIG_SOC_INTEL_I2C_DEV_MAX];
	/* PCH Thermal Trip Temperature in deg C */
	uint8_t pch_thermal_trip;
	struct mmc_dll_params emmc_dll;
	enum lb_fb_orientation panel_orientation;
	enum fw_splash_vertical_alignment logo_valignment;
	/* Implies spacing from an edge for rendering footer logo */
	uint8_t logo_bottom_margin;
};

/* This function to retrieve soc config structure required by common code */
struct soc_intel_common_config *chip_get_common_soc_structure(void);

#endif /* SOC_INTEL_COMMON_BLOCK_CFG_H */
