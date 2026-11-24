/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_INTEL_RAPL_OPTIONS_H
#define CPU_INTEL_RAPL_OPTIONS_H

#include <option.h>

enum rapl_limit {
	RAPL_PL1,
	RAPL_PL2,
};

static inline struct option_constraints rapl_default_range(enum rapl_limit limit);

static inline struct option_constraints get_rapl_override_range(enum rapl_limit limit)
{
	return rapl_default_range(limit);
}

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
