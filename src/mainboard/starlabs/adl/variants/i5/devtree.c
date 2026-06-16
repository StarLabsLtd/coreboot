/* SPDX-License-Identifier: GPL-2.0-only */

#include <chip.h>
#include <cpu/intel/turbo.h>
#include <device/device.h>
#include <device/i2c_bus.h>
#include <device/i2c_simple.h>
#include <devtree_update.h>
#include <option.h>
#include <static.h>
#include <types.h>
#include <variants.h>
#include <common/powercap.h>

static bool has_dedicated_card_reader(void)
{
	struct device *mxc_accel = DEV_PTR(mxc6655);

	return i2c_dev_detect(i2c_busdev(mxc_accel), mxc_accel->path.i2c.device);
}

void mb_devtree_update(void)
{
	config_t *cfg = config_of_soc();
	bool dedicated_card_reader;

	update_power_limits(cfg);

	/* Enable/Disable WiFi based on CMOS settings */
	if (get_uint_option("wifi", 1) == 0)
		DEV_PTR(cnvi_wifi)->enabled = 0;

	/* Enable/Disable Bluetooth based on CMOS settings */
	if (get_uint_option("bluetooth", 1) == 0)
		cfg->usb2_ports[9].enable = 0;

	/* Enable/Disable Webcam/Camera based on CMOS settings */
	if (get_uint_option("webcam", 1) == 0)
		cfg->usb2_ports[CONFIG_CCD_PORT].enable = 0;

	/* Enable/Disable Touchscreen based on CMOS settings */
	if (get_uint_option("touchscreen", 1) == 0)
		DEV_PTR(i2c2)->enabled = 0;

	/* Enable/Disable Accelerometer based on CMOS settings */
	if (get_uint_option("accelerometer", 1) == 0)
		DEV_PTR(i2c0)->enabled = 0;

	/* Enable/Disable GNA based on CMOS settings */
	if (get_uint_option("gna", 0) == 0)
		DEV_PTR(gna)->enabled = 0;

	dedicated_card_reader = has_dedicated_card_reader();

	/* Hide the KIOX000A-variant hub card reader on the MXC6655 variant */
	if (dedicated_card_reader)
		DEV_PTR(hub_card_reader)->enabled = 0;

	/* Enable/Disable the dedicated MXC6655-variant Card Reader based on CMOS Settings */
	if (!dedicated_card_reader || get_uint_option("card_reader", 0) == 0)
		cfg->usb2_ports[3].enable = 0;
}
