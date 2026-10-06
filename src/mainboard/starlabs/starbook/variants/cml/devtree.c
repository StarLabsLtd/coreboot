/* SPDX-License-Identifier: GPL-2.0-only */

#include <chip.h>
#include <cpu/intel/turbo.h>
#include <device/device.h>
#include <devtree_update.h>
#include <option.h>
#include <static.h>
#include <types.h>
#include <variants.h>
#include <common/powercap.h>

void mb_devtree_update(void)
{
	config_t *cfg = config_of_soc();
	update_power_limits(cfg);

	/* Enable/Disable WiFi based on CMOS settings */
	if (get_uint_option_checked("wifi", 1, OPTION_BOOL) == 0)
		DEV_PTR(cnvi_wifi)->enabled = 0;

	/* Enable/Disable Bluetooth based on CMOS settings */
	if (get_uint_option_checked("bluetooth", 1, OPTION_BOOL) == 0)
		cfg->usb2_ports[9].enable = 0;

	/* Enable/Disable Webcam based on CMOS settings */
	cfg->usb2_ports[CONFIG_CCD_PORT].enable =
		get_uint_option_checked("webcam", 1, OPTION_BOOL);

	/* Enable/Disable Card Reader based on CMOS Settings */
	if (get_uint_option_checked("card_reader", 1, OPTION_BOOL) == 0)
		cfg->usb2_ports[3].enable = 0;
}
