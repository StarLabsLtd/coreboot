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

	/*
	 * Restore settings from CMOS into EC RAM:
	 *
	 * kbl_timeout
	 * fn_ctrl_swap
	 * max_charge
	 * fan_mode
	 * fn_lock_state
	 * trackpad_state
	 * kbl_brightness
	 * kbl_state
	 *
	 */

	/*
	 * Keyboard Backlight Timeout
	 *
	 * Setting:	kbl_timeout
	 *
	 * Values:	30 Seconds, 1 Minute, 3 Minutes, 5 Minutes, Never
	 * Default:	30 Seconds
	 *
	 */
	switch (get_uint_option("kbl_timeout", 0)) {
		case MIN_1:
			ec_write(ECRAM_KBL_TIMEOUT, MIN_1);
			break;
		case MIN_3:
			ec_write(ECRAM_KBL_TIMEOUT, MIN_3);
			break;
		case MIN_5:
			ec_write(ECRAM_KBL_TIMEOUT, MIN_5);
			break;
		case NEVER:
			ec_write(ECRAM_KBL_TIMEOUT, NEVER);
			break;
		default:
			ec_write(ECRAM_KBL_TIMEOUT, SEC_30);
			break;
	}

	/*
	 * Fn Ctrl Reverse
	 *
	 * Setting:	fn_ctrl_swap
	 *
	 * Values:	Enabled, Disabled
	 * Default:	Disabled
	 *
	 */
	switch (get_uint_option("fn_ctrl_swap", 0)) {
		case CTRL_FN:
			ec_write(ECRAM_FN_CTRL_REVERSE, CTRL_FN);
			break;
		default:
			ec_write(ECRAM_FN_CTRL_REVERSE, FN_CTRL);
			break;
	}

	/*
	 * Maximum Charge Level
	 *
	 * Setting:	max_charge
	 *
	 * Values:	60%, 80%, 100%
	 * Default:	100%
	 *
	 */
	switch (get_uint_option("max_charge", 0)) {
		case CHARGE_80:
			ec_write(ECRAM_MAX_CHARGE, VAL_CHARGE_80);
			break;
		case CHARGE_60:
			ec_write(ECRAM_MAX_CHARGE, VAL_CHARGE_60);
			break;
		default:
		 	ec_write(ECRAM_MAX_CHARGE, VAL_CHARGE_100);
			break;
	}
	
	/*
	 * Fan Mode
	 *
	 * Setting:	fan_mode
	 *
	 * Values:	Quiet, Normal, Aggressive
	 * Default:	Normal
	 *
	 */
	switch (get_uint_option("fan_mode", 0)) {
		case FAN_AGGRESSIVE:
	 		ec_write(ECRAM_FAN_MODE, VAL_FAN_AGGRESSIVE);
			break;
		case FAN_QUIET:
			ec_write(ECRAM_FAN_MODE, VAL_FAN_QUIET);
			break;
		default:
			ec_write(ECRAM_FAN_MODE, VAL_FAN_NORMAL);
			break;
	}
	
	/*
	 * Function Lock
	 *
	 * Setting:	fn_lock_state
	 *
	 * Values:	Locked, Unlocked
	 * Default:	Locked
	 *
	 */
	switch (get_uint_option("fn_lock_state", 0)) {
		case UNLOCKED:
			ec_write(ECRAM_FN_LOCK_STATE, UNLOCKED);
			break;
		default:
			ec_write(ECRAM_FN_LOCK_STATE, LOCKED);
			break;
	}

	/*
	 * Trackpad State
	 *
	 * Setting:	trackpad_state
	 *
	 * Values:	Enabled, Disabled
	 * Default:	Enabled
	 *
	 */
	switch (get_uint_option("trackpad_state", 0)) {
		case TRACKPAD_DISABLED:
			ec_write(ECRAM_TRACKPAD_STATE, VAL_TRACKPAD_DISABLED);
			break;
		default:
			ec_write(ECRAM_TRACKPAD_STATE, VAL_TRACKPAD_ENABLED);
			break;
	}

	/*
	 * Keyboard Backlight Brightness
	 *
	 * Setting:	kbl_brightness
	 *
	 * Values:	Off, High
	 * Default:	High
	 *
	 */
	switch (get_uint_option("kbl_brightness", 0)) {
		case KBL_OFF:
			ec_write(ECRAM_KBL_BRIGHTNESS, VAL_KBL_OFF);
			break;
		default:
			ec_write(ECRAM_KBL_BRIGHTNESS, VAL_KBL_HIGH);
			break;
	}
	
	/*
	 * Keyboard Backlight State
	 *
	 * Setting:	kbl_state
	 *
	 * Values:	Off, On
	 * Default:	On
	 *
	 */
	switch (get_uint_option("kbl_state", 0)) {
		case KBL_DISABLED:
			ec_write(ECRAM_KBL_STATE, VAL_KBL_DISABLED);
			break;
		default:
			ec_write(ECRAM_KBL_STATE, VAL_KBL_ENABLED);
			break;
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
