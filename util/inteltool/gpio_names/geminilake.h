#ifndef GPIO_NAMES_GEMINILAKE_H
#define GPIO_NAMES_GEMINILAKE_H

#include "gpio_groups.h"

/*
 * Names prefixed with an *asterisk are the default.
 * (if it's the first column, GPIO is the default, no matter the name)
 */

static const char *const glk_group_northwest_names[] = {
	"*GPIO_0",	"JTAG_TCK",			"N/A",				"N/A",					"N/A",				"N/A",	"N/A",
	"*GPIO_1",	"JTAG_TRST_N",		"N/A",				"N/A",					"N/A",				"N/A",	"N/A",
	"*GPIO_2",	"JTAG_TMS",			"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_3",	"JTAG_TDI",			"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_4",	"JTAG_TDO",			"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_5",	"JTAGX",			"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_6",	"JTAG_PREQ_N",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_7",	"JTAG_PRDY_N",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_8",	"N/A",				"N/A",				"CNV_DEBUG_09",			"CNV_DEBUG_00",		"N/A", 	"N/A",
	"*GPIO_9",	"N/A",				"N/A",				"CNV_DEBUG_10",			"CNV_DEBUG_01",		"N/A", 	"N/A",
	"*GPIO_10",	"N/A",				"N/A",				"CNV_DEBUG_11",			"CNV_DEBUG_02",		"N/A",	"N/A",
	"*GPIO_11",	"N/A",				"N/A",				"CNV_DEBUG_12",			"GNV_DEBUG_03",		"N/A",	"N/A",
	"*GPIO_12",	"N/A",				"N/A",				"GNV_DEBUG_13",			"GNV_DEBUG_04",		"N/A",	"N/A",
	"*GPIO_13",	"N/A",				"N/A", 				"CNV_DEBUG_14",			"CNV_DEBUG_05",		"N/A",	"N/A",
	"*GPIO_14",	"N/A",				"N/A",				"CNV_DEBUG_15",			"CNV_DEBUG_06",		"N/A",	"N/A",
	"*GPIO_15",	"N/A",				"N/A",				"CNV_DEBUG_16",			"CNV_DEBUG_07",		"N/A",	"N/A",
	"*GPIO_16",	"NA",				"N/A",				"CNV_DEBUG_17",			"CNV_DEBUG_08",		"N/A", 	"N/A",
	"*GPIO_17",	"NA",				"CNV_MFUART0_RXD",	"CNV_DEBUG_00",			"N/A",				"N/A", 	"N/A",
	"*GPIO_18",	"NA",				"N/A",				"CNV_MFUART0_TXD",		"CNV_DEBUG_01",		"N/A",	"N/A",
	"*GPIO_19",	"NA",				"N/A",				"CNV_MFUART0_RTS_N",	"CNV_DEBUG_02",		"N/A",	"N/A",
	"*GPIO_20",	"N/A",				"CNV_MFUART0_CTS_N","CNV_DEBUG_03",			"N/A",				"N/A", 	"N/A",
	"*GPIO_21",	"N/A",				"CNV_MFUART2_RXD",	"CNV_DEBUG_04",			"N/A",				"N/A", 	"N/A",
	"*GPIO_22",	"N/A",				"CNV_MFU_ART2_TXD",	"CNV_DEBUG_05",			"N/A",				"N/A", 	"N/A",
	"*GPIO_23",	"N/A",				"CNV_GNSS_PA_BLANKING","CNV_DEBUG_06",		"PMIC_STDBY",		"N/A", 	"N/A",
	"*GPIO_24",	"N/A",				"CNV_GNSS_FTA",		"CNV_DEBUG_07",			"PMIC_PWRGOOD",		"N/A", 	"N/A",
	"*GPIO_25",	"N/A",				"CNV_GNSS_SYSCK",	"CNV_DEBUG_08",			"PMIC_RESET_N",		"N/A", 	"N/A",
	"*GPIO_26",	"ISH_GPIO_0",		"SIO_UART1_RXD",	"ISH_UART1_RXD",		"CNV_BT_UART_RXD",	"N/A",	"N/A",
	"*GPIO_27",	"ISH_GPIO_1",		"SIO_UART1_TXD",	"ISH_UART1_TXD",		"CNV_BT_UART_TXD",	"N/A",	"N/A",
	"*GPIO_28",	"ISH_GPIO_2",		"SIO_UART1_RST_N",	"SIO_UART1_RST_N",		"CNV_BT_UART_RTS_N","N/A",	"N/A",
	"*GPIO_29",	"ISH_GPIO_3",		"SIO_UART1_CTS_N",	"SIO_UART1_CTS_N",		"CNV_BT_UART_CTS_N","N/A",	"N/A",
	"*GPIO_30",	"ISH_GPIO_4",		"SATA_GP0",			"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_31",	"ISH_GPIO_5",		"SATA_GP1",			"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_32",	"ISH_GPIO_6",		"SATA_DEVSLP0",		"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_33",	"ISH_GPIO_7",		"SATA_DEVSLP1",		"SUSCLK1",				"N/A",				"N/A", 	"N/A",
	"*GPIO_34",	"ISH_GPIO_8",		"SATA_LEDN",		"SUSCLK2",				"N/A",				"N/A", 	"N/A",
	"*GPIO_35",	"ISH_GPIO_9",		"N/A",				"N/A",					"SPKR",				"N/A",	"BSSB_CLK",
	"*GPIO_36",	"N/A",				"N/A",				"CNV_BTEN",				"N/A",				"N/A",	"BSSB_DI",
	"*GPIO_37",	"N/A",				"N/A",				"CNV_GNEN",				"N/A",				"N/A",	"N/A",
	"*GPIO_38",	"N/A",				"N/A",				"CNV_WFEN",				"N/A",				"N/A",	"N/A",
	"*GPIO_39",	"N/A",				"N/A",				"CNV_WCEN",				"N/A",				"N/A",	"N/A",
	"*GPIO_40",	"N/A",				"N/A",				"CNV_BT_HOST_WAKE_N",	"N/A",				"N/A",	"N/A",
	"*GPIO_41",	"N/A",				"N/A",				"CNV_GNSS_HOST_WAKEN",	"N/A",				"N/A",	"N/A",
	"*GPIO_42",	"MDSI_A_TE",		"PWM0",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_43",	"MDSI_C_TE",		"PWM1",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_44",	"USB2_OC0_N",		"PWM2",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_45",	"USB2_OC1_N",		"PWM3",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_46",	"MIPI_I2C_SDA",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_47",	"MIPI_I2C_SCL",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_48",	"PMC_I2C_SDA",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_49",	"PMC_I2C_SCL",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_50",	"SIO_I2C0_SDA",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_51",	"SIO_I2C0_SCL",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_52",	"SIO_I2C1_SDA",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_53",	"SIO_I2C1_SCL",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_54",	"SIO_I2C2_SDA",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_55",	"SIO_I2C2_SCL",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_56",	"SIO_I2C3_SDA",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_57",	"SIO_I2C3_SCL",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_58",	"SIO_I2C4_SDA",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_59",	"SIO_I2C4_SCL",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_60",	"SIO_UART0_RXD",	"ISH_UART0_RXD",	"CNV_GNSS_UART_RXD",	"N/A",				"N/A", 	"N/A",
	"*GPIO_61",	"SIO_UART0_TXD",	"ISH_UART0_TXD",	"CNV_GNSS_UART_TXD",	"N/A",				"N/A", 	"N/A",
	"*GPIO_62",	"SIO_UART0_RTS_N",	"ISH_UART0_RTS_N",	"CNV_GNSS_UART_RTS_N",	"N/A",				"N/A", 	"N/A",
	"*GPIO_63",	"SIO_UART0_CTS_N",	"ISH_UART0_CTS_N",	"CNV_GBSS_UART_CTS_N",	"N/A",				"N/A", 	"N/A",
	"*GPIO_64",	"SIO_UART2_RXD",	"ISH_UART2_RXD",	"CNV_MFUA_RT1_RXD",		"N/A",				"N/A", 	"N/A",
	"*GPIO_65",	"SIO_UART2_TXD",	"ISH_UART2_TXD",	"CNV_MFUA_RT1_TXD",		"N/A",				"N/A", 	"N/A",
	"*GPIO_66",	"SIO_UART2_RTS_N",	"ISH_UART2_RTS_N",	"CNV_MFUA_RT1_RTS_N",	"N/A",				"N/A", 	"N/A",
	"*GPIO_67",	"SIO_UART2_CTS_N",	"ISH_UART2_CTS_N",	"CNV_MFUA_RT1_CTS_N",	"N/A",				"N/A", 	"N/A",
	"*GPIO_68",	"PMC_SPI_DS0",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_69",	"PMC_SPI_DS1",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_70",	"PMC_SPI_FS2",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_71",	"PMC_SPI_RXD",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_72",	"PMC_SPI_TXD",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_73",	"PMC_SPI_CLK",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_74",	"THERMTRIP_N",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_75",	"PROCHOT_N",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_211","EMMC_RST_N",		"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_212","N/A",				"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_213","N/A",				"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
	"*GPIO_214","N/A",				"N/A",				"N/A",					"N/A",				"N/A", 	"N/A",
};

static const char *const glk_group_north_names[] = {
	"*GPIO_76",	"SVID0_ALERT_N",	"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_77",	"SCID0_DATA",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_78",	"SVID0_CLK",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_79",	"SIO_SPI_0_CLK",	"ISH_SPI_0_CLK",	"N/A",				"N/A",			"N/A",
	"*GPIO_80",	"SIO_SPI_0_FS0",	"ISH_SPI_0_FS0",	"N/A",				"N/A",			"N/A",
	"*GPIO_81",	"SIO_SPI_0_FS1",	"ISH_SPI_0_FS1",	"FST_SPI_CS2_N",	"N/A",			"N/A",
	"*GPIO_82",	"SIO_SPI_0_RXD",	"ISH_SPI_0_RXD",	"N/A",				"N/A",			"N/A",
	"*GPIO_83",	"SIO_SPI_0_TXD",	"ISH_SPI_0_TXD",	"N/A",				"N/A",			"N/A",
	"*GPIO_84",	"SIO_SPI_2_CLK",	"ISH_SPI_1_CLK",	"TOUCH_SPI_CLK",	"N/A",			"N/A",
	"*GPIO_85",	"SIO_SPI_2_FS0",	"ISH_SPI_1_FS0",	"TOUCH_SPI_FS0",	"N/A",			"N/A",
	"*GPIO_86",	"SIO_SPI_2_FS1",	"ISH_SPI_1_FS1",	"TOUCH_SPI_D0",		"N/A",			"N/A",
	"*GPIO_87",	"SIO_SPI_2_FS2",	"N/A",				"TOUCH_SPI_D1",		"N/A",			"N/A",
	"*GPIO_88",	"SIO_SPI_2_RXD",	"ISH_SPI_1_RXD",	"TOUCH_SPI_D2",		"N/A",			"N/A",
	"*GPIO_89",	"SIO_SPI_2_TXD",	"ISH_SPI_1_TXD",	"TOUCH_SPI_D3",		"N/A",			"N/A",
	"*GPIO_90",	"FST_SPI_CS0_N",	"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_91",	"FST_SPI_CS1_N",	"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_92",	"FST_SPI_MOSI_IO0",	"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_93",	"FST_SPI_MISO_IO1",	"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_94",	"FST_SPI_IO2",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_95",	"FST_SPI_IO_3",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_96",	"FST_SPI_CLK",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_98",	"PMU_PLTRST_N",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_99",	"PMU_PWRBTN_N",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_100","PMU_SLP_S_3_N",	"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_101","PMU_SLP_S_4_N",	"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_102","SUSPWRDNACK",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_103","EMMC_PWR_EN_N",	"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_104","JTAG_TRST_N",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_105","JTAG_TRST_N",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_106","PMU_BATLOW_N",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_107","PMU_RSTBTN_N",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_108","PMU_SUSCLK",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_109","SUS_STAT_N",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_110","SIO_I2C5_SDA",		"ISH_I2C0_SDA",		"N/A",				"N/A",			"N/A",
	"*GPIO_111","SIO_I2C5_SCL",		"ISH_I2C0_SCL",		"N/A",				"N/A",			"N/A",
	"*GPIO_112","SIO_I2C6_SDA",		"ISH_I2C1_SDA",		"N/A",				"N/A",			"N/A",
	"*GPIO_113","SIO_I2C6_SCL",		"ISH_I2C1_SCL",		"N/A",				"N/A",			"N/A",
	"*GPIO_114","SIO_I2C7_SDA",		"ISH_I2C2_SDA",		"N/A",				"N/A",			"N/A",
	"*GPIO_115","SIO_I2C7_SCL",		"ISH_I2C2_SCL",		"N/A",				"N/A",			"N/A",
	"*GPIO_116","PCIE_WAKE0_N",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_117","PCIE_WAKE1_N",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_118","PCIE_WAKE2_N",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_119","PCIE_WAKE3_N",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_120","PCIE_CLKREQ0_N",	"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_121","PCIE_CLKREQ1_N",	"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_122","PCIE_CLKREQ2_N",	"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_123","PCIE_CLKREQ3_N",	"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_124","DDI0_DDC_SDA",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_125","DDI0_DDC_SCL",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_126","DDI1_DDC_SDA",		"SIO_I2C5_SDA",		"N/A",				"N/A",			"N/A",
	"*GPIO_127","DDI1_DDC_SCL",		"SIO_I2C5_SCL",		"N/A",				"N/A",			"N/A",
	"*GPIO_128","PNL0_VDDEN",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_129","PNL0_BKLTEN",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_130","PNL0_BKLCTL",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_131","DDI0_HPD",			"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_132","DDI1_HPD",			"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_133","DDI2_HPD",			"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_134","N/A",				"N/A",				"N/A",				"ISH_GPIO_10",	"N/A",
	"*GPIO_135","N/A",				"N/A",				"N/A",				"ISH_GPIO_11",	"N/A",
	"*GPIO_136","N/A",				"N/A",				"N/A",				"ISH_GPIO_12",	"N/A",
	"*GPIO_137","N/A",				"N/A",				"N/A",				"ISH_GPIO_13",	"N/A",
	"*GPIO_138","N/A",				"SIO_UART3_RXD",	"ISH_UART0_RXD",	"ISH_GPIO_14",	"SATA_GP0",
	"*GPIO_139","N/A",				"SIO_UART3_TXD",	"ISH_UART0_TXD",	"ISH_GPIO_15",	"SATA_GP1",
	"*GPIO_140","N/A",				"SIO_UART3_RTS_N",	"ISH_UART0_RTS_N",	"N/A",			"SATA_DEVSLP0",
	"*GPIO_141","N/A",				"SIO_UART3_CTS_N",	"SIO_UART0_CTS_N",	"N/A",			"SATA_DEVSLP1",
	"*GPIO_142","N/A",				"SIO_SPI_1_CLK",	"ISH_SPI_0_CLK",	"N/A",			"SATA_LE_DN",
	"*GPIO_143","N/A",				"SIO_SPI_1_FS0",	"ISH_SPI_0_FS0",	"JTAG2_TCK",	"N/A",
	"*GPIO_144","N/A",				"SIO_SPI_1_FS1",	"ISH_SPI_0_FS1",	"JTAG2_TDI",	"PNL1_VDDEN",
	"*GPIO_145","N/A",				"SIO_SPI_1_RXD",	"ISH_SPI_0_RXD",	"JTAG2_TMS",	"PNL1_BKLTEN",
	"*GPIO_146","N/A",				"SIO_SPI_1_TXD",	"ISH_SPI_0_TXD",	"JTAG2_TDO",	"PNL1_BKLTCTL",
	"*GPIO_147","LPC_SERIRQ",		"ESPI_RESET_N",		"N/A",				"N/A",			"N/A",
	"*GPIO_148","LPC_CLKOUT0",		"ESPI_CLK",			"N/A",				"N/A",			"N/A",
	"*GPIO_149","LPC_CLKOUT1",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_150","LPC_AD0",			"ESPI_IO_0",		"N/A",				"N/A",			"N/A",
	"*GPIO_151","LPC_AD1",			"ESPI_IO_1",		"N/A",				"N/A",			"N/A",
	"*GPIO_152","LPC_AD2",			"ESPI_IO_2",		"N/A",				"N/A",			"N/A",
	"*GPIO_153","LPC_AD3",			"ESPI_IO_3",		"N/A",				"N/A",			"N/A",
	"*GPIO_154","LPC_CLKRUN_N",		"N/A",				"N/A",				"N/A",			"N/A",
	"*GPIO_155","LPC_FRAME_N",		"ESPI_CS_N",		"N/A",				"N/A",			"N/A",
};

static const char *const glk_group_audio_names[] = {
	"*GPIO_156",	"AVS_I2S0_MCLK",	"N/A",				"N/A",
	"*GPIO_157",	"AVS_I2S0_BCLK",	"N/A",				"N/A",
	"*GPIO_158",	"AVS_I2S0_WS_SYNC",	"N/A",				"N/A",
	"*GPIO_159",	"AVS_I2S0_SDI",		"N/A",				"N/A",
	"*GPIO_160",	"AVS_I2S0_SDO",		"N/A",				"N/A",
	"*GPIO_161",	"AVS_I2S1_MCLK",	"N/A",				"N/A",
	"*GPIO_162",	"AVS_I2S1_BCLK",	"N/A",				"CNV_BT_I2S_BCLK",
	"*GPIO_163",	"AVS_I2S1_WS_SYNC",	"N/A",				"CNV_BT_I2S_WS_SYNC",
	"*GPIO_164",	"AVS_I2S1_SDI",		"N/A",				"CNV_BT_I2S_SD1",
	"*GPIO_165",	"AVS_I2S1_SDO",		"N/A",				"CNV_BT_I2S_SD0",
	"*GPIO_166",	"AVS_HDA_B_CLK",	"AVS_I2S2_BCLK",	"N/A",
	"*GPIO_167",	"AVS_HDA_WS_SYNC",	"AVS_I2S2_WS_SYNC",	"N/A",
	"*GPIO_168",	"AVS_HDS_SDI",		"AVS_I2S2_SDI",		"N/A",
	"*GPIO_169",	"AVS_HDA_SDO",		"AVS_I2S2_SDO",		"N/A",
	"*GPIO_170",	"AVS_HDA_RST_N",	"AVS_I2S1_MCLK",	"N/A",
	"*GPIO_171",	"AVS_DMIC_CLK_A1",	"N/A",				"N/A",
	"*GPIO_172",	"AVS_DMIC_CLK_B1",	"N/A",				"N/A",
	"*GPIO_173",	"AVS_DMIC_DATA_1",	"N/A",				"N/A",
	"*GPIO_174",	"AVS_DMIC_CLK_AB2",	"N/A",				"N/A",
	"*GPIO_175",	"AVS_DMIC_DATA_2",	"N/A",				"N/A",
};

static const char *const glk_group_scc_names[] = {
	"*GPIO_176","SMB_ALERT_N",		"N/A",			"N/A",
	"*GPIO_177","SMB_CLK",			"SIO_I2C7_SCL",	"N/A",
	"*GPIO_178","SMB_DATA",			"SIO_I2C7_SDA",	"N/A",
	"*GPIO_179","SDACARD_CLK",		"N/A",			"N/A",
	"*GPIO_181","SDCARD_D0",		"N/A",			"N/A",
	"*GPIO_182","SDCARD_D1",		"N/A",			"N/A",
	"*GPIO_183","SDCARD_D2",		"N/A",			"N/A",
	"*GPIO_184","SDCARD_D3",		"N/A",			"N/A",
	"*GPIO_185","SDCARD_CMD",		"N/A",			"N/A",
	"*GPIO_186","SDCARD_CD_N",		"N/A",			"N/A",
	"*GPIO_187","SDCARD_LVL_WP",	"N/A",			"N/A",
	"*GPIO_188","SDCARD_PWR_DWN_N",	"N/A",			"N/A",
	"*GPIO_210","N/A",				"N/A",			"N/A",
	"*GPIO_189","OSC_CLK_OUT_0",	"N/A",			"N/A",
	"*GPIO_190","OSC_CLK_OUT_1",	"N/A",			"N/A",
	"*GPIO_191","CNV_BRI_DT",		"N/A",			"SIO_UART1_RTS_N",
	"*GPIO_192","CNV_BRI_RSP",		"N/A",			"SIO_UART1_RXD",
	"*GPIO_193","CNV_RGI_DT",		"N/A",			"SIO_UART1_TXD",
	"*GPIO_194","CNV_RGI_RSP",		"N/A",			"SIO_UART1_CTS_N",
	"*GPIO_195","CNV_RF_RESET_N",	"N/A",			"AVS_I2S1_WS_SYNC",
	"*GPIO_196","XTAL_CLKREQ",		"N/A",			"AVS_I2S1_SDO",
	"*GPIO_198","EMMC_CLK",			"N/A",			"N/A",
	"*GPIO_200","EMMC_D0",			"N/A",			"N/A",
	"*GPIO_201","EMMC_D1",			"N/A",			"N/A",
	"*GPIO_202","EMMC_D2",			"N/A",			"N/A",
	"*GPIO_203","EMMC_D3",			"N/A",			"N/A",
	"*GPIO_204","EMMC_D4",			"N/A",			"N/A",
	"*GPIO_205","EMMC_D5",			"N/A",			"N/A",
	"*GPIO_206","EMMC_D6",			"N/A",			"N/A",
	"*GPIO_207","EMMC_D7",			"N/A",			"N/A",
	"*GPIO_208","EMMC_CMD",			"N/A",			"N/A",
	"*GPIO_209","EMMC_RCLK",		"N/A",			"N/A",
};

static const struct gpio_group glk_group_northwest = {
	.display	= "----- GPIO Group NorthWest -----",
	.pad_count	= ARRAY_SIZE(glk_group_northwest_names) / 7,
	.func_count	= 7,
	.pad_names	= glk_group_north_names,
};

static const struct gpio_group *const glk_community_northwest_groups[] = {
	&glk_group_northwest,
};

static const struct gpio_community glk_community_northwest = {
	.name		= "--- GPIO Community NorthWest ---",
	.pcr_port_id	= 0xc4,
	.group_count	= ARRAY_SIZE(glk_community_northwest_groups),
	.groups		= glk_community_northwest_groups,
};

static const struct gpio_group glk_group_north = {
	.display	= "------- GPIO Group North -------",
	.pad_count	= ARRAY_SIZE(glk_group_north_names) / 6,
	.func_count	= 6,
	.pad_names	= glk_group_north_names,
};

static const struct gpio_group *const glk_community_north_groups[] = {
	&glk_group_north,
};

static const struct gpio_community glk_community_north = {
	.name		= "----- GPIO Community North -----",
	.pcr_port_id	= 0xc5,
	.group_count	= ARRAY_SIZE(glk_community_north_groups),
	.groups		= glk_community_north_groups,
};

static const struct gpio_group glk_group_audio = {
	.display	= "------- GPIO Group Audio -------",
	.pad_count	= ARRAY_SIZE(glk_group_audio_names) / 4,
	.func_count	= 4,
	.pad_names	= glk_group_audio_names,
};

static const struct gpio_group *const glk_community_audio_groups[] = {
	&glk_group_audio,
};

static const struct gpio_community glk_community_audio = {
	.name		= "----- GPIO Community Audio -----",
	.pcr_port_id	= 0xc9,
	.group_count	= ARRAY_SIZE(glk_community_audio_groups),
	.groups		= glk_community_audio_groups,
};

static const struct gpio_group glk_group_scc = {
	.display	= "-------- GPIO Group SCC --------",
	.pad_count	= ARRAY_SIZE(glk_group_scc_names) / 4,
	.func_count	= 4,
	.pad_names	= glk_group_scc_names,
};

static const struct gpio_group *const glk_community_scc_groups[] = {
	&glk_group_scc,
};

static const struct gpio_community glk_community_scc = {
	.name		= "------ GPIO Community SCC ------",
	.pcr_port_id	= 0xc8,
	.group_count	= ARRAY_SIZE(glk_community_scc_groups),
	.groups		= glk_community_scc_groups,
};

static const struct gpio_community *const geminilake_communities[] = {
	&glk_community_northwest, &glk_community_north,
	&glk_community_audio, &glk_community_scc,
};

#endif

