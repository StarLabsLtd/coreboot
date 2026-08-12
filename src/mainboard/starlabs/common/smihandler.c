/* SPDX-License-Identifier: GPL-2.0-only */

#if CONFIG(STARLABS_TOUCHPAD_RUNTIME)
#include <common/touchpad.h>
#endif
#include <commonlib/helpers.h>
#include <drivers/option/cfr_runtime.h>
#include <ec/acpi/ec.h>
#include <ec/starlabs/merlin/ec.h>
#if CONFIG(STARLABS_AUTOMATIC_START)
#include <intelblocks/pmclib.h>
#endif
#if CONFIG(STARLABS_ACPI_EFI_OPTION_SMI)
#include <acpi/acpi_gnvs.h>
#include <cpu/x86/smm.h>
#if CONFIG(SOC_INTEL_COMMON_BLOCK_FAST_SPI)
#include <cpu/intel/msr.h>
#include <cpu/x86/msr.h>
#include <device/mmio.h>
#include <intelblocks/fast_spi.h>
#endif
#include <soc/nvs.h>
#endif
#include <option.h>
#include <starlabs/efi_option_smi.h>
#include <types.h>
#include "ecdefs.h"

#if CONFIG(STARLABS_ACPI_EFI_OPTION_SMI)
#if CONFIG(SOC_INTEL_COMMON_BLOCK_FAST_SPI)
static void set_insmm_sts(const bool enable_writes)
{
	msr_t msr = {
		.lo = read32p(0xfed30880),
		.hi = 0,
	};

	if (enable_writes)
		msr.lo |= 1;
	else
		msr.lo &= ~1;

	wrmsr(MSR_SPCL_CHIPSET_USAGE, msr);
}
#endif

static bool chipset_disable_wp(void)
{
#if CONFIG(SOC_INTEL_COMMON_BLOCK_FAST_SPI)
	const bool wp_enabled = !fast_spi_wpd_status();

	if (wp_enabled) {
		set_insmm_sts(true);
		/*
		 * As per BWG, clearing "SPI_BIOS_CONTROL_SYNC_SS"
		 * bit is a must prior setting SPI_BIOS_CONTROL_WPD" bit
		 * to avoid 3-strike error.
		 */
		fast_spi_clear_sync_smi_status();
		fast_spi_disable_wp();
	}

	return wp_enabled;
#endif

	return false;
}

static void chipset_enable_wp(void)
{
#if CONFIG(SOC_INTEL_COMMON_BLOCK_FAST_SPI)
	fast_spi_enable_wp();
	set_insmm_sts(false);
#endif
}

static struct starlabs_dnvs_efiopt *get_starlabs_dnvs_efiopt(void)
{
	if (!gnvs)
		return NULL;

	const size_t gnvs_size = ALIGN_UP(sizeof(struct global_nvs), sizeof(uint64_t));

	uint8_t *base = (uint8_t *)gnvs;
	base += gnvs_size;
	return (struct starlabs_dnvs_efiopt *)base;
}
#endif

struct starlabs_efiopt_entry {
	const char *name;
	enum starlabs_efiopt_id id;
	uint32_t fallback;
};

#if CONFIG(STARLABS_TOUCHPAD_RUNTIME)
static bool is_valid_touchpad_haptics(uint32_t value)
{
	return value == STARLABS_TOUCHPAD_HAPTICS_LOW ||
	       value == STARLABS_TOUCHPAD_HAPTICS_MEDIUM ||
	       value == STARLABS_TOUCHPAD_HAPTICS_HIGH ||
	       value == STARLABS_TOUCHPAD_HAPTICS_DEFAULT ||
	       value == STARLABS_TOUCHPAD_HAPTICS_MAX ||
	       (!CONFIG(STARLABS_TOUCHPAD_CST) && value == STARLABS_TOUCHPAD_HAPTICS_MIN);
}

static bool is_valid_touchpad_press_force(uint32_t value)
{
	return value == STARLABS_TOUCHPAD_PRESS_FORCE_MINIMAL ||
	       value == STARLABS_TOUCHPAD_PRESS_FORCE_LOW ||
	       value == STARLABS_TOUCHPAD_PRESS_FORCE_AVERAGE ||
	       value == STARLABS_TOUCHPAD_PRESS_FORCE_HIGH ||
	       value == STARLABS_TOUCHPAD_PRESS_FORCE_HULK;
}

static bool is_valid_touchpad_release_force(uint32_t value)
{
	return value == STARLABS_TOUCHPAD_RELEASE_FORCE_MINIMAL ||
	       value == STARLABS_TOUCHPAD_RELEASE_FORCE_LOW ||
	       value == STARLABS_TOUCHPAD_RELEASE_FORCE_AVERAGE ||
	       value == STARLABS_TOUCHPAD_RELEASE_FORCE_HIGH ||
	       value == STARLABS_TOUCHPAD_RELEASE_FORCE_HULK;
}

#if CONFIG(STARLABS_TOUCHPAD_PIXART)
static bool is_valid_touchpad_report_rate(uint32_t value)
{
	return value == STARLABS_TOUCHPAD_RATE_RELAXED ||
	       value == STARLABS_TOUCHPAD_RATE_BALANCED ||
	       value == STARLABS_TOUCHPAD_RATE_FAST ||
	       value == STARLABS_TOUCHPAD_RATE_LUDICROUS ||
	       value == STARLABS_TOUCHPAD_RATE_PLAID;
}
#endif
#endif

static const struct starlabs_efiopt_entry efiopts[] = {
	{
		.name = "fn_lock_state",
		.id = STARLABS_EFIOPT_ID_FN_LOCK_STATE,
		.fallback = LOCKED,
	},
	{
		.name = "trackpad_state",
		.id = STARLABS_EFIOPT_ID_TRACKPAD_STATE,
		.fallback = TRACKPAD_ENABLED,
	},
	{
		.name = "kbl_brightness",
		.id = STARLABS_EFIOPT_ID_KBL_BRIGHTNESS,
		.fallback = CONFIG(EC_STARLABS_KBL_LEVELS) ? KBL_LOW : KBL_ON,
	},
	{
		.name = "kbl_state",
		.id = STARLABS_EFIOPT_ID_KBL_STATE,
		.fallback = KBL_ENABLED,
	},
	{
		.name = "kbl_timeout",
		.id = STARLABS_EFIOPT_ID_KBL_TIMEOUT,
		.fallback = SEC_30,
	},
	{
		.name = "fn_ctrl_swap",
		.id = STARLABS_EFIOPT_ID_FN_CTRL_SWAP,
		.fallback = FN_CTRL,
	},
#if CONFIG(EC_STARLABS_MAX_CHARGE)
	{
		.name = "max_charge",
		.id = STARLABS_EFIOPT_ID_MAX_CHARGE,
		.fallback = CHARGE_100,
	},
#endif
#if CONFIG(EC_STARLABS_FAN)
	{
		.name = "fan_mode",
		.id = STARLABS_EFIOPT_ID_FAN_MODE,
		.fallback = FAN_NORMAL,
	},
#endif
#if CONFIG(EC_STARLABS_CHARGING_SPEED)
	{
		.name = "charging_speed",
		.id = STARLABS_EFIOPT_ID_CHARGING_SPEED,
		.fallback = SPEED_1_0C,
	},
#endif
#if CONFIG(EC_STARLABS_LID_SWITCH)
	{
		.name = "lid_switch",
		.id = STARLABS_EFIOPT_ID_LID_SWITCH,
		.fallback = SWITCH_NORMAL,
	},
#endif
#if CONFIG(EC_STARLABS_POWER_LED)
	{
		.name = "power_led",
		.id = STARLABS_EFIOPT_ID_POWER_LED,
		.fallback = LED_NORMAL,
	},
#endif
#if CONFIG(EC_STARLABS_CHARGE_LED)
	{
		.name = "charge_led",
		.id = STARLABS_EFIOPT_ID_CHARGE_LED,
		.fallback = LED_NORMAL,
	},
#endif
#if CONFIG(STARLABS_AUTOMATIC_START)
	{
		.name = "automatic_start",
		.id = STARLABS_EFIOPT_ID_AUTOMATIC_START,
		.fallback = AUTOMATIC_START_DEFAULT,
	},
#elif CONFIG(EC_STARLABS_ADAPTER_AUTO_POWER_ON)
	{
		.name = "power_on_ac",
		.id = STARLABS_EFIOPT_ID_POWER_ON_AC,
		.fallback = ADAPTER_AUTO_POWER_ON_DEFAULT,
	},
#endif
#if CONFIG(STARLABS_TOUCHPAD_RUNTIME)
	{
		.name = "touchpad_haptics",
		.id = STARLABS_EFIOPT_ID_TOUCHPAD_HAPTICS,
		.fallback = STARLABS_TOUCHPAD_HAPTICS_DEFAULT,
	},
#if CONFIG(STARLABS_TOUCHPAD_CST)
	{
		.name = "touchpad_force_press",
		.id = STARLABS_EFIOPT_ID_TOUCHPAD_FORCE_PRESS,
		.fallback = STARLABS_TOUCHPAD_PRESS_FORCE_DEFAULT,
	},
	{
		.name = "touchpad_force_release",
		.id = STARLABS_EFIOPT_ID_TOUCHPAD_FORCE_RELEASE,
		.fallback = STARLABS_TOUCHPAD_RELEASE_FORCE_DEFAULT,
	},
#endif
#if CONFIG(STARLABS_TOUCHPAD_PIXART)
	{
		.name = "touchpad_report_rate",
		.id = STARLABS_EFIOPT_ID_TOUCHPAD_REPORT_RATE,
		.fallback = STARLABS_TOUCHPAD_REPORT_RATE_DEFAULT,
	},
#endif
#endif
};

static const struct starlabs_efiopt_entry *find_efiopt(enum starlabs_efiopt_id id)
{
	for (size_t i = 0; i < ARRAY_SIZE(efiopts); i++) {
		if (efiopts[i].id == id)
			return &efiopts[i];
	}

	return NULL;
}

#if CONFIG(STARLABS_ACPI_EFI_OPTION_SMI)
static uint32_t get_supported_efiopts(void)
{
	uint32_t mask = 0;

	for (size_t i = 0; i < ARRAY_SIZE(efiopts); i++)
		mask |= 1U << efiopts[i].id;

	return mask;
}
#endif

static enum cb_err normalize_value(enum starlabs_efiopt_id id, uint32_t *value)
{
	if (!value)
		return CB_ERR_ARG;

	switch (id) {
	case STARLABS_EFIOPT_ID_FN_LOCK_STATE:
		if (*value == UNLOCKED || *value == LOCKED)
			return CB_SUCCESS;
		return CB_ERR_ARG;
	case STARLABS_EFIOPT_ID_TRACKPAD_STATE:
		/* Normalize "re-enabled" to "enabled". */
		if (*value == 0x11)
			*value = TRACKPAD_ENABLED;
		if (*value == TRACKPAD_ENABLED || *value == TRACKPAD_DISABLED)
			return CB_SUCCESS;
		return CB_ERR_ARG;
	case STARLABS_EFIOPT_ID_KBL_BRIGHTNESS:
		if (*value == KBL_ON || *value == KBL_OFF || *value == KBL_LOW ||
		    *value == KBL_HIGH)
			return CB_SUCCESS;
		return CB_ERR_ARG;
	case STARLABS_EFIOPT_ID_KBL_STATE:
		if (*value == KBL_DISABLED || *value == KBL_ENABLED)
			return CB_SUCCESS;
		return CB_ERR_ARG;
	case STARLABS_EFIOPT_ID_KBL_TIMEOUT:
		if (*value == SEC_30 || *value == MIN_1 || *value == MIN_3 ||
		    *value == MIN_5 || *value == NEVER)
			return CB_SUCCESS;
		return CB_ERR_ARG;
	case STARLABS_EFIOPT_ID_FN_CTRL_SWAP:
		if (*value == FN_CTRL || *value == CTRL_FN)
			return CB_SUCCESS;
		return CB_ERR_ARG;
#if CONFIG(EC_STARLABS_MAX_CHARGE)
	case STARLABS_EFIOPT_ID_MAX_CHARGE:
		if (*value == CHARGE_100 || *value == CHARGE_80 || *value == CHARGE_60)
			return CB_SUCCESS;
		return CB_ERR_ARG;
#endif
#if CONFIG(EC_STARLABS_FAN)
	case STARLABS_EFIOPT_ID_FAN_MODE:
		if (*value == FAN_NORMAL || *value == FAN_AGGRESSIVE ||
		    *value == FAN_QUIET || *value == FAN_DISABLED)
			return CB_SUCCESS;
		return CB_ERR_ARG;
#endif
#if CONFIG(EC_STARLABS_CHARGING_SPEED)
	case STARLABS_EFIOPT_ID_CHARGING_SPEED:
		if (*value == SPEED_1_0C || *value == SPEED_0_5C || *value == SPEED_0_2C)
			return CB_SUCCESS;
		return CB_ERR_ARG;
#endif
#if CONFIG(EC_STARLABS_LID_SWITCH)
	case STARLABS_EFIOPT_ID_LID_SWITCH:
		if (*value == SWITCH_NORMAL || *value == SWITCH_SLEEP_ONLY ||
		    *value == SWITCH_DISABLED)
			return CB_SUCCESS;
		return CB_ERR_ARG;
#endif
#if CONFIG(EC_STARLABS_POWER_LED)
	case STARLABS_EFIOPT_ID_POWER_LED:
		if (*value == LED_NORMAL || *value == LED_REDUCED || *value == LED_OFF)
			return CB_SUCCESS;
		return CB_ERR_ARG;
#endif
#if CONFIG(EC_STARLABS_CHARGE_LED)
	case STARLABS_EFIOPT_ID_CHARGE_LED:
		if (*value == LED_NORMAL || *value == LED_REDUCED || *value == LED_OFF)
			return CB_SUCCESS;
		return CB_ERR_ARG;
#endif
#if CONFIG(STARLABS_AUTOMATIC_START)
	case STARLABS_EFIOPT_ID_AUTOMATIC_START:
		if (*value == AUTOMATIC_START_ALWAYS ||
		    *value == AUTOMATIC_START_AFTER_FAILURE ||
		    *value == AUTOMATIC_START_NEVER)
			return CB_SUCCESS;
		return CB_ERR_ARG;
#elif CONFIG(EC_STARLABS_ADAPTER_AUTO_POWER_ON)
	case STARLABS_EFIOPT_ID_POWER_ON_AC:
		if (*value <= 1)
			return CB_SUCCESS;
		return CB_ERR_ARG;
#endif
#if CONFIG(STARLABS_TOUCHPAD_RUNTIME)
	case STARLABS_EFIOPT_ID_TOUCHPAD_HAPTICS:
		return is_valid_touchpad_haptics(*value) ? CB_SUCCESS : CB_ERR_ARG;
#if CONFIG(STARLABS_TOUCHPAD_CST)
	case STARLABS_EFIOPT_ID_TOUCHPAD_FORCE_PRESS:
		return is_valid_touchpad_press_force(*value) ? CB_SUCCESS : CB_ERR_ARG;
	case STARLABS_EFIOPT_ID_TOUCHPAD_FORCE_RELEASE:
		return is_valid_touchpad_release_force(*value) ? CB_SUCCESS : CB_ERR_ARG;
#endif
#if CONFIG(STARLABS_TOUCHPAD_PIXART)
	case STARLABS_EFIOPT_ID_TOUCHPAD_REPORT_RATE:
		return is_valid_touchpad_report_rate(*value) ? CB_SUCCESS : CB_ERR_ARG;
#endif
#endif
	default:
		return CB_ERR_ARG;
	}
}

static enum cb_err get_stored_efiopt_value(enum starlabs_efiopt_id id, uint32_t *value)
{
	const struct starlabs_efiopt_entry *opt = find_efiopt(id);

	if (!opt || !value)
		return CB_ERR_ARG;

	*value = get_uint_option(opt->name, opt->fallback);
	return normalize_value(id, value);
}

#if CONFIG(STARLABS_TOUCHPAD_RUNTIME)
static enum cb_err apply_touchpad_efiopts(void)
{
	uint32_t haptics;
	uint32_t press;
	uint32_t release;
	uint32_t rate = STARLABS_TOUCHPAD_REPORT_RATE_DEFAULT;

	if (get_stored_efiopt_value(STARLABS_EFIOPT_ID_TOUCHPAD_HAPTICS, &haptics))
		return CB_ERR_ARG;

	if (CONFIG(STARLABS_TOUCHPAD_CST)) {
		if (get_stored_efiopt_value(STARLABS_EFIOPT_ID_TOUCHPAD_FORCE_PRESS, &press) ||
		    get_stored_efiopt_value(STARLABS_EFIOPT_ID_TOUCHPAD_FORCE_RELEASE, &release))
			return CB_ERR_ARG;
	} else {
		press = STARLABS_TOUCHPAD_PRESS_FORCE_DEFAULT;
		release = STARLABS_TOUCHPAD_RELEASE_FORCE_DEFAULT;
	}

#if CONFIG(STARLABS_TOUCHPAD_PIXART)
	if (get_stored_efiopt_value(STARLABS_EFIOPT_ID_TOUCHPAD_REPORT_RATE, &rate))
		return CB_ERR_ARG;
#endif

	return starlabs_touchpad_runtime_apply(haptics, press, release, rate);
}
#endif

static enum cb_err apply_ec_value(uint8_t reg, uint32_t value)
{
	if (send_ec_command(WR_EC) || send_ec_data(reg) || send_ec_data(value))
		return CB_ERR;

	return CB_SUCCESS;
}

static enum cb_err apply_runtime_efiopt(enum starlabs_efiopt_id id, uint32_t value)
{
	switch (id) {
	case STARLABS_EFIOPT_ID_FN_LOCK_STATE:
		return apply_ec_value(ECRAM_FN_LOCK_STATE, value);
	case STARLABS_EFIOPT_ID_TRACKPAD_STATE:
		return apply_ec_value(ECRAM_TRACKPAD_STATE, value);
	case STARLABS_EFIOPT_ID_KBL_BRIGHTNESS:
		return apply_ec_value(ECRAM_KBL_BRIGHTNESS, value);
	case STARLABS_EFIOPT_ID_KBL_STATE:
		return apply_ec_value(ECRAM_KBL_STATE, value);
	case STARLABS_EFIOPT_ID_KBL_TIMEOUT:
		return apply_ec_value(ECRAM_KBL_TIMEOUT, value);
	case STARLABS_EFIOPT_ID_FN_CTRL_SWAP:
		return apply_ec_value(ECRAM_FN_CTRL_REVERSE, value);
#if CONFIG(EC_STARLABS_MAX_CHARGE)
	case STARLABS_EFIOPT_ID_MAX_CHARGE:
		return apply_ec_value(ECRAM_MAX_CHARGE, value);
#endif
#if CONFIG(EC_STARLABS_FAN)
	case STARLABS_EFIOPT_ID_FAN_MODE:
		return apply_ec_value(ECRAM_FAN_MODE, value);
#endif
#if CONFIG(EC_STARLABS_CHARGING_SPEED)
	case STARLABS_EFIOPT_ID_CHARGING_SPEED:
		return apply_ec_value(ECRAM_CHARGING_SPEED, value);
#endif
#if CONFIG(EC_STARLABS_LID_SWITCH)
	case STARLABS_EFIOPT_ID_LID_SWITCH:
		return apply_ec_value(ECRAM_LID_SWITCH, value);
#endif
#if CONFIG(EC_STARLABS_POWER_LED)
	case STARLABS_EFIOPT_ID_POWER_LED:
		return apply_ec_value(ECRAM_POWER_LED, value);
#endif
#if CONFIG(EC_STARLABS_CHARGE_LED)
	case STARLABS_EFIOPT_ID_CHARGE_LED:
		return apply_ec_value(ECRAM_CHARGE_LED, value);
#endif
#if CONFIG(STARLABS_AUTOMATIC_START)
	case STARLABS_EFIOPT_ID_AUTOMATIC_START: {
		const enum cb_err ret =
			apply_ec_value(ECRAM_POWER_ON_AC, value == AUTOMATIC_START_ALWAYS);

		if (ret != CB_SUCCESS)
			return ret;

		pmc_set_power_failure_state(true);
		return CB_SUCCESS;
	}
#elif CONFIG(EC_STARLABS_ADAPTER_AUTO_POWER_ON)
	case STARLABS_EFIOPT_ID_POWER_ON_AC:
		return apply_ec_value(ECRAM_POWER_ON_AC, value);
#endif
#if CONFIG(STARLABS_TOUCHPAD_RUNTIME)
	case STARLABS_EFIOPT_ID_TOUCHPAD_HAPTICS:
#if CONFIG(STARLABS_TOUCHPAD_CST)
	case STARLABS_EFIOPT_ID_TOUCHPAD_FORCE_PRESS:
	case STARLABS_EFIOPT_ID_TOUCHPAD_FORCE_RELEASE:
#endif
#if CONFIG(STARLABS_TOUCHPAD_PIXART)
	case STARLABS_EFIOPT_ID_TOUCHPAD_REPORT_RATE:
#endif
		return apply_touchpad_efiopts();
#endif
	default:
		return CB_ERR_ARG;
	}
}

static enum cb_err apply_stored_efiopt(enum starlabs_efiopt_id id)
{
	uint32_t value;
	enum cb_err ret;

	ret = get_stored_efiopt_value(id, &value);
	if (ret != CB_SUCCESS)
		return ret;

	return apply_runtime_efiopt(id, value);
}

enum cb_err cfr_runtime_apply_option(uint32_t id)
{
	return apply_stored_efiopt(id);
}

#if CONFIG(STARLABS_ACPI_EFI_OPTION_SMI)
int mainboard_smi_apmc(u8 data)
{
	if (data != STARLABS_APMC_CMD_EFI_OPTION)
		return 0;

	struct starlabs_dnvs_efiopt *dnvs = get_starlabs_dnvs_efiopt();
	if (!dnvs)
		return 0;

	const enum starlabs_efiopt_cmd cmd = dnvs->cmd;
	const enum starlabs_efiopt_id id = dnvs->id;
	const struct starlabs_efiopt_entry *opt = NULL;

	if (cmd != STARLABS_EFIOPT_CMD_GET_SUPPORTED)
		opt = find_efiopt(id);

	if (cmd != STARLABS_EFIOPT_CMD_GET_SUPPORTED && !opt) {
		dnvs->status = CB_ERR_ARG;
		return 1;
	}

	switch (cmd) {
	case STARLABS_EFIOPT_CMD_GET_SUPPORTED:
		dnvs->value = get_supported_efiopts();
		dnvs->status = CB_SUCCESS;
		break;
	case STARLABS_EFIOPT_CMD_GET:
		dnvs->value = get_uint_option(opt->name, opt->fallback);
		dnvs->status = CB_SUCCESS;
		break;
	case STARLABS_EFIOPT_CMD_SET: {
		uint32_t value = dnvs->value;
		dnvs->status = normalize_value(id, &value);
		if (dnvs->status != CB_SUCCESS)
			break;

		const bool wp_enabled = chipset_disable_wp();
		dnvs->status = set_uint_option(opt->name, value);
		if (wp_enabled)
			chipset_enable_wp();
		if (dnvs->status == CB_SUCCESS)
			dnvs->status = apply_runtime_efiopt(id, value);
		break;
	}
	default:
		dnvs->status = CB_ERR_ARG;
		break;
	}

	return 1;
}
#endif
