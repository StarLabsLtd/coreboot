/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/cpu.h>
#include <soc/gpio.h>
#include <soc/platform_descriptors.h>
#include <types.h>

static const fsp_dxio_descriptor starbook_dxio_descriptors[] = {
	/*
	 * Device:		M.2 2280 SSD
	 *
	 * Engine:		PCIe Port
	 * Phy Lane:		8 - 11
	 * Hot Plug:		0
	 * GPIO Group:		27
	 * Port Present:	1
	 * Device:		02.1
	 * Link Speed:		0
	 * Link ASPM:		2
	 * SB Link:		0
	 * Misc Control:	0x80
	 * Master PLL:		0
	 *
	 * Engine Type:		SATA Port
	 * Phy Lane:		2 - 3
	 * Hot Plug:		0
	 * Gpio Group:		1
	 */
	{
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 8,
		.end_logical_lane	= 11,
		.link_speed_capability	= GEN3,
		.device_number		= 2,
		.function_number	= 1,
		.link_aspm		= ASPM_L1,
		.link_aspm_L1_1		= true,
		.link_aspm_L1_2		= true,
		.turn_off_unused_lanes	= true,
		.clk_req		= CLK_REQ1,
		.port_params		= {PP_PSPP_AC, 0x133, PP_PSPP_DC, 0x122}
	},
	{
		.engine_type		= SATA_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 2,
		.end_logical_lane	= 3,
		.channel_type		= SATA_CHANNEL_LONG,
	},
	/*
	 * Device:		M.2 2230 Wireless
	 *
	 * Engine:		PCIe Port
	 * Phy Lane:		6
	 * Hot Plug:		0
	 * GPIO Group:		14
	 * Port Present:	1
	 * Device:		02.4
	 * Link Speed:		0
	 * Link ASPM:		2
	 * SB Link:		0
	 * Misc Control:	0x80
	 * Master PLL:		0
	 */
	{
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 6,
		.end_logical_lane	= 6,
		.link_speed_capability	= GEN3,
		.device_number		= 2,
		.function_number	= 4,
		.link_aspm		= ASPM_L1,
		.link_aspm_L1_1		= true,
		.link_aspm_L1_2		= true,
		.turn_off_unused_lanes	= true,
		.clk_req		= CLK_REQ6,
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
