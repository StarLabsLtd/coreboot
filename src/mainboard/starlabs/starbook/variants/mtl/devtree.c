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
	if (!CONFIG(CONSOLE_SERIAL) && !CONFIG(CONSOLE_SERIAL_RUNTIME) &&
	    !CONFIG(CONSOLE_CBMEM_DUMP_TO_UART)) {
		DEV_PTR(uart0)->enabled = 0;
		cfg->serial_io_uart_mode[PchSerialIoIndexUART0] = PchSerialIoDisabled;
	}

	/* Enable/Disable Bluetooth based on CMOS settings */
	if (get_uint_option_checked("bluetooth", 1, OPTION_BOOL) == 0)
		cfg->usb2_ports[9].enable = 0;

	/* Enable/Disable Webcam based on CMOS settings */
	if (get_uint_option_checked("webcam", 1, OPTION_BOOL) == 0)
		cfg->usb2_ports[CONFIG_CCD_PORT].enable = 0;

	/* Enable/Disable Card Reader based on CMOS Settings */
	if (get_uint_option_checked("card_reader", 1, OPTION_BOOL) == 0)
		cfg->usb2_ports[7].enable = 0;

	/* Enable/Disable GNA based on CMOS settings */
	if (get_uint_option_checked("gna", 0, OPTION_BOOL) == 0)
		DEV_PTR(gna)->enabled = 0;

	/* Enable/Disable VPU based on CMOS settings */
	if (get_uint_option_checked("vpu", 0, OPTION_BOOL) == 0)
		DEV_PTR(vpu)->enabled = 0;
}
