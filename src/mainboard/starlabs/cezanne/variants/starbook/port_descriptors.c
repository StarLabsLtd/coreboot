/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/cpu.h>
#include <soc/gpio.h>
#include <soc/platform_descriptors.h>
#include <types.h>

static const fsp_dxio_descriptor starbook_dxio_descriptors[] =
{
	/*
	 * Device:		M.2 2230 Wireless
	 */
	{
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 6,
		.end_logical_lane	= 6,
//		.gpio_group_id		= GPIO_34,
		.link_speed_capability	= GEN3,
		.device_number		= 2,
		.function_number	= 4,
		.link_aspm		= 0,	// ASPM_L1,
		.link_aspm_L1_1		= 0,	// true,
		.link_aspm_L1_2		= 0,	// true,
		.turn_off_unused_lanes	= true,
		.clk_req		= CLK_REQ6,
		.port_params		= {PP_PSPP_AC, 0x133, PP_PSPP_DC, 0x122}
	},
	/*
	 * Device:		M.2 2280 SSD
	 */
	{
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 8,
		.end_logical_lane	= 11,
		.gpio_group_id		= GPIO_27,
		.link_speed_capability	= GEN3,
		.device_number		= 2,
		.function_number	= 1,
		.link_aspm		= 0,	// ASPM_L1,
		.link_aspm_L1_1		= 0,	// true,
		.link_aspm_L1_2		= 0,	// true,
		.turn_off_unused_lanes	= true,
		.clk_req		= CLK_REQ1,
		.port_params		= {PP_PSPP_AC, 0x133, PP_PSPP_DC, 0x122}
	},
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
