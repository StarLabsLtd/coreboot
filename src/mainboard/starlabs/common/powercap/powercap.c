/* SPDX-License-Identifier: GPL-2.0-only */

#include <ec/acpi/ec.h>
#include <option.h>
#include <soc/soc_chip.h>

#include "ecdefs.h"
#include "powercap.h"


static enum cmos_power_profile get_power_profile(enum cmos_power_profile fallback)
{
	const unsigned int power_profile = get_uint_option("power_profile", fallback);
	return power_profile < NUM_POWER_PROFILES ? power_profile : fallback;
}


void update_power_limits(config_t *cfg)
{
	uint8_t performance_scale = 100;

	uint16_t battery_design_capacity = (ec_read(ECRAM_BATTERY_DESIGN_CAPACITY) << 8) | ec_read(ECRAM_BATTERY_DESIGN_CAPACITY);
	uint16_t battery_design_voltage = (ec_read(ECRAM_BATTERY_DESIGN_VOLTAGE) << 8) | ec_read(ECRAM_BATTERY_DESIGN_VOLTAGE);
	uint32_t battery_design_wattage =
		((uint32_t)battery_design_voltage * (uint32_t)battery_design_capacity) / 1000;

	/* Scale PL1 & PL2 based on CMOS settings */
	switch (get_power_profile(PP_POWER_SAVER)) {
	case PP_POWER_SAVER:
		performance_scale -= 50;
		cfg->tcc_offset = TCC(80);
		break;
	case PP_BALANCED:
		performance_scale -= 25;
		cfg->tcc_offset = TCC(90);
		break;
	case PP_PERFORMANCE:
		/* Use the Intel defaults */
		cfg->tcc_offset = TCC(100);
		break;
	}

	for (size_t i = 0; i < ARRAY_SIZE(cfg->power_limits_config); i++) {
		struct soc_power_limits_config *limits = &cfg->power_limits_config[i];

		limits->tdp_pl4 = (uint16_t)battery_design_wattage;

		if (!limits->tdp_pl2_override)
			continue;

		/* Set PL1 to 50% of PL2 */
		limits->tdp_pl1_override = (limits->tdp_pl2_override / 2) & ~1;

		if (performance_scale == 100)
			continue;

		limits->tdp_pl1_override = ((limits->tdp_pl1_override * performance_scale) / 100);
		limits->tdp_pl2_override = ((limits->tdp_pl2_override * performance_scale) / 100);
	}
}
