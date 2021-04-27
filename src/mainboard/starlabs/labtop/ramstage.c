
/* SPDX-License-Identifier: GPL-2.0-only */

#include <baseboard/variants.h>
#include <soc/ramstage.h>
#include <option.h>
#include "variant/gpio.h"


#if CONFIG(BOARD_STARLABS_LABTOP_CML)
void mainboard_silicon_init_params(FSPS_UPD *supd)
#else
void mainboard_silicon_init_params(FSP_S_CONFIG *params)
#endif
{
	/*
	 * Configure pads prior to SiliconInit() in case there's any
	 * dependencies during hardware initialization.
	 */
	const struct pad_config *pads;
	size_t num;

	pads = variant_gpio_table(&num);
	gpio_configure_pads(pads, num);
}

static void mainboard_enable(struct device *dev)
{
	u8 me_state = get_int_option("me_state", 0xff);
	printk(BIOS_DEBUG, "XXXXX me_state: %d\n", me_state);
}

struct chip_operations mainboard_ops = {
        .enable_dev = mainboard_enable,
};
