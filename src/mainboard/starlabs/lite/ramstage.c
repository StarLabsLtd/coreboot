/* SPDX-License-Identifier: GPL-2.0-only */

#include <baseboard/variants.h>
#include <device/device.h>
#include <option.h>
#include <soc/ramstage.h>

static void init_mainboard(void *chip_info)
{
	const struct pad_config *pads;
	size_t num;

	pads = variant_gpio_table(&num);
	gpio_configure_pads(pads, num);

	devtree_update();
}

struct chip_operations mainboard_ops = {
	.init = init_mainboard,
};
