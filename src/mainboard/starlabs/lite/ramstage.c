<<<<<<< HEAD
/* SPDX-License-Identifier: GPL-2.0-only */

#include <baseboard/variants.h>
#include <device/device.h>
#include <soc/ramstage.h>
#include <option.h>
#include "variant/gpio.h"

void mainboard_silicon_init_params(FSPS_UPD *supd)
{
	/*
	 * Configure pads prior to SiliconInit() in case there's any
	 * dependencies during hardware initialization.
	 */
=======

/* SPDX-License-Identifier: GPL-2.0-only */

#include <baseboard/variants.h>
#include <soc/ramstage.h>
#include <gpio.h>

void mainboard_silicon_init_params(FSPS_UPD *supd)
{
        /* 
         * Configure pads prior to SiliconInit() in case there's any
         * dependencies during hardware initialization.
         */
>>>>>>> 0aad105d98... Rebase
	const struct pad_config *pads;
	size_t num;

	pads = variant_gpio_table(&num);
	gpio_configure_pads(pads, num);
}
