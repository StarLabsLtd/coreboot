/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <commonlib/coreboot_tables.h>
#include <drivers/option/cfr.h>
#include <inttypes.h>
#include <string.h>
#include <types.h>
#include <variants.h>

static uint32_t byte_get_object_id(void)
{
	static uint32_t object_id = 0;

	/* Let's start at 1: this way, option ID being 0 indicates someone messed up */
	return ++object_id;
}

void lb_board(struct lb_header *header)
{
	const struct sm_obj_enum enum_opts[] = {
		{
			.object_id	= byte_get_object_id(),
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
			.object_id	= byte_get_object_id(),
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
			.object_id	= byte_get_object_id(),
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

			.object_id	= byte_get_object_id(),
			.opt_name	= "power_profile",
			.ui_name	= "Power Profile",
			.ui_helptext	= "Select whether to maximise performance, battery life or both.",
			.default_value	= 0,
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
			.object_id	= byte_get_object_id(),
			.opt_name	= "vtd",
			.ui_name	= "VT-d",
			.ui_helptext	= "Enable or disable Intel VT-d (virtualisation)",
			.default_value	= true,
		},
		{
			.object_id	= byte_get_object_id(),
			.opt_name	= "wireless",
			.ui_name	= "Wireless",
			.ui_helptext	= "Enable or disable the built-in wireless card",
			.default_value	= true,
		},
		{
			.object_id	= byte_get_object_id(),
			.opt_name	= "power_on_after_fail",
			.ui_name	= "Power on after failure",
			.ui_helptext	= "Automatically turn on after a power failure",
			.default_value	= false,
		}
	};

	const struct sm_obj_number number_opts[] = {
		{
			.object_id	= byte_get_object_id(),
			.opt_name	= "me_state_counter",
			.ui_name	= "ME State Counter",
			.flags		= CFR_OPTFLAG_SUPPRESS,
			.default_value	= 0,
		},
		{
			.object_id	= byte_get_object_id(),
			.opt_name	= "reboot_counter",
			.ui_name	= "Reboot Counter",
			.flags		= CFR_OPTFLAG_SUPPRESS,
			.default_value	= 0,
		},
	};

	/* General Options */
	const struct sm_object general_options[] = {
		SM_DECLARE_ENUM(enum_opts[3]),		// power profile
	};

	/* Device Options */
	const struct sm_object device_options[] = {
		SM_DECLARE_BOOL(bool_opts[1]),		// wireless
		SM_DECLARE_BOOL(bool_opts[2]),		// power_on_after_fail
	};

	/* Chipset Options */
	const struct sm_object chipset_options[] = {
		SM_DECLARE_ENUM(enum_opts[2]),		// me_state
		SM_DECLARE_NUMBER(number_opts[0]),	// me_state_counter
		SM_DECLARE_BOOL(bool_opts[0]),		// vtd
	};

	/* coreboot Options */
	const struct sm_object coreboot_options[] = {
		SM_DECLARE_ENUM(enum_opts[0]),		// boot_option
		SM_DECLARE_ENUM(enum_opts[1]),		// debug_level
		SM_DECLARE_NUMBER(number_opts[1]),	// reboot_counter
	};

	const struct sm_obj_form root_contents[] = {
		{
			.object_id	= byte_get_object_id(),
			.ui_name	= "General Options",
			.obj_list	= general_options,
			.num_objects	= ARRAY_SIZE(general_options),
		},
		{
			.object_id	= byte_get_object_id(),
			.ui_name	= "Devices",
			.obj_list	= device_options,
			.num_objects	= ARRAY_SIZE(device_options),
		},
		{
			.object_id	= byte_get_object_id(),
			.ui_name	= "Chipset",
			.obj_list	= chipset_options,
			.num_objects	= ARRAY_SIZE(chipset_options),
		},
		{
			.object_id	= byte_get_object_id(),
			.ui_name	= "coreboot",
			.obj_list	= coreboot_options,
			.num_objects	= ARRAY_SIZE(coreboot_options),
		},
	};

	const struct setup_menu_root sm_root = {
		.form_list	= root_contents,
		.num_forms	= ARRAY_SIZE(root_contents),
	};

	cfr_set_default_values(&sm_root);
	cfr_write_setup_menu(header, &sm_root);
}

