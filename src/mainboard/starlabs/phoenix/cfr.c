/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <boot/coreboot_tables.h>
#include <drivers/option/cfr_frontend.h>
#include <common/cfr_touchpad.h>
#include <ec/starlabs/merlin/cfr.h>

static struct sm_obj_form battery_group = {
	.ui_name = "Battery",
	.obj_list = (const struct sm_object *[]) {
		#if CONFIG(EC_STARLABS_MAX_CHARGE)
		&max_charge,
		#endif
		#if CONFIG(EC_STARLABS_CHARGING_SPEED)
		&charging_speed,
		#endif
		#if CONFIG(EC_STARLABS_ADAPTER_AUTO_POWER_ON)
		&power_on_ac,
		#endif
		NULL,
	},
};

static struct sm_obj_form keyboard_group = {
	.ui_name = "Keyboard",
	.obj_list = (const struct sm_object *[]) {
		&kbl_timeout,
		&fn_ctrl_swap,
		NULL,
	},
};

static struct sm_obj_form led_group = {
	.ui_name = "LEDs",
	.obj_list = (const struct sm_object *[]) {
		#if CONFIG(EC_STARLABS_CHARGE_LED)
		&charge_led,
		#endif
		#if CONFIG(EC_STARLABS_POWER_LED)
		&power_led,
		#endif
		NULL,
	},
};

static struct sm_obj_form lid_group = {
	.ui_name = "Lid",
	.obj_list = (const struct sm_object *[]) {
		#if CONFIG(EC_STARLABS_LID_SWITCH)
		&lid_switch,
		#endif
		NULL,
	},
};

static struct sm_obj_form performance_group = {
	.ui_name = "Performance",
	.obj_list = (const struct sm_object *[]) {
		#if CONFIG(EC_STARLABS_FAN)
		&fan_mode,
		#endif
		NULL,
	},
};

static struct sm_obj_form trackpad_group = {
	.ui_name = "Trackpad",
	.obj_list = (const struct sm_object *[]) {
		&touchpad_haptics,
		&touchpad_force_press,
		&touchpad_force_release,
		&touchpad_report_rate,
		NULL,
	},
};

static const struct sm_object rom_armor = SM_DECLARE_BOOL({
	.flags		= CONFIG(SOC_AMD_COMMON_BLOCK_PSP_ROM_ARMOR_RUNTIME_OPTION) ?
			  0 : CFR_OPTFLAG_SUPPRESS,
	.opt_name	= "rom_armor",
	.ui_name	= "ROM Armor",
	.ui_helptext	= "Restrict SPI flash writes to System Management Mode. "
			  "Disabling this allows direct flash access. Requires a reboot.",
	.default_value	= true,
});

static struct sm_obj_form security = {
	.ui_name = "Security",
	.obj_list = (const struct sm_object *[]) {
		&rom_armor,
		NULL,
	},
};

void mb_cfr_setup_menu(struct lb_cfr *cfr_root)
{
	struct sm_obj_form *forms[] = {
		&battery_group,
		&keyboard_group,
		&led_group,
		&lid_group,
		&performance_group,
		&trackpad_group,
		&security,
		NULL,
	};

	cfr_write_setup_menu(cfr_root, forms);
}
