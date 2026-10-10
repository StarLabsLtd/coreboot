/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <boot/coreboot_tables.h>
#include <drivers/option/cfr_frontend.h>
#include <common/cfr_touchpad.h>
#include <ec/starlabs/merlin/cfr.h>

static const struct sm_object microphone = SM_DECLARE_BOOL({
	.opt_name	= "microphone",
	.ui_name	= "Microphone",
	.ui_helptext	= "Enable or disable the built-in microphone. Requires a reboot.",
	.default_value	= true,
});

static struct sm_obj_form audio_group = {
	.ui_name = "Audio",
	.obj_list = (const struct sm_object *[]) {
		&microphone,
		NULL,
	},
};

static struct sm_obj_form battery_group = {
	.ui_name = "Battery",
	.obj_list = (const struct sm_object *[]) {
		#if CONFIG(EC_STARLABS_MAX_CHARGE)
		&max_charge,
		#endif
		#if CONFIG(EC_STARLABS_CHARGING_SPEED)
		&charging_speed,
		#endif
		&charging_policy,
		&power_reporting,
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

void mb_cfr_setup_menu(struct lb_cfr *cfr_root)
{
	struct sm_obj_form *forms[] = {
		&audio_group,
		&battery_group,
		&keyboard_group,
		&led_group,
		&lid_group,
		&performance_group,
		&trackpad_group,
		NULL,
	};

	cfr_write_setup_menu(cfr_root, forms);
}
