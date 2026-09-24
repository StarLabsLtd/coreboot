/* SPDX-License-Identifier: GPL-2.0-only */

#include <gpio.h>
#include <intelblocks/gpio.h>
#include <types.h>

/*
 * PMU_PLTRST_B (APL) / GPIO_98 (GLK) drives PLTRST#. Locking the pad
 * configuration keeps system software from switching it to GPIO mode and
 * resetting a discrete TPM without restarting the measured boot chain.
 */
static const struct gpio_lock_config pltrst_pad[] = {
#if CONFIG(SOC_INTEL_GEMINILAKE)
	{ .pad = GPIO_98, .lock_action = GPIO_LOCK_CONFIG,
	  .expected_mode = PAD_CFG0_MODE_NF1 },
#else
	{ .pad = PMU_PLTRST_B, .lock_action = GPIO_LOCK_CONFIG,
	  .expected_mode = PAD_CFG0_MODE_NF1 },
#endif
};

const struct gpio_lock_config *soc_gpio_lock_config(size_t *num)
{
	*num = ARRAY_SIZE(pltrst_pad);
	return pltrst_pad;
}
