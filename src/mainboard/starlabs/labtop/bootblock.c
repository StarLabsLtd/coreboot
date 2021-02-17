/* SPDX-License-Identifier: GPL-2.0-only */

#include <soc/gpio.h>
#include <console/console.h>
#include <baseboard/variants.h>
#include <bootblock_common.h>

void bootblock_mainboard_init(void)
{
	const struct pad_config *pads;
	size_t num;

	printk(BIOS_DEBUG, "bootblock_mainboard_init()\n");

	pads = variant_early_gpio_table(&num);
	gpio_configure_pads(pads, num);
}
