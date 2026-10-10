/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <boot/coreboot_tables.h>
#include <drivers/option/cfr_frontend.h>
#include <common/cfr_touchpad.h>
#include <ec/starlabs/merlin/cfr.h>
#include <variants.h>

static const struct sm_object pciexp_wifi_clk_pm = SM_DECLARE_BOOL({
	.opt_name	= "pciexp_wifi_clk_pm",
	.ui_name	= "Wi-Fi Clock Power Management",
	.ui_helptext	= "Allow Wi-Fi clock power management. Requires a reboot.",
	.default_value	= true,
});

static const struct sm_object pciexp_wifi_aspm = SM_DECLARE_ENUM({
	.opt_name	= "pciexp_wifi_aspm",
	.ui_name	= "Wi-Fi ASPM",
	.ui_helptext	= "Limit Wi-Fi Active State Power Management. Requires a reboot.",
	.default_value	= PHX_ASPM_DEFAULT,
	.values		= (const struct sm_enum_value[]) {
		{ "Default", PHX_ASPM_DEFAULT },
		{ "Disabled", PHX_ASPM_DISABLED },
		{ "L1", PHX_ASPM_L1 },
		SM_ENUM_VALUE_END,
	},
});

static const struct sm_object pciexp_ssd_clk_pm = SM_DECLARE_BOOL({
	.opt_name	= "pciexp_ssd_clk_pm",
	.ui_name	= "SSD Clock Power Management",
	.ui_helptext	= "Allow SSD clock power management. Requires a reboot.",
	.default_value	= true,
});

static const struct sm_object pciexp_ssd_aspm = SM_DECLARE_ENUM({
	.opt_name	= "pciexp_ssd_aspm",
	.ui_name	= "SSD ASPM",
	.ui_helptext	= "Limit SSD Active State Power Management. Requires a reboot.",
	.default_value	= PHX_ASPM_DEFAULT,
	.values		= (const struct sm_enum_value[]) {
		{ "Default", PHX_ASPM_DEFAULT },
		{ "Disabled", PHX_ASPM_DISABLED },
		{ "L0s", PHX_ASPM_L0S },
		{ "L1", PHX_ASPM_L1 },
		{ "L0s and L1", PHX_ASPM_L0S_L1 },
		SM_ENUM_VALUE_END,
	},
});

static const struct sm_object pciexp_ssd_l1ss = SM_DECLARE_ENUM({
	.opt_name	= "pciexp_ssd_l1ss",
	.ui_name	= "SSD L1 Substates",
	.ui_helptext	= "Limit SSD PCI-PM and ASPM L1 substates. Requires a reboot.",
	.default_value	= PHX_L1SS_DEFAULT,
	.values		= (const struct sm_enum_value[]) {
		{ "Default", PHX_L1SS_DEFAULT },
		{ "Disabled", PHX_L1SS_DISABLED },
		{ "L1.1", PHX_L1SS_L1_1 },
		{ "L1.1 and L1.2", PHX_L1SS_L1_2 },
		SM_ENUM_VALUE_END,
	},
});

static struct sm_obj_form pcie_group = {
	.ui_name = "PCIe Power Management",
	.obj_list = (const struct sm_object *[]) {
		&pciexp_wifi_clk_pm,
		&pciexp_wifi_aspm,
		&pciexp_ssd_clk_pm,
		&pciexp_ssd_aspm,
		&pciexp_ssd_l1ss,
		NULL,
	},
};

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
		&audio_group,
		&battery_group,
		&keyboard_group,
		&led_group,
		&pcie_group,
		&lid_group,
		&performance_group,
		&trackpad_group,
		&security,
		NULL,
	};

	cfr_write_setup_menu(cfr_root, forms);
}
