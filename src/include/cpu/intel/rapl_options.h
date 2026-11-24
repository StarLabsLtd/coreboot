/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_INTEL_RAPL_OPTIONS_H
#define CPU_INTEL_RAPL_OPTIONS_H

#include <drivers/option/cfr_frontend.h>
#include <option.h>

enum rapl_limit {
	RAPL_PL1,
	RAPL_PL2,
};

static inline struct option_constraints rapl_default_range(enum rapl_limit limit);

/* Return the numeric object used in the board's CFR menu. */
const struct sm_obj_number *mainboard_cfr_rapl_option(enum rapl_limit limit);

#if ENV_RAMSTAGE && CONFIG(DRIVERS_OPTION_CFR)
struct option_constraints get_rapl_override_range(enum rapl_limit limit);
#else
static inline struct option_constraints get_rapl_override_range(enum rapl_limit limit)
{
	return rapl_default_range(limit);
}
#endif

static inline struct option_constraints rapl_pl1_range(void)
{
	return get_rapl_override_range(RAPL_PL1);
}

static inline struct option_constraints rapl_pl2_range(void)
{
	return get_rapl_override_range(RAPL_PL2);
}

static inline struct option_constraints rapl_default_range(enum rapl_limit limit)
{
	if (CONFIG(CPU_INTEL_HASWELL))
		return limit == RAPL_PL1 ? OPTION_RANGE(15, 25) : OPTION_RANGE(15, 30);

	return limit == RAPL_PL1 ? OPTION_RANGE(0, 400) : OPTION_RANGE(0, 500);
}

#endif /* CPU_INTEL_RAPL_OPTIONS_H */
