/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <commonlib/coreboot_tables.h>
#include <drivers/option/cfr.h>
#include <inttypes.h>
#include <intelblocks/pcie_rp.h>
#include <string.h>
#include <types.h>
#include <variants.h>

static uint32_t get_object_id(void)
{
	static uint32_t object_id = 0;
	/* Let's start at 1: this way, option ID being 0 indicates someone messed up */
	return ++object_id;
}

void lb_board(struct lb_header *header)
{
	struct sm_obj_enum boot_option = {
		.object_id	= get_object_id(),
		.opt_name	= "boot_option",
		.ui_name	= "Boot Option",
		.ui_helptext	= "Change the boot device in the event of a failed boot",
		.default_value	= 0,
		.values = (const struct sm_enum_value[]) {
			{ "Fallback", 		0		},
			{ "Normal",		1		},
			SM_ENUM_VALUE_END,
		},
	};

	#if CONFIG(EC_STARLABS_CHARGING_SPEED)
	struct sm_obj_enum charging_speed = {
		.object_id	= get_object_id(),
		.opt_name	= "charging_speed",
		.ui_name	= "Charging Speed",
		.ui_helptext	= "Set the maximum speed to charge the battery. Charging faster will increase heat and battery wear.",
		.default_value	= 1,
		.values = (const struct sm_enum_value[]) {
			{ "1.0C",		0	},
			{ "0.5C",		1	},
			{ "0.2C",		2	},
			SM_ENUM_VALUE_END,
		},
	};
	#endif

	struct sm_obj_enum debug_level = {
		.object_id	= get_object_id(),
		.opt_name	= "debug_level",
		.ui_name	= "Debug Level",
		.ui_helptext	= "Set the verbosity of the debug output.",
		.default_value	= 0,
		.values = (const struct sm_enum_value[]) {
			{ "Emergency",		0 		},
			{ "Alert",		1		},
			{ "Critical",		2		},
			{ "Error",		3		},
			{ "Warning",		4		},
			{ "Notice",		5		},
			{ "Info",		6		},
			{ "Debug",		7		},
			{ "Spew",		8		},
			SM_ENUM_VALUE_END,
		},
	};

	struct sm_obj_enum fan_mode = {
		.object_id	= get_object_id(),
		.opt_name	= "fan_mode",
		.ui_name	= "Fan Mode",
		.ui_helptext	= "Adjust the fan curve to prioritize performance or noise levels.",
		.default_value	= 0,
		.values	= (const struct sm_enum_value[]) {
			{ "Normal",		0		},
			{ "Aggressive",		1		},
			{ "Quiet",		2		},
			SM_ENUM_VALUE_END,
		},
	};

	struct sm_obj_bool fn_ctrl_swap = {
		.object_id	= get_object_id(),
		.opt_name	= "fn_ctrl_swap",
		.ui_name	= "Fn Ctrl Reverse",
		.ui_helptext	= "Swap the functions of the [Fn] and [Ctrl] keys",
		.default_value	= false,
	};

	struct sm_obj_bool hyper_threading = {
		.object_id	= get_object_id(),
		.opt_name	= "hyper_threading",
		.ui_name	= "Hyper-Threading",
		.ui_helptext	= "Enable or disable Hyper-Threading",
		.default_value	= true,
	};

	struct sm_obj_enum kbl_timeout = {
		.object_id	= get_object_id(),
		.opt_name	= "kbl_timeout",
		.ui_name	= "Keyboard Backlight Timeout",
		.ui_helptext	= "Set the amount of time before the keyboard backlight turns off when un-used",
		.default_value	= 0,
		.values	= (const struct sm_enum_value[]) {
			{ "30 seconds",		0	},
			{ "1 minute",		1	},
			{ "3 minutes",		2	},
			{ "5 minutes",		3	},
			{ "Never",		4	},
			SM_ENUM_VALUE_END,
		},
	};

	#if CONFIG(EC_STARLABS_LID_SWITCH)
	struct sm_obj_enum lid_switch = {
		.object_id	= get_object_id(),
		.opt_name	= "lid_switch",
		.ui_name	= "Lid Switch",
		.ui_helptext	= "Enable or disable the lid switch.",
		.default_value	= 0,
		.values = (const struct sm_enum_value[]) {
			{ "Enabled",		0	},
			{ "Disabled",		1	},
			SM_ENUM_VALUE_END,
		},
	};
	#endif

	struct sm_obj_enum max_charge = {
		.object_id	= get_object_id(),
		.opt_name	= "max_charge",
		.ui_name	= "Maximum Charge Level",
		.ui_helptext	= "Set the maximum level the battery will charge to.",
		.default_value	= 0,
		.values = (const struct sm_enum_value[]) {
			{ "100%",		0	},
			{ "80%",		1	},
			{ "60%",		2	},
			SM_ENUM_VALUE_END,
		},
	};

	struct sm_obj_enum me_state = {
		.object_id	= get_object_id(),
		.opt_name	= "me_state",
		.ui_name	= "Intel Management Engine",
		.ui_helptext	= "Enable or disable the Intel Management Engine",
		.default_value	= 1,
		.values = (const struct sm_enum_value[]) {
			{ "Disabled",		1		},
			{ "Enabled", 		0		},
			SM_ENUM_VALUE_END,
		},
	};

	struct sm_obj_number me_state_counter = {
		.object_id	= get_object_id(),
		.opt_name	= "me_state_counter",
		.ui_name	= "ME State Counter",
		.flags		= CFR_OPTFLAG_SUPPRESS,
		.default_value	= 0,
	};

	struct sm_obj_bool power_on_after_fail = {
		.object_id	= get_object_id(),
		.opt_name	= "power_on_after_fail",
		.ui_name	= "Power on after failure",
		.ui_helptext	= "Automatically turn on after a power failure",
		.default_value	= false,
	};

	struct sm_obj_enum power_profile = {
		.object_id	= get_object_id(),
		.opt_name	= "power_profile",
		.ui_name	= "Power Profile",
		.ui_helptext	= "Select whether to maximize performance, battery life or both.",
		.default_value	= 1,
		.values	= (const struct sm_enum_value[]) {
			{ "Power Saver",	PP_POWER_SAVER	},
			{ "Balanced",		PP_BALANCED	},
			{ "Performance",	PP_PERFORMANCE	},
			SM_ENUM_VALUE_END,
		},
	};

	#if CONFIG(SOC_INTEL_ALDERLAKE)
	struct sm_obj_enum pciexp_aspm = {
		.object_id	= get_object_id(),
		.opt_name	= "pciexp_aspm",
		.ui_name	= "PCI ASPM",
		.ui_helptext	= "Controls the Active State Power Management for PCI devices. Enabling this feature can reduce power consumption of PCI-connected devices during idle times.",
		.default_value	= ASPM_L0S_L1,
		.values	= (const struct sm_enum_value[]) {
			{ "Disabled",		ASPM_DISABLE 	},
			{ "L0s",		ASPM_L0S	},
			{ "L1",			ASPM_L1		},
			{ "L0sL1",		ASPM_L0S_L1	},
			SM_ENUM_VALUE_END,
		},
	};

	struct sm_obj_bool pciexp_clk_pm = {
		.object_id	= get_object_id(),
		.opt_name	= "pciexp_clk_pm",
		.ui_name	= "PCI Clock Power Management",
		.ui_helptext	= "Enables or disables power management for the PCI clock. When enabled, it reduces power consumption during idle states. This can help lower overall energy use but may impact performance in power-sensitive tasks.",
		.default_value	= true,
	};

	struct sm_obj_enum pciexp_l1ss = {
		.object_id	= get_object_id(),
		.opt_name	= "pciexp_l1ss",
		.ui_name	= "PCI L1 Substates",
		.ui_helptext	= "Controls deeper power-saving states for PCI devices. Enabling this feature allows supported devices to achieve lower power states at the cost of slightly increased latency when exiting these states.",
		.default_value	= L1_SS_L1_2,
		.values	= (const struct sm_enum_value[]) {
			{ "Disabled",		L1_SS_DISABLED	},
			{ "L1.1",		L1_SS_L1_1	},
			{ "L1.2",		L1_SS_L1_2	},
			SM_ENUM_VALUE_END,
		},
	};
	#endif

	struct sm_obj_number reboot_counter = {
		.object_id	= get_object_id(),
		.opt_name	= "reboot_counter",
		.ui_name	= "Reboot Counter",
		.flags		= CFR_OPTFLAG_SUPPRESS,
		.default_value	= 0,
	};

	#if CONFIG(DRIVERS_INTEL_USB4_RETIMER)
	struct sm_obj_bool thunderbolt = {
		.object_id	= get_object_id(),
		.opt_name	= "thunderbolt",
		.ui_name	= "Thunderbolt",
		.ui_helptext	= "Enable or disable Thunderbolt support",
		.default_value	= true,
	};
	#endif

	struct sm_obj_bool vtd = {
		.object_id	= get_object_id(),
		.opt_name	= "vtd",
		.ui_name	= "VT-d",
		.ui_helptext	= "Enable or disable Intel VT-d (virtualization)",
		.default_value	= true,
	};

	/* Performance */
	const struct sm_object performance[] = {
		SM_DECLARE_ENUM(fan_mode),
		SM_DECLARE_ENUM(power_profile)
	};

	/* Processor */
	const struct sm_object processor[] = {
		SM_DECLARE_ENUM(me_state),
		SM_DECLARE_NUMBER(me_state_counter),
		SM_DECLARE_BOOL(hyper_threading),
		SM_DECLARE_BOOL(vtd),
	};

	/* Power */
	const struct sm_object power[] = {
		SM_DECLARE_ENUM(max_charge),
		#if CONFIG(EC_STARLABS_CHARGING_SPEED)
			SM_DECLARE_ENUM(charging_speed),
		#endif
		SM_DECLARE_BOOL(power_on_after_fail)
	};

	/* Keyboard */
	const struct sm_object keyboard[] = {
		SM_DECLARE_ENUM(kbl_timeout),
		SM_DECLARE_BOOL(fn_ctrl_swap),
	};

	/* Devices */
	const struct sm_object device[] = {
		#if CONFIG(EC_STARLABS_LID_SWITCH)
			SM_DECLARE_ENUM(lid_switch),
		#endif
		#if CONFIG(DRIVERS_INTEL_USB4_RETIMER)
			SM_DECLARE_BOOL(thunderbolt),
		#endif
	};

	/* PCI */
	const struct sm_object pci[] = {
		#if CONFIG(SOC_INTEL_ALDERLAKE)
			SM_DECLARE_BOOL(pciexp_clk_pm),
			SM_DECLARE_ENUM(pciexp_aspm),
			SM_DECLARE_ENUM(pciexp_l1ss),
		#endif
	};

	/* Coreboot Options */
	const struct sm_object coreboot[] = {
		SM_DECLARE_ENUM(boot_option),
		SM_DECLARE_ENUM(debug_level),
		SM_DECLARE_NUMBER(reboot_counter)
	};

	const struct sm_obj_form root_contents[] = {
		{
			.object_id	= get_object_id(),
			.ui_name	= "Performance",
			.obj_list	= performance,
			.num_objects	= ARRAY_SIZE(performance),
		},
		{
			.object_id	= get_object_id(),
			.ui_name	= "Processor",
			.obj_list	= processor,
			.num_objects	= ARRAY_SIZE(processor),
		},
		{
			.object_id	= get_object_id(),
			.ui_name	= "Power",
			.obj_list	= power,
			.num_objects	= ARRAY_SIZE(power),
		},
		{
			.object_id	= get_object_id(),
			.ui_name	= "Keyboard",
			.obj_list	= keyboard,
			.num_objects	= ARRAY_SIZE(keyboard),
		},
		{
			.object_id	= get_object_id(),
			.ui_name	= "Device",
			.obj_list	= device,
			.num_objects	= ARRAY_SIZE(device),
		},
		{
			.object_id	= get_object_id(),
			.ui_name	= "PCI",
			.obj_list	= pci,
			.num_objects	= ARRAY_SIZE(pci),
		},
		{
			.object_id	= get_object_id(),
			.ui_name	= "coreboot",
			.obj_list	= coreboot,
			.num_objects	= ARRAY_SIZE(coreboot),
		}
	};

	const struct setup_menu_root sm_root = {
		.form_list	= root_contents,
		.num_forms	= ARRAY_SIZE(root_contents),
	};

	cfr_set_default_values(&sm_root);
	cfr_write_setup_menu(header, &sm_root);
}

