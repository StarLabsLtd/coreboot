/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/intel/rapl_options.h>

const struct sm_obj_number *__weak mainboard_cfr_rapl_option(enum rapl_limit limit)
{
	return NULL;
}

struct option_constraints get_rapl_override_range(enum rapl_limit limit)
{
	const struct sm_obj_number *option = mainboard_cfr_rapl_option(limit);

	if (!option)
		return rapl_default_range(limit);

	/* Zero requests automatic limits even when the menu minimum is higher. */
	return option_range_step_or_zero(option->min, option->max, option->step);
}
