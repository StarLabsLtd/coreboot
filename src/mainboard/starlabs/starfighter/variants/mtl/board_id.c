/* SPDX-License-Identifier: GPL-2.0-only */

#include <gpio.h>
#include <variants.h>

uint8_t get_memory_config_straps(void)
{
	const gpio_t spd_id[] = {
		GPP_B05,
		GPP_B06,
		GPP_B07,
		GPP_B08,
	};

	uint8_t strap = 0;

	/* The early GPIO table already configured these inputs. */
	for (size_t i = 0; i < ARRAY_SIZE(spd_id); i++)
		strap |= gpio_get(spd_id[i]) << i;

	return strap;
}
