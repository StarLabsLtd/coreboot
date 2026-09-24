/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <gpio.h>
#include <intelblocks/gpio.h>
#include <intelblocks/p2sb.h>
#include <types.h>

/*
 * GPP_B13 drives PLTRST# as native function 1. PADCFGLOCK alone is host-removable;
 * mask host access to GPIOCOM5 after locking the pad.
 */
static const struct gpio_lock_config pltrst_pad[] = {
	{ .pad = GPP_B13, .lock_action = GPIO_LOCK_CONFIG,
	  .expected_mode = PAD_CFG0_MODE_NF1 },
};

const struct gpio_lock_config *soc_gpio_lock_config(size_t *num)
{
	*num = ARRAY_SIZE(pltrst_pad);
	return pltrst_pad;
}

int soc_gpio_lock_finalize(void)
{
	int ret;

	if (!p2sb_unhide()) {
		printk(BIOS_ERR, "P2SB: failed to unhide for GPIO lockdown\n");
		return -1;
	}

	ret = p2sb_disable_sideband_access();
	if (!p2sb_hide()) {
		printk(BIOS_ERR, "P2SB: failed to hide after GPIO lockdown\n");
		ret = -1;
	}

	return ret;
}
