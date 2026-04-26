/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/cpu.h>
#include <console/console.h>
#include <ec/acpi/ec.h>
#include <gpio.h>
#include <soc/gpio.h>
#include <soc/platform_descriptors.h>
#include <types.h>

#define BYTE_EC_GPIO_7_ADDR	0xa7
#define   BYTE_EC_ODD_SSD_SW	BIT(4)
#define   BYTE_EC_SSD_HDD_SW	BIT(5)
#define BYTE_EC_GPIO_8_ADDR	0xa8
#define   BYTE_EC_M2SSD_PWREN	BIT(5)
enum {
	BYTE_DXIO_M2_SSD_DESC = 1,
};

static fsp_dxio_descriptor byte_dxio_descriptors[] = {
	{ /* Dummy Device */
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 16,
		.end_logical_lane	= 23,
		.device_number		= 1,
		.function_number	= 1,
		.clk_req		= CLK_REQ0,
		.port_params		= {PP_PSPP_AC, 0x133, PP_PSPP_DC, 0x122},
	},
	/*
	 * Device:		M.2 2280 SSD
	 *
	 * Engine:		PCIe Port
	 * Phy Lane:		0-3
	 * Port Present:	1
	 * Device:		02.4
	 */
	{
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 0,
		.end_logical_lane	= 3,
		.link_speed_capability	= GEN_MAX,
		.device_number		= 2,
		.function_number	= 4,
		.link_aspm		= ASPM_DISABLED,
		.link_aspm_L1_1		= false,
		.link_aspm_L1_2		= false,
		.turn_off_unused_lanes	= true,
		.clk_req		= CLK_REQ5,
		.port_params		= {PP_PSPP_AC, 0x133, PP_PSPP_DC, 0x122},
	},
	{ /* Unused lane */
		.engine_type		= UNUSED_ENGINE,
		.port_present		= false,
		.start_logical_lane	= 4,
		.end_logical_lane	= 4,
	},
	/*
	 * Device:		Unused DT/LAN1 lane from the AMI table
	 *
	 * Engine:		PCIe Port
	 * Phy Lane:		5
	 * Port Present:	1
	 * Device:		01.2
	 */
	{
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 5,
		.end_logical_lane	= 5,
		.link_speed_capability	= GEN_MAX,
		.device_number		= 1,
		.function_number	= 2,
		.link_aspm		= ASPM_DISABLED,
		.link_aspm_L1_1		= false,
		.link_aspm_L1_2		= false,
		.turn_off_unused_lanes	= true,
		.clk_req		= CLK_REQ4_GFX,
		.port_params		= {PP_PSPP_AC, 0x133, PP_PSPP_DC, 0x122},
	},
	/*
	 * Device:		RJ45 LAN
	 *
	 * Engine:		PCIe Port
	 * Phy Lane:		6
	 * Port Present:	1
	 * Device:		02.1
	 */
	{
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 6,
		.end_logical_lane	= 6,
		.link_speed_capability	= GEN_MAX,
		.device_number		= 2,
		.function_number	= 1,
		.link_aspm		= ASPM_DISABLED,
		.link_aspm_L1_1		= false,
		.link_aspm_L1_2		= false,
		.turn_off_unused_lanes	= true,
		.clk_req		= CLK_REQ1,
		.port_params		= {PP_PSPP_AC, 0x133, PP_PSPP_DC, 0x122},
	},
	/*
	 * Device:		M.2 2230 Wireless
	 *
	 * Engine:		PCIe Port
	 * Phy Lane:		7
	 * Port Present:	1
	 * Device:		02.2
	 */
	{
		.engine_type		= PCIE_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 7,
		.end_logical_lane	= 7,
		.link_speed_capability	= GEN_MAX,
		.device_number		= 2,
		.function_number	= 2,
		.link_aspm		= ASPM_DISABLED,
		.link_aspm_L1_1		= false,
		.link_aspm_L1_2		= false,
		.turn_off_unused_lanes	= true,
		.clk_req		= CLK_REQ6,
		.port_params		= {PP_PSPP_AC, 0x133, PP_PSPP_DC, 0x122},
	},
	/*
	 * Device:		SATA SSD
	 *
	 * Engine:		SATA Port
	 * Phy Lane:		8-9
	 * Port Present:	1
	 */
	{
		.engine_type		= SATA_ENGINE,
		.port_present		= true,
		.start_logical_lane	= 8,
		.end_logical_lane	= 9,
		.gpio_group_id		= 1,
		.channel_type		= SATA_CHANNEL_LONG,
	},
};

static void byte_configure_m2_ssd_mux(bool pcie_ssd)
{
	uint8_t reg;

	reg = ec_read(BYTE_EC_GPIO_8_ADDR);
	reg |= BYTE_EC_M2SSD_PWREN;
	ec_write(BYTE_EC_GPIO_8_ADDR, reg);

	reg = ec_read(BYTE_EC_GPIO_7_ADDR);
	if (pcie_ssd) {
		reg |= BYTE_EC_ODD_SSD_SW;
		reg &= ~BYTE_EC_SSD_HDD_SW;
	} else {
		reg &= ~BYTE_EC_ODD_SSD_SW;
		reg |= BYTE_EC_SSD_HDD_SW;
	}
	ec_write(BYTE_EC_GPIO_7_ADDR, reg);
}

static void byte_select_ssd_dxio_descriptor(void)
{
	fsp_dxio_descriptor *m2_ssd = &byte_dxio_descriptors[BYTE_DXIO_M2_SSD_DESC];

	gpio_input(GPIO_40);

	/*
	 * The Y1 M.2 socket exposes PEDET as "OC-PCIe" with the socket note:
	 * "PCIe SSD: NC / SATA SSD: GND". With the board-side pull-up, PCIe
	 * devices leave the line high while SATA devices pull it low.
	 */
	if (gpio_get(GPIO_40)) {
		byte_configure_m2_ssd_mux(true);
		printk(BIOS_INFO, "DXIO: detected PCIe SSD on lanes 0-3\n");
		return;
	}

	byte_configure_m2_ssd_mux(false);
	printk(BIOS_INFO, "DXIO: detected SATA SSD; routing lanes 2-3 to SATA\n");
	m2_ssd->engine_type = SATA_ENGINE;
	m2_ssd->port_present = true;
	m2_ssd->start_logical_lane = 2;
	m2_ssd->end_logical_lane = 3;
	m2_ssd->gpio_group_id = 1;
	m2_ssd->channel_type = SATA_CHANNEL_LONG;
	m2_ssd->clk_req = CLK_DISABLE;
}

static fsp_ddi_descriptor byte_ddi_descriptors[] = {
	/* DDI0:	HDMI */
	{
		.connector_type		= DDI_HDMI,
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
	/* DDI3:	DisplayPort over USB-C */
	{
		.connector_type		= DDI_DP,
		.aux_index		= DDI_AUX3,
		.hdp_index		= DDI_HDP3,
	},
	/* DDI4:	Not Used */
	{
		.connector_type		= DDI_UNUSED_TYPE,
		.aux_index		= DDI_AUX4,
		.hdp_index		= DDI_HDP4,
	}
};

void mainboard_get_dxio_ddi_descriptors(
		const fsp_dxio_descriptor **dxio_descs, size_t *dxio_num,
		const fsp_ddi_descriptor **ddi_descs, size_t *ddi_num)
{
	byte_select_ssd_dxio_descriptor();

	*dxio_descs = byte_dxio_descriptors;
	*dxio_num = ARRAY_SIZE(byte_dxio_descriptors);
	*ddi_descs = byte_ddi_descriptors;
	*ddi_num = ARRAY_SIZE(byte_ddi_descriptors);
}
