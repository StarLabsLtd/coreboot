/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/bsd/clamp.h>
#include <commonlib/helpers.h>
#include <intelblocks/power_limit.h>
#include <option.h>
#include <types.h>
#include <chip.h>
#include <common/powercap.h>

static enum cmos_power_profile get_power_profile(enum cmos_power_profile fallback)
{
	const unsigned int power_profile = get_uint_option("power_profile", fallback);
	return power_profile < NUM_POWER_PROFILES ? power_profile : fallback;
}

static uint16_t round_up_to_5(uint16_t value)
{
	return DIV_ROUND_UP(value, 5) * 5;
}

static uint32_t apply_uplift(uint32_t value, uint32_t uplift_percent)
{
	return MAX(value, (value * (100 + uplift_percent)) / 100);
}

static uint32_t get_pl_uplift_percent(uint32_t stock_pl1)
{
	if (stock_pl1 <= 7)
		return 30;
	if (stock_pl1 <= 15)
		return 25;
	if (stock_pl1 <= 28)
		return 20;

	return 15;
}

bool starlabs_get_power_profile_bounds(const config_t *cfg,
	struct starlabs_power_profile_bounds *bounds)
{
	uint32_t stock_pl1, stock_pl2, stock_pl4, stock_tcc_offset, uplift_percent;
	uint32_t default_pl2, max_pl2;

	if (!cfg || !bounds)
		return false;

	stock_pl1 = get_cpu_tdp();
	if (!stock_pl1)
		return false;
	stock_pl2 = round_up_to_5(stock_pl1 * 2);
	stock_pl4 = CONFIG_MB_STARLABS_PL4_WATTS;
	stock_tcc_offset = CONFIG(EC_STARLABS_FAN) ? 10 : 20;
	uplift_percent = get_pl_uplift_percent(stock_pl1);
	max_pl2 = MIN(apply_uplift(stock_pl2, uplift_percent), stock_pl4);
	default_pl2 = clamp_u32(stock_pl1, stock_pl2, max_pl2);

	bounds->default_pl1 = stock_pl1;
	bounds->min_pl1 = MAX(1U, DIV_ROUND_UP(stock_pl1, 2));
	bounds->max_pl1 = apply_uplift(stock_pl1, uplift_percent);

	bounds->default_pl2 = default_pl2;
	bounds->min_pl2 = stock_pl1;
	bounds->max_pl2 = max_pl2;

	bounds->default_pl4 = stock_pl4;
	bounds->min_pl4 = default_pl2;
	bounds->max_pl4 = stock_pl4;

	bounds->default_tcc_offset = stock_tcc_offset;
	bounds->min_tcc_offset = 0;
	bounds->max_tcc_offset = stock_tcc_offset + 20;

	return true;
}

void update_power_limits(config_t *cfg)
{
	uint8_t performance_scale = 100;
	uint32_t performance_tcc_offset = CONFIG(EC_STARLABS_FAN) ? 10 : 20;
	const enum cmos_power_profile profile = get_power_profile(PP_POWER_SAVER);
	struct starlabs_power_profile_bounds bounds;
	bool have_bounds = starlabs_get_power_profile_bounds(cfg, &bounds);
	uint16_t custom_pl1 = 0, custom_pl2 = 0, custom_pl4 = 0;

	/* Scale PL1 & PL2 based on CMOS settings */
	switch (profile) {
	case PP_POWER_SAVER:
		performance_scale -= 50;
		cfg->tcc_offset = performance_tcc_offset + 20;
		break;
	case PP_BALANCED:
		performance_scale -= 25;
		cfg->tcc_offset = performance_tcc_offset + 10;
		break;
	case PP_PERFORMANCE:
		cfg->tcc_offset = performance_tcc_offset;
		break;
	case PP_CUSTOM:
		if (have_bounds) {
			custom_pl1 = clamp_u32(bounds.min_pl1,
				get_uint_option("pl1_override", bounds.default_pl1),
				bounds.max_pl1);
			custom_pl4 = clamp_u32(bounds.min_pl4,
				get_uint_option("pl4_override", bounds.default_pl4),
				bounds.max_pl4);
			custom_pl2 = clamp_u32(bounds.min_pl2,
				get_uint_option("pl2_override", bounds.default_pl2),
				bounds.max_pl2);
			custom_pl2 = MIN(custom_pl2, custom_pl4);
			custom_pl2 = MAX(custom_pl2, custom_pl1);
			cfg->tcc_offset = clamp_u32(bounds.min_tcc_offset,
				get_uint_option("tcc_offset", bounds.default_tcc_offset),
				bounds.max_tcc_offset);
		} else {
			cfg->tcc_offset = performance_tcc_offset;
		}
		break;
	}

	struct soc_power_limits_config *limits =
		(struct soc_power_limits_config *)&cfg->power_limits_config;
	size_t limit_count =
		sizeof(cfg->power_limits_config) / sizeof(struct soc_power_limits_config);

	for (size_t i = 0; i < limit_count; i++) {
		struct soc_power_limits_config *entry = &limits[i];
		uint16_t tdp, pl1, pl2;

		if (profile == PP_CUSTOM && have_bounds) {
			entry->tdp_pl1_override = custom_pl1;
			entry->tdp_pl2_override = custom_pl2;
			entry->tdp_pl4 = custom_pl4;
			continue;
		}

		entry->tdp_pl4 = (uint16_t)CONFIG_MB_STARLABS_PL4_WATTS;

		tdp = entry->tdp_pl1_override;
		if (!tdp)
			continue;

		pl1 = (tdp * performance_scale) / 100;
		pl2 = round_up_to_5(pl1 * 2);

		entry->tdp_pl1_override = pl1;
		entry->tdp_pl2_override = pl2;
	}
}
