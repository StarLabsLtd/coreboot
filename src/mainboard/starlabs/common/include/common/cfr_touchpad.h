/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_COMMON_CFR_TOUCHPAD_H
#define MAINBOARD_STARLABS_COMMON_CFR_TOUCHPAD_H

#include <drivers/option/cfr_frontend.h>
#include <common/touchpad.h>
#include <starlabs/efi_option_smi.h>

static const struct sm_object touchpad_haptics = SM_DECLARE_ENUM({
	.flags		= CONFIG(STARLABS_TOUCHPAD_RUNTIME) ?
			  CFR_OPTFLAG_RUNTIME : 0,
	.opt_name	= "touchpad_haptics",
	.ui_name	= "Touchpad Vibration Intensity",
	.ui_helptext	= "Choose how strong the touchpad click vibration feels.",
	.default_value	= STARLABS_TOUCHPAD_HAPTICS_DEFAULT,
	.runtime_apply = {
		.method	= CONFIG(STARLABS_TOUCHPAD_RUNTIME) ?
			  CFR_RUNTIME_APPLY_APM_CNT : CFR_RUNTIME_APPLY_NONE,
		.id	= STARLABS_EFIOPT_ID_TOUCHPAD_HAPTICS,
	},
	.values		= (const struct sm_enum_value[]) {
#if CONFIG(STARLABS_TOUCHPAD_CST)
		{ "Low",	STARLABS_TOUCHPAD_HAPTICS_LOW },
		{ "Medium",	STARLABS_TOUCHPAD_HAPTICS_MEDIUM },
		{ "High",	STARLABS_TOUCHPAD_HAPTICS_HIGH },
		{ "Factory",	STARLABS_TOUCHPAD_HAPTICS_DEFAULT },
		{ "Maximum",	STARLABS_TOUCHPAD_HAPTICS_MAX },
#else
		{ "Off",	STARLABS_TOUCHPAD_HAPTICS_MIN },
		{ "Low",	STARLABS_TOUCHPAD_HAPTICS_LOW },
		{ "Medium",	STARLABS_TOUCHPAD_HAPTICS_MEDIUM },
		{ "High",	STARLABS_TOUCHPAD_HAPTICS_HIGH },
		{ "Maximum",	STARLABS_TOUCHPAD_HAPTICS_MAX },
#endif
		SM_ENUM_VALUE_END,
	},
});

static const struct sm_object touchpad_force_press = SM_DECLARE_ENUM({
	.flags		= CONFIG(STARLABS_TOUCHPAD_RUNTIME) ?
			  CFR_OPTFLAG_RUNTIME : 0,
	.opt_name	= "touchpad_force_press",
	.ui_name	= "Touchpad Click Force",
	.ui_helptext	= "Choose how much force it takes to click the touchpad.",
	.default_value	= STARLABS_TOUCHPAD_PRESS_FORCE_DEFAULT,
	.runtime_apply = {
		.method	= CONFIG(STARLABS_TOUCHPAD_RUNTIME) ?
			  CFR_RUNTIME_APPLY_APM_CNT : CFR_RUNTIME_APPLY_NONE,
		.id	= STARLABS_EFIOPT_ID_TOUCHPAD_FORCE_PRESS,
	},
	.values		= (const struct sm_enum_value[]) {
		{ "Minimal",	STARLABS_TOUCHPAD_PRESS_FORCE_MINIMAL },
		{ "Low",	STARLABS_TOUCHPAD_PRESS_FORCE_LOW },
		{ "Average",	STARLABS_TOUCHPAD_PRESS_FORCE_AVERAGE },
		{ "High",	STARLABS_TOUCHPAD_PRESS_FORCE_HIGH },
		{ "Hulk",	STARLABS_TOUCHPAD_PRESS_FORCE_HULK },
		SM_ENUM_VALUE_END,
	},
});

static const struct sm_object touchpad_force_release = SM_DECLARE_ENUM({
	.flags		= CONFIG(STARLABS_TOUCHPAD_RUNTIME) ?
			  CFR_OPTFLAG_RUNTIME : 0,
	.opt_name	= "touchpad_force_release",
	.ui_name	= "Touchpad Release Force",
	.ui_helptext	= "Choose how much force it takes for the touchpad click to release.",
	.default_value	= STARLABS_TOUCHPAD_RELEASE_FORCE_DEFAULT,
	.runtime_apply = {
		.method	= CONFIG(STARLABS_TOUCHPAD_RUNTIME) ?
			  CFR_RUNTIME_APPLY_APM_CNT : CFR_RUNTIME_APPLY_NONE,
		.id	= STARLABS_EFIOPT_ID_TOUCHPAD_FORCE_RELEASE,
	},
	.values		= (const struct sm_enum_value[]) {
		{ "Minimal",	STARLABS_TOUCHPAD_RELEASE_FORCE_MINIMAL },
		{ "Low",	STARLABS_TOUCHPAD_RELEASE_FORCE_LOW },
		{ "Average",	STARLABS_TOUCHPAD_RELEASE_FORCE_AVERAGE },
		{ "High",	STARLABS_TOUCHPAD_RELEASE_FORCE_HIGH },
		{ "Hulk",	STARLABS_TOUCHPAD_RELEASE_FORCE_HULK },
		SM_ENUM_VALUE_END,
	},
});

static const struct sm_object touchpad_report_rate = SM_DECLARE_ENUM({
	.flags		= CONFIG(STARLABS_TOUCHPAD_RUNTIME) ?
			  CFR_OPTFLAG_RUNTIME : 0,
	.opt_name	= "touchpad_report_rate",
	.ui_name	= "Touchpad Tracking Speed",
	.ui_helptext	= "Choose how quickly the touchpad reports movement.",
	.default_value	= STARLABS_TOUCHPAD_REPORT_RATE_DEFAULT,
	.runtime_apply = {
		.method	= CONFIG(STARLABS_TOUCHPAD_RUNTIME) ?
			  CFR_RUNTIME_APPLY_APM_CNT : CFR_RUNTIME_APPLY_NONE,
		.id	= STARLABS_EFIOPT_ID_TOUCHPAD_REPORT_RATE,
	},
	.values		= (const struct sm_enum_value[]) {
		{ "Relaxed",	STARLABS_TOUCHPAD_RATE_RELAXED },
		{ "Balanced",	STARLABS_TOUCHPAD_RATE_BALANCED },
		{ "Fast",	STARLABS_TOUCHPAD_RATE_FAST },
		{ "Ludicrous",	STARLABS_TOUCHPAD_RATE_LUDICROUS },
		{ "Plaid",	STARLABS_TOUCHPAD_RATE_PLAID },
		SM_ENUM_VALUE_END,
	},
});

#endif /* MAINBOARD_STARLABS_COMMON_CFR_TOUCHPAD_H */
