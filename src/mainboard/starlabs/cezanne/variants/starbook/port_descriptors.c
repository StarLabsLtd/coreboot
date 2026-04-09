/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/cpu.h>
#include <console/console.h>
#include <gpio.h>
#include <option.h>
#include <soc/gpio.h>
#include <soc/platform_descriptors.h>
#include <types.h>

enum {
	STARLABS_CFR_ASPM_DISABLE = 1,
	STARLABS_CFR_ASPM_L0S,
	STARLABS_CFR_ASPM_L1,
	STARLABS_CFR_ASPM_L0S_L1,
	STARLABS_CFR_ASPM_AUTO,
};

enum {
	STARLABS_CFR_L1SS_DISABLED = 1,
	STARLABS_CFR_L1SS_L1_1,
	STARLABS_CFR_L1SS_L1_2,
};

enum {
	STARBOOK_DXIO_WIFI_DESC = 4,
	STARBOOK_DXIO_SSD_DESC = 6,
};

static fsp_dxio_descriptor starbook_dxio_descriptors[] =
{
	{ /* Dummy Device */
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 16,
		.end_logical_lane	= 23,
		.port_params		= { PP_PSPP_AC, 0x133, PP_PSPP_DC, 0x122 }
	},
	{ /* Dummy Device */
		.engine_type		= UNUSED_ENGINE,
		.port_present		= false,
		.start_logical_lane	= 0,
		.end_logical_lane	= 1
	},
	{ /* Dummy Device */
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 4,
		.end_logical_lane	= 4,
		.device_number		= 2,
		.function_number	= 2,
		.port_params		= { PP_PSPP_AC, 0x133, PP_PSPP_DC, 0x122 }
	},
	{ /* Dummy Device */
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 5,
		.end_logical_lane	= 5,
		.device_number		= 2,
		.function_number	= 3,
		.port_params		= { PP_PSPP_AC, 0x133, PP_PSPP_DC, 0x122 }
	},
	/*
	 * Device:		M.2 2230 Wireless
	 */
	{
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 6,
		.end_logical_lane	= 6,
		.link_speed_capability	= GEN_MAX,
		.device_number		= 2,
		.function_number	= 4,
		.link_aspm		= ASPM_L1,
		.link_aspm_L1_1		= false,
		.link_aspm_L1_2		= false,
		.turn_off_unused_lanes	= true,
		.clk_req		= CLK_REQ6,
		.port_params		= { PP_PSPP_AC, 0x133, PP_PSPP_DC, 0x122 }
	},
	{ /* Dummy Device */
		.engine_type		= UNUSED_ENGINE,
		.port_present		= false,
		.start_logical_lane	= 7,
		.end_logical_lane	= 7
	},
	/*
	 * Device:		M.2 2280 SSD
	 */
	{
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.gpio_group_id		= 27,
		.start_logical_lane	= 8,
		.end_logical_lane	= 11,
		.link_speed_capability	= GEN_MAX,
		.device_number		= 2,
		.function_number	= 1,
		.link_aspm		= ASPM_L1,
		.link_aspm_L1_1		= true,
		.link_aspm_L1_2		= true,
		.turn_off_unused_lanes	= true,
		.clk_req		= CLK_REQ1,
		.port_params		= { PP_PSPP_AC, 0x133, PP_PSPP_DC, 0x122 }
	},
	/*
	 * Device:		SATA SSD
	 */
	{
		.engine_type		= SATA_ENGINE,
		.start_logical_lane	= 2,
		.end_logical_lane	= 3,
		.gpio_group_id		= 1,
	}
};

static void starbook_set_dxio_aspm(fsp_dxio_descriptor *desc, unsigned int aspm)
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

static void starbook_set_dxio_l1ss(fsp_dxio_descriptor *desc, unsigned int l1ss)
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

static void starbook_update_dxio_power_management(void)
{
	fsp_dxio_descriptor *wifi = &starbook_dxio_descriptors[STARBOOK_DXIO_WIFI_DESC];
	fsp_dxio_descriptor *ssd = &starbook_dxio_descriptors[STARBOOK_DXIO_SSD_DESC];

	if (get_uint_option("wifi", 1) == 0) {
		wifi->engine_type = UNUSED_ENGINE;
		wifi->port_present = false;
	}

	wifi->clk_req = get_uint_option("pciexp_wifi_clk_pm", 1) ? CLK_REQ6 : CLK_ENABLE;
	ssd->clk_req = get_uint_option("pciexp_ssd_clk_pm", 1) ? CLK_REQ1 : CLK_ENABLE;

	starbook_set_dxio_aspm(wifi, get_uint_option("pciexp_wifi_aspm",
						     STARLABS_CFR_ASPM_L1));
	starbook_set_dxio_aspm(ssd, get_uint_option("pciexp_ssd_aspm",
						    STARLABS_CFR_ASPM_L1));

	starbook_set_dxio_l1ss(wifi, get_uint_option("pciexp_wifi_l1ss",
						     STARLABS_CFR_L1SS_DISABLED));
	starbook_set_dxio_l1ss(ssd, get_uint_option("pciexp_ssd_l1ss",
						    STARLABS_CFR_L1SS_L1_2));
}

static void starbook_select_ssd_dxio_descriptor(void)
{
	fsp_dxio_descriptor *ssd = &starbook_dxio_descriptors[STARBOOK_DXIO_SSD_DESC];

	gpio_input(GPIO_4);

	if (gpio_get(GPIO_4)) {
		ssd->engine_type = PCIE_ENGINE;
		ssd->start_logical_lane = 8;
		ssd->end_logical_lane = 11;
		ssd->gpio_group_id = 27;
		ssd->channel_type = SATA_CHANNEL_OTHER;
		printk(BIOS_INFO, "DXIO: detected PCIe SSD on lanes 8-11\n");
		return;
	}

	printk(BIOS_INFO, "DXIO: detected SATA SSD; routing lanes 8-9 to SATA\n");
	ssd->engine_type = SATA_ENGINE;
	ssd->start_logical_lane = 8;
	ssd->end_logical_lane = 9;
	ssd->gpio_group_id = 1;
	ssd->port_present = true;
	ssd->channel_type = SATA_CHANNEL_LONG;
}

static fsp_ddi_descriptor starbook_ddi_descriptors[] = {
	/* DDI0:	eDP */
	{
		.connector_type		= DDI_EDP,
		.aux_index		= DDI_AUX1,
		.hdp_index		= DDI_HDP1
	},
	/* DDI1:	HDMI */
	{
		.connector_type		= DDI_HDMI,
		.aux_index		= DDI_AUX2,
		.hdp_index		= DDI_HDP2
	},
	/* DDI2:	Not Used */
	{
		.connector_type		= DDI_UNUSED_TYPE,
		.aux_index		= DDI_AUX3,
		.hdp_index		= DDI_HDP3,
	},
	/* DDI3:	Not Used */
	{
		.connector_type		= DDI_UNUSED_TYPE,
		.aux_index		= DDI_AUX3,
		.hdp_index		= DDI_HDP3,
	},
	/* DDI4:	Display Port (via Type-C) */
	{
		.connector_type		= DDI_DP,
		.aux_index		= DDI_AUX4,
		.hdp_index		= DDI_HDP4,
	}
};

void mainboard_get_dxio_ddi_descriptors(
		const fsp_dxio_descriptor **dxio_descs, size_t *dxio_num,
		const fsp_ddi_descriptor **ddi_descs, size_t *ddi_num)
{
	starbook_select_ssd_dxio_descriptor();
	starbook_update_dxio_power_management();

	*dxio_descs = starbook_dxio_descriptors;
	*dxio_num = ARRAY_SIZE(starbook_dxio_descriptors);
	*ddi_descs = starbook_ddi_descriptors;
	*ddi_num = ARRAY_SIZE(starbook_ddi_descriptors);
}
