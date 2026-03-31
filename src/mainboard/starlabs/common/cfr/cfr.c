/* SPDX-License-Identifier: GPL-2.0-only */

#include <drivers/option/cfr_frontend.h>
#include <intelblocks/aspm.h>
#include <intelblocks/pcie_rp.h>
#include <stdio.h>
#include <static.h>
#include <string.h>
#include <common/cfr.h>

static char pl1_helptext[160];
static char pl2_helptext[160];
static char pl4_helptext[160];
static char tcc_temp_helptext[192];

static void update_power_limit_helptext(struct sm_object *new_obj,
					const struct starlabs_power_profile_bounds *bounds)
{
	const char *opt_name;
	char *helptext = NULL;
	size_t helptext_size = 0;

	if (!new_obj || !bounds || new_obj->kind != SM_OBJ_NUMBER)
		return;

	opt_name = new_obj->sm_number.opt_name;

	if (strcmp(opt_name, "pl1_override") == 0) {
		helptext = pl1_helptext;
		helptext_size = sizeof(pl1_helptext);
		snprintf(helptext, helptext_size,
			 "Long-duration CPU package power limit in Watts. "
			 "Range: %u-%u W. Default: %u W.",
			 bounds->min_pl1, bounds->max_pl1, bounds->default_pl1);
	} else if (strcmp(opt_name, "pl2_override") == 0) {
		helptext = pl2_helptext;
		helptext_size = sizeof(pl2_helptext);
		snprintf(helptext, helptext_size,
			 "Short-duration CPU package power limit in Watts. "
			 "Range: %u-%u W. Default: %u W. "
			 "Runtime clamped so PL2 never exceeds PL4.",
			 bounds->min_pl2, bounds->max_pl2, bounds->default_pl2);
	} else if (strcmp(opt_name, "pl4_override") == 0) {
		helptext = pl4_helptext;
		helptext_size = sizeof(pl4_helptext);
		snprintf(helptext, helptext_size,
			 "Hard CPU package power limit in Watts. "
			 "Range: %u-%u W. Default: %u W.",
			 bounds->min_pl4, bounds->max_pl4, bounds->default_pl4);
	} else if (strcmp(opt_name, "tcc_temp") == 0) {
		helptext = tcc_temp_helptext;
		helptext_size = sizeof(tcc_temp_helptext);
		snprintf(helptext, helptext_size,
			 "CPU temperature in Celsius where thermal throttling starts. "
			 "Range: %u-%u C. Default: %u C. "
			 "Higher values let the CPU run hotter before throttling.",
			 bounds->min_tcc_temp, bounds->max_tcc_temp,
			 bounds->default_tcc_temp);
	}

	if (helptext)
		new_obj->sm_number.ui_helptext = helptext;
}

void __weak cfr_card_reader_update(struct sm_object *new_obj)
{
	(void)new_obj;
}

void __weak cfr_touchscreen_update(struct sm_object *new_obj)
{
	(void)new_obj;
}

void starlabs_cfr_custom_profile_update(struct sm_object *new_obj)
{
	struct starlabs_power_profile_bounds bounds;

	if (!new_obj || new_obj->kind != SM_OBJ_NUMBER)
		return;

	if (!starlabs_get_power_profile_bounds(config_of_soc(), &bounds))
		return;

	if (strcmp(new_obj->sm_number.opt_name, "pl1_override") == 0) {
		new_obj->sm_number.default_value = bounds.default_pl1;
		new_obj->sm_number.min = bounds.min_pl1;
		new_obj->sm_number.max = bounds.max_pl1;
		new_obj->sm_number.step = 1;
		update_power_limit_helptext(new_obj, &bounds);
		return;
	}

	if (strcmp(new_obj->sm_number.opt_name, "pl2_override") == 0) {
		new_obj->sm_number.default_value = bounds.default_pl2;
		new_obj->sm_number.min = bounds.min_pl2;
		new_obj->sm_number.max = bounds.max_pl2;
		new_obj->sm_number.step = 1;
		update_power_limit_helptext(new_obj, &bounds);
		return;
	}

	if (strcmp(new_obj->sm_number.opt_name, "pl4_override") == 0) {
		new_obj->sm_number.default_value = bounds.default_pl4;
		new_obj->sm_number.min = bounds.min_pl4;
		new_obj->sm_number.max = bounds.max_pl4;
		new_obj->sm_number.step = 1;
		update_power_limit_helptext(new_obj, &bounds);
		return;
	}

	if (strcmp(new_obj->sm_number.opt_name, "tcc_temp") == 0) {
		new_obj->sm_number.default_value = bounds.default_tcc_temp;
		new_obj->sm_number.min = bounds.min_tcc_temp;
		new_obj->sm_number.max = bounds.max_tcc_temp;
		new_obj->sm_number.step = 1;
		update_power_limit_helptext(new_obj, &bounds);
	}
}

static const struct cfr_default_override starlabs_cfr_overrides[] = {
	CFR_OVERRIDE_ENUM("pciexp_aspm", ASPM_L0S_L1),
	CFR_OVERRIDE_END
};

void starlabs_cfr_register_overrides(void)
{
	if (!CONFIG(DRIVERS_OPTION_CFR))
		return;
	cfr_register_overrides(starlabs_cfr_overrides);
}

static void set_pcie_pm_option_names(struct pcie_pm_option_names *names,
				     const char *clk_pm,
				     const char *aspm,
				     const char *l1ss)
{
	if (!names)
		return;

	names->clk_pm = clk_pm;
	names->aspm = aspm;
	names->l1ss = l1ss;
}

void mainboard_get_pcie_pm_options(const struct pcie_rp_config *rp_cfg,
				   unsigned int index,
				   bool is_cpu_rp,
				   struct pcie_pm_option_names *names)
{
	(void)rp_cfg;

	set_pcie_pm_option_names(names, NULL, NULL, NULL);

	if (is_cpu_rp) {
#if CONFIG(BOARD_STARLABS_STARBOOK_RPL) || CONFIG(BOARD_STARLABS_STARFIGHTER_RPL)
		if (index == CPU_RP(1))
			set_pcie_pm_option_names(names, "pciexp_ssd_clk_pm",
						 "pciexp_ssd_aspm",
						 "pciexp_ssd_l1ss");
#endif
		return;
	}

#if CONFIG(BOARD_STARLABS_ADL_HORIZON) || CONFIG(BOARD_STARLABS_LITE_ADL)
	if (index == PCH_RP(9))
		set_pcie_pm_option_names(names, "pciexp_ssd_clk_pm",
					 "pciexp_ssd_aspm",
					 "pciexp_ssd_l1ss");
#elif CONFIG(BOARD_STARLABS_BYTE_ADL) || CONFIG(BOARD_STARLABS_BYTE_TWL)
	switch (index) {
	case PCH_RP(9):
		set_pcie_pm_option_names(names, "pciexp_lan1_clk_pm",
					 "pciexp_lan1_aspm",
					 "pciexp_lan1_l1ss");
		return;
	case PCH_RP(10):
		set_pcie_pm_option_names(names, "pciexp_lan2_clk_pm",
					 "pciexp_lan2_aspm",
					 "pciexp_lan2_l1ss");
		return;
	case PCH_RP(12):
		set_pcie_pm_option_names(names, "pciexp_ssd_clk_pm",
					 "pciexp_ssd_aspm",
					 "pciexp_ssd_l1ss");
		return;
	}
#elif CONFIG(BOARD_STARLABS_STARBOOK_ADL)
	switch (index) {
	case PCH_RP(5):
		set_pcie_pm_option_names(names, "pciexp_wifi_clk_pm",
					 "pciexp_wifi_aspm",
					 "pciexp_wifi_l1ss");
		return;
	case PCH_RP(9):
		set_pcie_pm_option_names(names, "pciexp_ssd_clk_pm",
					 "pciexp_ssd_aspm",
					 "pciexp_ssd_l1ss");
		return;
	}
#elif CONFIG(BOARD_STARLABS_STARBOOK_ADL_N)
	switch (index) {
	case PCH_RP(7):
		set_pcie_pm_option_names(names, "pciexp_wifi_clk_pm",
					 "pciexp_wifi_aspm",
					 "pciexp_wifi_l1ss");
		return;
	case PCH_RP(9):
		set_pcie_pm_option_names(names, "pciexp_ssd_clk_pm",
					 "pciexp_ssd_aspm",
					 "pciexp_ssd_l1ss");
		return;
	}
#elif CONFIG(BOARD_STARLABS_STARBOOK_MTL)
	switch (index) {
	case PCH_RP(9):
		set_pcie_pm_option_names(names, "pciexp_wifi_clk_pm",
					 "pciexp_wifi_aspm",
					 "pciexp_wifi_l1ss");
		return;
	case PCH_RP(10):
		set_pcie_pm_option_names(names, "pciexp_ssd_clk_pm",
					 "pciexp_ssd_aspm",
					 "pciexp_ssd_l1ss");
		return;
	}
#elif CONFIG(BOARD_STARLABS_STARBOOK_RPL)
	switch (index) {
	case PCH_RP(5):
		set_pcie_pm_option_names(names, "pciexp_wifi_clk_pm",
					 "pciexp_wifi_aspm",
					 "pciexp_wifi_l1ss");
		return;
	}
#elif CONFIG(BOARD_STARLABS_STARFIGHTER_RPL)
	switch (index) {
	case PCH_RP(5):
		set_pcie_pm_option_names(names, "pciexp_wifi_clk_pm",
					 "pciexp_wifi_aspm",
					 "pciexp_wifi_l1ss");
		return;
	case PCH_RP(9):
		set_pcie_pm_option_names(names, "pciexp_ssd2_clk_pm",
					 "pciexp_ssd2_aspm",
					 "pciexp_ssd2_l1ss");
		return;
	}
#elif CONFIG(BOARD_STARLABS_STARFIGHTER_MTL)
	switch (index) {
	case PCH_RP(9):
		set_pcie_pm_option_names(names, "pciexp_wifi_clk_pm",
					 "pciexp_wifi_aspm",
					 "pciexp_wifi_l1ss");
		return;
	case PCH_RP(10):
		set_pcie_pm_option_names(names, "pciexp_ssd_clk_pm",
					 "pciexp_ssd_aspm",
					 "pciexp_ssd_l1ss");
		return;
	case PCH_RP(1):
		set_pcie_pm_option_names(names, "pciexp_ssd2_clk_pm",
					 "pciexp_ssd2_aspm",
					 "pciexp_ssd2_l1ss");
		return;
	}
#endif
}
