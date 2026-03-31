/* SPDX-License-Identifier: GPL-2.0-only */

#include <drivers/option/cfr_frontend.h>
#include <intelblocks/pcie_rp.h>
#include <static.h>
#include <string.h>
#include <common/cfr.h>

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
		return;
	}

	if (strcmp(new_obj->sm_number.opt_name, "pl2_override") == 0) {
		new_obj->sm_number.default_value = bounds.default_pl2;
		new_obj->sm_number.min = bounds.min_pl2;
		new_obj->sm_number.max = bounds.max_pl2;
		new_obj->sm_number.step = 1;
		return;
	}

	if (strcmp(new_obj->sm_number.opt_name, "pl4_override") == 0) {
		new_obj->sm_number.default_value = bounds.default_pl4;
		new_obj->sm_number.min = bounds.min_pl4;
		new_obj->sm_number.max = bounds.max_pl4;
		new_obj->sm_number.step = 1;
		return;
	}

	if (strcmp(new_obj->sm_number.opt_name, "tcc_offset") == 0) {
		new_obj->sm_number.default_value = bounds.default_tcc_offset;
		new_obj->sm_number.min = bounds.min_tcc_offset;
		new_obj->sm_number.max = bounds.max_tcc_offset;
		new_obj->sm_number.step = 1;
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
