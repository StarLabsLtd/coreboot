/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef POWERCAP
#define POWERCAP

#define TJ_MAX		110
#define TCC(temp)	(TJ_MAX - temp)

enum cmos_power_profile {
	PP_POWER_SAVER	= 0,
	PP_BALANCED	= 1,
	PP_PERFORMANCE	= 2,
};
#define NUM_POWER_PROFILES 3

void update_power_limits(config_t *cfg);

#endif
