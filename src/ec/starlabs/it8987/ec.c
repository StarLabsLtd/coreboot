/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <device/device.h>
#include <device/pnp.h>
#include <pc80/keyboard.h>
#include <ec/acpi/ec.h>
#include <delay.h>
#include <option.h>

#include "ec.h"
#include "chip.h"

void it8987_write_data(u8 addr, u8 data)
{
	outb(addr, IT8987E_ADDR);
	outb(data, IT8987E_DATA);
}

u8 it8987_read_data(u8 addr)
{
	outb(addr, IT8987E_ADDR);
	return inb(IT8987E_DATA);
}

u16 it8987_read_chipid(void)
{
	return((it8987_read_data(0x20) << 8) | it8987_read_data(0x21));
}

u16 it8987_get_version(void)
{
	return (ec_read(0x00) << 8) | ec_read(0x01);
}

static void it8987_init(struct device *dev)
{
	u8 val;

	if (!dev->enabled)
		return;

	if (it8987_read_chipid() != 0x8987) {
		printk(BIOS_DEBUG, "IT8987: Device not found.\n");
		return;
	}

	printk(BIOS_DEBUG, "IT8987: Initializing keyboard.\n");
	pc_keyboard_init(NO_AUX_DEVICE);

	/* Enable the keyboard backlight support. */
	ec_write(0x18, 0xaa);
	ec_write(0x19, 0xdd);

	/* Set the timeout for the keyboard backlight. */
	if (get_option(&val, "kbl_timeout") != CB_SUCCESS)
		val = 0;
	ec_write(ECRAM_KBL_TIMEOUT, val);

	/*
	 * Set the correct state for the Ctrl Fn Reverse option. This
	 * swaps the Ctrl and Fn keys to make it like an Apple keyboard.
	 */
	if (get_option(&val, "fn_ctrl_swap") != CB_SUCCESS)
		val = 0;
	ec_write(ECRAM_FN_CTRL_REVERSE, val);

	/*
	 * Copy the stored state of the fn_lock_state CMOS variable to the
	 * corresponding location within the EC RAM.
	 */
	if (get_option(&val, "fn_lock_state") != CB_SUCCESS)
		val = 0;
	ec_write(ECRAM_FN_LOCK_STATE, val);
}

static struct device_operations ops = {
	.init             = it8987_init,
	.read_resources   = noop_read_resources,
	.set_resources    = noop_set_resources,
};

static struct pnp_info pnp_dev_info[] = {
	{ NULL, 0, 0, 0, }
};

static void enable_dev(struct device *dev)
{
	pnp_enable_devices(dev, &ops, ARRAY_SIZE(pnp_dev_info), pnp_dev_info);
}

struct chip_operations ec_starlabs_it8987_ops = {
	CHIP_NAME("ITE IT8987 EC")
	.enable_dev = enable_dev
};
