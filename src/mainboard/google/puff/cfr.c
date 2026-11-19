/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <baseboard/board.h>
#include <drivers/intel/oc_mailbox/cfr.h>
#include <drivers/option/cfr_frontend.h>
#include <intelblocks/cfr.h>
#include <mainboard/rapl_options.h>
#include <soc/cfr.h>

static const struct sm_object tdp_pl1_override = SM_DECLARE_NUMBER({
	.opt_name	= "tdp_pl1_override",
	.ui_name	= "CPU PL1 power limit (W)",
	.ui_helptext	= "Long-duration CPU package power limit.",
	.default_value	= 15,
	.min		= RAPL_PL1_MIN,
	.max		= RAPL_PL1_MAX,
	.step		= RAPL_PL1_STEP,
	.display_flags	= 0,
});

static void update_tdp_pl2_default(struct sm_object *new)
{
	new->sm_number.default_value = puff_default_pl2_watts();
}

static const struct sm_object tdp_pl2_override = SM_DECLARE_NUMBER({
	.opt_name	= "tdp_pl2_override",
	.ui_name	= "CPU PL2 power limit (W)",
	.ui_helptext	= "Short-duration CPU package power limit.\n"
			  "Must be >= PL1 (enforced at boot).",
	.default_value	= 35,
	.min		= RAPL_PL2_MIN,
	.max		= RAPL_PL2_MAX,
	.step		= RAPL_PL2_STEP,
	.display_flags	= 0,
}, WITH_CALLBACK(update_tdp_pl2_default));

static struct sm_obj_form system = {
	.ui_name = "System",
	.obj_list = (const struct sm_object *[]) {
		&hyper_threading,
		&igd_dvmt,
		&igd_aperture,
		&legacy_8254_timer,
		&me_state,
		&me_state_counter,
		&disable_heci1_at_pre_boot,
		&pciexp_aspm,
		&pciexp_clk_pm,
		&pciexp_l1ss,
		&pciexp_speed,
		&s0ix_enable,
		&vtd,
		&bios_lock,
		NULL
	},
};

static struct sm_obj_form power = {
	.ui_name = "Power",
	.obj_list = (const struct sm_object *[]) {
		&power_on_after_fail,
		&tdp_pl1_override,
		&tdp_pl2_override,
		&pkg_power_limit_lock,
		NULL
	},
};

static struct sm_obj_form *sm_root[] = {
	&system,
	&power,
#if CONFIG(INTEL_OC_MAILBOX_ENABLE_UNDERVOLTING)
	&cpu_voltage,
#endif
	NULL
};

void mb_cfr_setup_menu(struct lb_cfr *cfr_root)
{
	cfr_write_setup_menu(cfr_root, sm_root);
}
