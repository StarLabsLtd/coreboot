/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef INTELBLOCKS_8254_H
#define INTELBLOCKS_8254_H

#include <console/console.h>
#include <fsp/api.h>
#include <option.h>

static inline void fsp_configure_8254_clock_gating(FSP_S_CONFIG *params)
{
	const bool use_8254 = get_uint_option_checked("legacy_8254_timer",
						    CONFIG(USE_LEGACY_8254_TIMER), OPTION_BOOL);

	params->Enable8254ClockGating = !use_8254;
	params->Enable8254ClockGatingOnS3 = !use_8254;
}

static inline bool fsp_check_s0ix_clock_gating(const FSP_S_CONFIG *params, bool enable)
{
	if (enable && params->Enable8254ClockGating != 1) {
		printk(BIOS_WARNING, "S0ix disabled: SLP_S0 requires 8254 clock gating.\n");
		return false;
	}

	return enable;
}

#endif
