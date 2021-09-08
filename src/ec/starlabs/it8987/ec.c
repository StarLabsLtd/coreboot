/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <delay.h>
#include <device/device.h>
#include <device/pnp.h>
#include <ec/acpi/ec.h>
#include <option.h>
#include <pc80/keyboard.h>

#include "chip.h"
#include "ec.h"

u16 it_get_version(void)
{
	return (ec_read(0x00) << 8) | ec_read(0x01);
}

static void it8987_init(struct device *dev)
{
	if (!dev->enabled)
		return;

	/*
	 * The address/data IO port pair for the IT8987 EC are configurable
	 * through the EC domain and are fixed by the EC's firmware blob. If
	 * the value(s) passed through the "dev" structure don't match the
	 * expected values then output severe warnings.
	 */
	if (dev->path.pnp.port != IT8987E_FIXED_ADDR) {
		printk(BIOS_ERR, "IT8987: Incorrect ports defined in devicetree.cb.\n");
		printk(BIOS_ERR, "IT8987: Serious operational issues will arise.\n");
		return;
	}

	u8 chipid1 = pnp_read_index(dev->path.pnp.port, IT8987_CHIPID1);
	u8 chipid2 = pnp_read_index(dev->path.pnp.port, IT8987_CHIPID2);
	if (chipid1 != IT8987_CHIPID1_VAL || chipid2 != IT8987_CHIPID2_VAL) {
		printk(BIOS_DEBUG, "IT8987: Device not found.\n");
		return;
	}

	printk(BIOS_DEBUG, "IT8987: Initializing keyboard.\n");
	pc_keyboard_init(NO_AUX_DEVICE);

	/* Enable the keyboard backlight support. */
	u8 lastlevel = ec_read(ECRAM_KBL_BRIGHTNESS);
	if ((lastlevel == KBL_OFF) || (lastlevel == KBL_ON)) {
		ec_write(ECRAM_KBL_STATE, lastlevel);
	} else {
		ec_write(ECRAM_KBL_STATE, KBL_ON);
	}

	/* Set the timeout for the keyboard backlight. */
	ec_write(ECRAM_KBL_TIMEOUT, get_uint_option("kbl_timeout", 0));

	/* Set the maximum charge level for the internal battery */
	ec_write(ECRAM_MAX_CHARGE, get_uint_option("max_charge", 0));

	/* Set the fan mode */
	ec_write(ECRAM_MAX_CHARGE, get_uint_option("fan_mode", 0));

	/*
	 * Restore the stored state for the ctrl_fn_reverse CMOS variable to the
	 * corresponding location within the EC RAM.
	 */
	ec_write(ECRAM_FN_CTRL_REVERSE, get_uint_option("fn_ctrl_swap", 0));

	/*
	 * Restore the stored state of the fn_lock_state CMOS variable to the
	 * corresponding location within the EC RAM.
	 */
	ec_write(ECRAM_FN_LOCK_STATE, get_uint_option("fn_lock_state", 0));

	/*
	 * Restore the stored state of the trackpad_state CMOS variable to the
	 * corresponding location within the EC RAM.
	 */
	u8 laststate = ec_read(ECRAM_TRACKPAD_STATE);
	/*
	 * Enabled:	0x11
	 * Disabled:	0x22
	 */
	if (laststate == 0) {
		ec_write(ECRAM_TRACKPAD_STATE, 0x11);
	} else {
		ec_write(ECRAM_TRACKPAD_STATE, get_uint_option("trackpad_state", 0));
	}
}

static struct device_operations ops = {
	.init		= it8987_init,
	.read_resources	= noop_read_resources,
	.set_resources	= noop_set_resources,
};

static struct pnp_info pnp_dev_info[] = {
	{ NULL,
	  0,
	  0,
	  0, }
};

static void enable_dev(struct device *dev)
{
	pnp_enable_devices(dev, &ops, ARRAY_SIZE(pnp_dev_info), pnp_dev_info);
}

struct chip_operations ec_starlabs_it8987_ops = {CHIP_NAME("ITE IT8987 EC").enable_dev =
							 enable_dev};
