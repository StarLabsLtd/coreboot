/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/device.h>
#include <devtree_update.h>
#include <gpio.h>
#include <option.h>
#include <soc/gpio.h>
#include <static.h>

void mb_devtree_update(void)
{
	const unsigned int wifi_enabled = get_uint_option("wifi", 1);
	const unsigned int bluetooth_enabled = get_uint_option("bluetooth", 1);

	/* Enable/Disable WiFi based on CMOS settings */
	gpio_set(GPIO_91, wifi_enabled);
	if (!wifi_enabled)
		DEV_PTR(gpp_bridge_3)->enabled = 0;

	/* Enable/Disable Bluetooth based on CMOS settings */
	gpio_set(GPIO_69, bluetooth_enabled);
	if (!bluetooth_enabled)
		DEV_PTR(usb2_port4)->enabled = 0;

	/*
	 * Webcam, fingerprint reader, and daughterboard card reader are behind
	 * EC-controlled or shared power rails on this board, so they need
	 * explicit EC support rather than a plain devicetree toggle.
	 */
}
