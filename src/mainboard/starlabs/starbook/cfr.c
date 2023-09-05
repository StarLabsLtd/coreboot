/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <commonlib/coreboot_tables.h>
#include <drivers/option/cfr.h>
#include <inttypes.h>
#include <string.h>
#include <types.h>
#include <variants.h>

static uint32_t starbook_get_object_id(void)
{
	static uint32_t object_id = 0;

	/* Let's start at 1: this way, option ID being 0 indicates someone messed up */
	return ++object_id;
}

void lb_board(struct lb_header *header)
{
	const struct sm_obj_enum enum_opts[] = {
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "boot_option",
			.ui_name	= "Boot Option",
			.ui_helptext	= "Change the boot device in the event of a failed boot",
			.default_value	= 0,
			.values		= (const struct sm_enum_value[]) {
				{	"Fallback",	0		},
				{	"Normal",	1		},
				SM_ENUM_VALUE_END,
			}
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "debug_level",
			.ui_name	= "Debug Level",
			.ui_helptext	= "Set the verbosity of the debug output.",
			.default_value	= 0,
			.values		= (const struct sm_enum_value[]) {
				{	"Emergency",	0		},
				{	"Alert",	1		},
				{	"Critical",	2		},
				{	"Error",	3		},
				{	"Warning",	4		},
				{	"Notice",	5		},
				{	"Info",		6		},
				{	"Debug",	7		},
				{	"Spew",		8		},
				SM_ENUM_VALUE_END,
			}
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "fan_mode",
			.ui_name	= "Fan Mode",
			.ui_helptext	= "Adjust the fan curve to priotise performance or noise levels.",
			.default_value	= 1,
			.values		= (const struct sm_enum_value[]) {
				{	"Normal",	0		},
				{	"Aggressive",	1		},
				{	"Quiet",	2		},
				SM_ENUM_VALUE_END,
			}
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "kbl_timeout",
			.ui_name	= "Keyboard Backlight Timeout",
			.ui_helptext	= "Set the amount of time before the keyboard backlight turns off when un-used",
			.default_value	= 0,
			.values		= (const struct sm_enum_value[]) {
				{	"30 seconds",	0		},
				{	"1 minute",	1		},
				{	"3 minutes",	2		},
				{	"5 minutes",	3		},
				{	"Never",	4		},
				SM_ENUM_VALUE_END,
			}
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "max_charge",
			.ui_name	= "Maximum Charge Level",
			.ui_helptext	= "Set the maximum level the battery will charge to.",
			.default_value	= 0,
			.values		= (const struct sm_enum_value[]) {
				{	"100%",		0		},
				{	"80%",		1		},
				{	"60%",		2		},
				SM_ENUM_VALUE_END,
			}
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "me_state",
			.ui_name	= "Intel Management Engine",
			.ui_helptext	= "Enable or disable the Intel Management Engine",
			.default_value	= 1,
			.values		= (const struct sm_enum_value[]) {
				{	"Disabled",	1		},
				{	"Enabled",	0		},
				SM_ENUM_VALUE_END,
			}
		},
		{

			.object_id	= starbook_get_object_id(),
			.opt_name	= "power_profile",
			.ui_name	= "Power Profile",
			.ui_helptext	= "Select whether to maximise performance, battery life or both.",
			.default_value	= 1,
			.values		= (const struct sm_enum_value []) {
				{	"Power Saver",	PP_POWER_SAVER	},
				{	"Balanced",	PP_BALANCED	},
				{	"Performance",	PP_PERFORMANCE	},
				SM_ENUM_VALUE_END,
			},
		},
	};

	const struct sm_obj_bool bool_opts[] = {
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "fn_ctrl_swap",
			.ui_name	= "Fn Ctrl Reverse",
			.ui_helptext	= "Swap the functions of the [Fn] and [Ctrl] keys",
			.default_value	= false,
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "legacy_8254_timer",
			.ui_name	= "Legacy 8254 Timer",
			.ui_helptext	= "Enable or disable the legacy 8254 timer. Increases power consumption"
					" by disabling clock gating, but increases compatibility with older"
					" Operating Systems",
			.default_value	= true,
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "hyper_threading",
			.ui_name	= "Hyper-Threading",
			.ui_helptext	= "Enable or disable Hyper-Threading",
			.default_value	= true,
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "microphone",
			.ui_name	= "Microphone",
			.ui_helptext	= "Enable or disable the built-in microphone",
			.default_value	= true,
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "mirror_flag",
			.ui_name	= "Mirror Flag",
			.ui_helptext	= "Use the mirror flag to automatically update the EC firmware from coreboot.",
			.default_value	= false,
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "pci_hot_plug",
			.ui_name	= "Third-Party SSD Support",
			.ui_helptext	= "Enables PCI Hot Plug, which slows down the SSD initialisation. It is"
					"required for certain third-party SSDs to be detected.",
			.default_value	= false,
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "thunderbolt",
			.ui_name	= "Thunderbolt",
			.ui_helptext	= "Enable or disable Thunderbolt support",
			.default_value	= true,
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "vtd",
			.ui_name	= "VT-d",
			.ui_helptext	= "Enable or disable Intel VT-d (virtualisation)",
			.default_value	= true,
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "webcam",
			.ui_name	= "Webcam",
			.ui_helptext	= "Enable or disable the built-in webcam",
			.default_value	= true,
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "wireless",
			.ui_name	= "Wireless",
			.ui_helptext	= "Enable or disable the built-in wireless card",
			.default_value	= true,
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "power_on_after_fail",
			.ui_name	= "Power on after failure",
			.ui_helptext	= "Automatically turn on after a power failure",
			.default_value	= false,
		}
	};

	const struct sm_obj_number number_opts[] = {
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "me_state_counter",
			.ui_name	= "ME State Counter",
			.flags		= CFR_OPTFLAG_SUPPRESS,
			.default_value	= 0,
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "mirror_flag_counter",
			.ui_name	= "Mirror Flag Counter",
			.flags		= CFR_OPTFLAG_SUPPRESS,
			.default_value	= 0,
		},
		{
			.object_id	= starbook_get_object_id(),
			.opt_name	= "reboot_counter",
			.ui_name	= "Reboot Counter",
			.flags		= CFR_OPTFLAG_SUPPRESS,
			.default_value	= 0,
		},
	};

	/* General Options */
	const struct sm_object general_options[] = {
		SM_DECLARE_ENUM(enum_opts[6]),		// power profile
		SM_DECLARE_ENUM(enum_opts[2]),		// fan mode
		SM_DECLARE_ENUM(enum_opts[4]),		// maximum charge level
		SM_DECLARE_ENUM(enum_opts[3]),		// keyboard backlight timeout
		SM_DECLARE_BOOL(bool_opts[0]),		// function control swap
	};

	/* Device Options */
	const struct sm_object device_options[] = {
		SM_DECLARE_BOOL(bool_opts[9]),		// wireless
		SM_DECLARE_BOOL(bool_opts[8]),		// webcam
	#if CONFIG(DRIVERS_INTEL_USB4_RETIMER)
		SM_DECLARE_BOOL(bool_opts[6]),		// thunderbolt
	#endif
	#if CONFIG(BOARD_STARLABS_STARBOOK_ADL)
		SM_DECLARE_BOOL(bool_opts[5]),		// pci_hot_plug
	#endif
		SM_DECLARE_BOOL(bool_opts[3]),		// microphone
		SM_DECLARE_BOOL(bool_opts[10]),		// power_on_after_fail
	};

	/* Chipset Options */
	const struct sm_object chipset_options[] = {
		SM_DECLARE_ENUM(enum_opts[5]),		// me_state
		SM_DECLARE_NUMBER(number_opts[0]),	// me_state_counter
		SM_DECLARE_BOOL(bool_opts[1]),		// legacy_8254_timer
		SM_DECLARE_BOOL(bool_opts[2]),		// hyper_threading
		SM_DECLARE_BOOL(bool_opts[7]),		// vtd
	};

	/* coreboot Options */
	const struct sm_object coreboot_options[] = {
		SM_DECLARE_ENUM(enum_opts[0]),		// boot_option
		SM_DECLARE_ENUM(enum_opts[1]),		// debug_level
		SM_DECLARE_NUMBER(number_opts[2]),	// reboot_counter
	};

	/* Embedded Controller Options */
	const struct sm_object ec_options[] = {
	#if CONFIG(EC_STARLABS_MIRROR_SUPPORT)
	#if !CONFIG(DRIVERS_INTEL_USB4_RETIMER)
		SM_DECLARE_BOOL(bool_opts[4]),		// mirror_flag
	#endif
		SM_DECLARE_NUMBER(number_opts[1]),	// mirror_flag_counter
	#endif
	};

	const struct sm_obj_form root_contents[] = {
		{
			.object_id	= starbook_get_object_id(),
			.ui_name	= "General Options",
			.obj_list	= general_options,
			.num_objects	= ARRAY_SIZE(general_options),
		},
		{
			.object_id	= starbook_get_object_id(),
			.ui_name	= "Devices",
			.obj_list	= device_options,
			.num_objects	= ARRAY_SIZE(device_options),
		},
		{
			.object_id	= starbook_get_object_id(),
			.ui_name	= "Chipset",
			.obj_list	= chipset_options,
			.num_objects	= ARRAY_SIZE(chipset_options),
		},
		{
			.object_id	= starbook_get_object_id(),
			.ui_name	= "coreboot",
			.obj_list	= coreboot_options,
			.num_objects	= ARRAY_SIZE(coreboot_options),
		},
		{
			.object_id	= starbook_get_object_id(),
			.ui_name	= "Embedded Controller",
			.obj_list	= ec_options,
			.num_objects	= ARRAY_SIZE(ec_options),
			.flags		= CFR_OPTFLAG_SUPPRESS,
		},
	};

	const struct setup_menu_root sm_root = {
		.form_list	= root_contents,
		.num_forms	= ARRAY_SIZE(root_contents),
	};

	cfr_set_default_values(&sm_root);
	cfr_write_setup_menu(header, &sm_root);
}

