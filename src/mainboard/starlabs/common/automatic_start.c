/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <ec/starlabs/merlin/ec.h>
#include <intelblocks/pmclib.h>
#include <option.h>

unsigned int mainboard_get_power_failure_state(void)
{
	const unsigned int state =
		get_uint_option("automatic_start", AUTOMATIC_START_DEFAULT);

	switch (state) {
	case AUTOMATIC_START_ALWAYS:
	case AUTOMATIC_START_AFTER_FAILURE:
		return MAINBOARD_POWER_STATE_ON;
	case AUTOMATIC_START_NEVER:
		return MAINBOARD_POWER_STATE_OFF;
	default:
		printk(BIOS_WARNING, "Unknown automatic-start state: %u\n", state);
		if (AUTOMATIC_START_DEFAULT == AUTOMATIC_START_NEVER)
			return MAINBOARD_POWER_STATE_OFF;
		return MAINBOARD_POWER_STATE_ON;
	}
}
