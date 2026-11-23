/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_INTEL_RAPL_OPTIONS_H
#define CPU_INTEL_RAPL_OPTIONS_H

#include <option.h>

#if CONFIG(CPU_INTEL_RAPL_BOARD_OPTIONS)
#include <mainboard/rapl_options.h>
#endif

static inline unsigned int get_rapl_pl1_override(unsigned int fallback)
{
	unsigned int watts = get_uint_option("tdp_pl1_override", fallback);

#if CONFIG(CPU_INTEL_RAPL_BOARD_OPTIONS)
	if (watts && !option_value_valid(watts, OPTION_RANGE_STEP(RAPL_PL1_MIN, RAPL_PL1_MAX,
								 RAPL_PL1_STEP)))
		return fallback;
#endif
	return watts;
}

static inline unsigned int get_rapl_pl2_override(unsigned int fallback)
{
	unsigned int watts = get_uint_option("tdp_pl2_override", fallback);

#if CONFIG(CPU_INTEL_RAPL_BOARD_OPTIONS)
	if (watts && !option_value_valid(watts, OPTION_RANGE_STEP(RAPL_PL2_MIN, RAPL_PL2_MAX,
								 RAPL_PL2_STEP)))
		return fallback;
#endif
	return watts;
}

static inline bool rapl_pl2_disabled(unsigned int watts)
{
#ifdef RAPL_PL2_UNLIMITED_W
	return watts == RAPL_PL2_UNLIMITED_W;
#else
	return false;
#endif
}

static inline unsigned int rapl_encode_watts(unsigned int watts, unsigned int unit,
					    unsigned int mask, unsigned int automatic)
{
	if (!watts || watts > mask / unit)
		return automatic;

	return watts * unit;
}

#endif /* CPU_INTEL_RAPL_OPTIONS_H */
