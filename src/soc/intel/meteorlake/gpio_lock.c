/* SPDX-License-Identifier: GPL-2.0-only */

#include <gpio.h>
#include <intelblocks/gpio.h>
#include <types.h>

/* GPP_B13 drives PLTRST# as native function 1. */
static const struct gpio_lock_config pltrst_pad[] = {
	{ .pad = GPP_B13, .lock_action = GPIO_LOCK_CONFIG,
	  .expected_mode = PAD_CFG0_MODE_NF1 },
};

const struct gpio_lock_config *soc_gpio_lock_config(size_t *num)
{
	*num = ARRAY_SIZE(pltrst_pad);
	return pltrst_pad;
}
