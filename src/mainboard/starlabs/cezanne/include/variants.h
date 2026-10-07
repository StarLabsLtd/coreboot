/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _BASEBOARD_VARIANTS_H_
#define _BASEBOARD_VARIANTS_H_

#include <soc/gpio.h>
#include <soc/platform_descriptors.h>

enum {
	STARLABS_CFR_ASPM_DISABLE = 1,
	STARLABS_CFR_ASPM_L0S,
	STARLABS_CFR_ASPM_L1,
	STARLABS_CFR_ASPM_L0S_L1,
	STARLABS_CFR_ASPM_AUTO,
};

enum {
	STARLABS_CFR_L1SS_DISABLED = 1,
	STARLABS_CFR_L1SS_L1_1,
	STARLABS_CFR_L1SS_L1_2,
};

void mainboard_update_dxio_power_management(dxio_descriptor *wifi, enum cpm_clk_req wifi_clk,
					  dxio_descriptor *ssd, enum cpm_clk_req ssd_clk);

enum cmos_power_profile {
	PP_POWER_SAVER = 0,
	PP_BALANCED    = 1,
	PP_PERFORMANCE = 2,
};
#define NUM_POWER_PROFILES 3

enum cmos_power_profile get_power_profile(enum cmos_power_profile fallback);

/*
 * The next set of functions return the gpio table and fill in the number of
 * entries for each table.
 */
const struct soc_amd_gpio *variant_early_gpio_table(size_t *num);
const struct soc_amd_gpio *variant_bootblock_gpio_table(size_t *num);
const struct soc_amd_gpio *variant_gpio_table(size_t *num);

#endif /* _BASEBOARD_VARIANTS_H_ */
