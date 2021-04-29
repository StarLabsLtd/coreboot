
/* SPDX-License-Identifier: GPL-2.0-only */

#include <baseboard/variants.h>
#include <device/device.h>
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
	struct device *csedev = pcidev_path_on_root(PCH_DEVFN_CSE);
	printk(BIOS_DEBUG, "XXXXX PCI device state before: %d\n", csedev->enabled);
	if (csedev->enabled) {
		u8 me_state = get_int_option("me_state", 0xff);
		printk(BIOS_DEBUG, "XXXXX CMOS me_state: %d\n", me_state);
		if (me_state == 1) {
			printk(BIOS_DEBUG, "Flagging that SMM should disable the ME\n");
			csedev->enabled = 0;
			printk(BIOS_DEBUG, "XXXXX PCI device state after: %d\n", csedev->enabled);
		}
	}
}
