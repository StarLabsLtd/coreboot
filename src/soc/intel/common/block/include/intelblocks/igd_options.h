/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_COMMON_BLOCK_IGD_OPTIONS_H
#define SOC_INTEL_COMMON_BLOCK_IGD_OPTIONS_H

#include <option.h>

/*
 * FSP IgdDvmt50PreAlloc uses 32 MiB steps starting at zero, and a second
 * range of 4 MiB steps (4-60 MiB) starting at 0xf0.
 */
static inline unsigned int get_igd_dvmt_option(unsigned int fallback, unsigned int max)
{
	const unsigned int value = get_uint_option("igd_dvmt_prealloc", fallback);

	if (value <= max || (value >= 0xf0 && value <= 0xfe))
		return value;

	return fallback;
}

#endif /* SOC_INTEL_COMMON_BLOCK_IGD_OPTIONS_H */
