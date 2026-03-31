/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _STARLABS_CMN_POWERCAP_H_
#define _STARLABS_CMN_POWERCAP_H_

#include <soc/soc_chip.h>

enum cmos_power_profile {
	PP_POWER_SAVER	= 0,
	PP_BALANCED	= 1,
	PP_PERFORMANCE	= 2,
	PP_CUSTOM	= 3,
};
#define NUM_POWER_PROFILES 4

struct starlabs_power_profile_bounds {
	uint32_t default_pl1;
	uint32_t min_pl1;
	uint32_t max_pl1;
	uint32_t default_pl2;
	uint32_t min_pl2;
	uint32_t max_pl2;
	uint32_t default_pl4;
	uint32_t min_pl4;
	uint32_t max_pl4;
	uint32_t default_tcc_temp;
	uint32_t min_tcc_temp;
	uint32_t max_tcc_temp;
};

bool starlabs_get_power_profile_bounds(const config_t *cfg,
	struct starlabs_power_profile_bounds *bounds);

void update_power_limits(config_t *cfg);

#endif
