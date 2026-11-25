/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <chip.h>
#include <cpu/intel/turbo.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <device/resource.h>
#include <devtree_update.h>
#include <drivers/i2c/hid/chip.h>
#include <intelblocks/gpio.h>
#include <option.h>
#include <static.h>
#include <types.h>
#include <variants.h>
#include <common/powercap.h>

void mb_devtree_update(void)
{
	config_t *cfg = config_of_soc();

	update_power_limits(cfg);
	if (!CONFIG(CONSOLE_SERIAL)) {
		DEV_PTR(uart0)->enabled = 0;
		cfg->serial_io_uart_mode[PchSerialIoIndexUART0] = PchSerialIoDisabled;
	}

	/* Enable/Disable WiFi based on CMOS settings */
	if (get_uint_option_checked("wifi", 1, OPTION_BOOL) == 0)
		DEV_PTR(cnvi_wifi)->enabled = 0;

	/* Enable/Disable Bluetooth based on CMOS settings */
	if (get_uint_option_checked("bluetooth", 1, OPTION_BOOL) == 0)
		cfg->usb2_ports[9].enable = 0;

	/* Enable/Disable Webcam/Camera based on CMOS settings */
	if (get_uint_option_checked("webcam", 1, OPTION_BOOL) == 0)
		cfg->usb2_ports[CONFIG_CCD_PORT].enable = 0;

	/* Enable/Disable Touchscreen based on CMOS settings */
	if (get_uint_option_checked("touchscreen", 1, OPTION_BOOL) == 0)
		DEV_PTR(i2c2)->enabled = 0;

	/* Enable/Disable Accelerometer based on CMOS settings */
	if (get_uint_option_checked("accelerometer", 1, OPTION_BOOL) == 0)
		DEV_PTR(i2c0)->enabled = 0;

	/* Enable/Disable GNA based on CMOS settings */
	if (get_uint_option_checked("gna", 0, OPTION_BOOL) == 0)
		DEV_PTR(gna)->enabled = 0;

	/* Enable/Disable the dedicated card reader root port based on CMOS Settings */
	if (get_uint_option_checked("card_reader", 0, OPTION_BOOL) == 0)
		cfg->usb2_ports[3].enable = 0;
}

#if CONFIG(PAYLOAD_RESOURCE_HANDOFF)
size_t fill_lb_i2c_hid(struct lb_prh_i2c_hid *devices, size_t capacity)
{
	const struct device *touchscreen = DEV_PTR(touchscreen);
	const struct drivers_i2c_hid_config *config = touchscreen->chip_info;
	const struct device *controller = DEV_PTR(i2c2);
	const struct resource *bar;

	if (!capacity || !touchscreen->enabled || !controller->enabled)
		return 0;

	bar = probe_resource(controller, PCI_BASE_ADDRESS_0);
	if (!bar || !bar->base || bar->size < 0x100 || bar->size > UINT32_MAX ||
	    (bar->flags & IORESOURCE_TYPE_MASK) != IORESOURCE_MEM ||
	    !(bar->flags & IORESOURCE_ASSIGNED) ||
	    !(bar->flags & (IORESOURCE_STORED | IORESOURCE_FIXED)))
		return 0;

	/* The board releases reset before handoff and GPP_F18 asserts low. */
	*devices = (struct lb_prh_i2c_hid) {
		.revision = LB_PRH_I2C_HID_REVISION,
		.family = LB_PRH_I2C_HID_DESIGNWARE,
		.controller_base = bar->base,
		.controller_size = bar->size,
		.ready_address = (uintptr_t)gpio_dwx_address(GPP_F18),
		.ready_mask = PAD_CFG0_RX_STATE,
		.ready_value = 0,
		.descriptor_register = config->hid_desc_reg_offset,
		.address = touchscreen->path.i2c.device,
	};
	return 1;
}
#endif
