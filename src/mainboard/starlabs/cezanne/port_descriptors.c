/* SPDX-License-Identifier: GPL-2.0-only */

#include <option.h>
#include <soc/amd/cezanne/chip.h>
#include <static.h>
#include <variants.h>

static void cezanne_set_dxio_aspm(dxio_descriptor *desc, unsigned int aspm)
{
	desc->link_aspm = ASPM_L1;

	switch (aspm) {
	case STARLABS_CFR_ASPM_DISABLE:
		desc->link_aspm = ASPM_DISABLED;
		break;
	case STARLABS_CFR_ASPM_L0S:
		desc->link_aspm = ASPM_L0s;
		break;
	case STARLABS_CFR_ASPM_L1:
		desc->link_aspm = ASPM_L1;
		break;
	case STARLABS_CFR_ASPM_L0S_L1:
		desc->link_aspm = ASPM_L0sL1;
		break;
	case STARLABS_CFR_ASPM_AUTO:
	default:
		break;
	}
}

static void cezanne_set_dxio_l1ss(dxio_descriptor *desc, unsigned int l1ss)
{
	desc->link_aspm_L1_1 = true;
	desc->link_aspm_L1_2 = true;

	switch (l1ss) {
	case STARLABS_CFR_L1SS_DISABLED:
		desc->link_aspm_L1_1 = false;
		desc->link_aspm_L1_2 = false;
		break;
	case STARLABS_CFR_L1SS_L1_1:
		desc->link_aspm_L1_1 = true;
		desc->link_aspm_L1_2 = false;
		break;
	case STARLABS_CFR_L1SS_L1_2:
	default:
		break;
	}
}

static void cezanne_set_dxio_clock(dxio_descriptor *desc, enum cpm_clk_req clk_req,
				  bool clock_pm)
{
	desc->clk_req = clock_pm ? clk_req : CLK_DISABLE;

#if ENV_RAMSTAGE
	struct soc_amd_cezanne_config *cfg = config_of_soc();

	if (!desc->port_present)
		cfg->gpp_clk_config[clk_req - CLK_REQ0] = GPP_CLK_OFF;
	else
		cfg->gpp_clk_config[clk_req - CLK_REQ0] = clock_pm ? GPP_CLK_REQ : GPP_CLK_ON;
#endif
}

void mainboard_update_dxio_power_management(dxio_descriptor *wifi, enum cpm_clk_req wifi_clk,
					  dxio_descriptor *ssd, enum cpm_clk_req ssd_clk)
{
	if (get_uint_option_checked("wifi", 1, OPTION_BOOL) == 0) {
		wifi->engine_type = UNUSED_ENGINE;
		wifi->port_present = false;
	}

	cezanne_set_dxio_clock(wifi, wifi_clk,
		get_uint_option_checked("pciexp_wifi_clk_pm", 1, OPTION_BOOL));
	if (ssd->engine_type == PCIE_ENGINE)
		cezanne_set_dxio_clock(ssd, ssd_clk,
			get_uint_option_checked("pciexp_ssd_clk_pm", 1, OPTION_BOOL));

	cezanne_set_dxio_aspm(wifi,
		get_uint_option_checked("pciexp_wifi_aspm", STARLABS_CFR_ASPM_L1,
			OPTION_RANGE(STARLABS_CFR_ASPM_DISABLE, STARLABS_CFR_ASPM_AUTO)));
	if (ssd->engine_type == PCIE_ENGINE)
		cezanne_set_dxio_aspm(ssd,
			get_uint_option_checked("pciexp_ssd_aspm", STARLABS_CFR_ASPM_L1,
				OPTION_RANGE(STARLABS_CFR_ASPM_DISABLE, STARLABS_CFR_ASPM_AUTO)));

	cezanne_set_dxio_l1ss(wifi, STARLABS_CFR_L1SS_DISABLED);
	if (ssd->engine_type == PCIE_ENGINE)
		cezanne_set_dxio_l1ss(ssd,
			get_uint_option_checked("pciexp_ssd_l1ss", STARLABS_CFR_L1SS_L1_2,
				OPTION_RANGE(STARLABS_CFR_L1SS_DISABLED, STARLABS_CFR_L1SS_L1_2)));
}
