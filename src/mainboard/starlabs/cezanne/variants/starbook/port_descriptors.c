/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/cpu.h>
#include <soc/gpio.h>
#include <soc/platform_descriptors.h>
#include <types.h>

static const fsp_dxio_descriptor starbook_dxio_descriptors[] =
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
		.link_aspm_L1_1		= true,
		.link_aspm_L1_2		= true,
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
	*dxio_descs = starbook_dxio_descriptors;
	*dxio_num = ARRAY_SIZE(starbook_dxio_descriptors);
	*ddi_descs = starbook_ddi_descriptors;
	*ddi_num = ARRAY_SIZE(starbook_ddi_descriptors);
}
