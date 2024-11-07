/* SPDX-License-Identifier: GPL-2.0-only */

#include <variants.h>

/* Early pad configuration in bootblock */
const struct soc_amd_gpio early_gpio_table[] = {
	PAD_NF(GPIO_141,	UART0_RXD,	PULL_NONE),	// UART0_RXD
	PAD_NF(GPIO_143,	UART0_TXD,	PULL_NONE),	// UART0_TXD

	PAD_NFO(GPIO_26,	PCIE_RST_L,	LOW),		// PLT_RST_N
	PAD_GPO(GPIO_40,			LOW),		// SSD_RST
};

const struct soc_amd_gpio *variant_early_gpio_table(size_t *num)
{
	*num = ARRAY_SIZE(early_gpio_table);
	return early_gpio_table;
}

/* Pad configuration in bootblock */
const struct soc_amd_gpio bootblock_gpio_table[] = {
	PAD_NFO(GPIO_26,	PCIE_RST_L,	HIGH),		// PLT_RST_N
	PAD_GPO(GPIO_40,			HIGH),		// SSD_RST
};

const struct soc_amd_gpio *variant_bootblock_gpio_table(size_t *num)
{
	*num = ARRAY_SIZE(bootblock_gpio_table);
	return bootblock_gpio_table;
}

/* Pad configuration in ramstage. */
const struct soc_amd_gpio gpio_table[] = {
	PAD_NF(GPIO_0,		PWR_BTN_L,	PULL_UP),	// PWR_BTN#_EC
	PAD_NF(GPIO_1,		SYS_RESET_L,	PULL_UP),	// SYS_RST#
	PAD_SCI(GPIO_2,		PULL_NONE,	EDGE_HIGH),	// PCIE_SSD_WAKE_N#
	PAD_GPI(GPIO_3,				PULL_NONE),	// AGPIO3 TPM_DET
	PAD_SCI(GPIO_4,		PULL_DOWN,	EDGE_HIGH),	// N/A
	PAD_NF(GPIO_5,		GPIOxx,		PULL_DOWN),	// SATA_DEVSLP0
	PAD_NF(GPIO_6,		GPIOxx,		PULL_NONE),	// N/A
	PAD_SCI(GPIO_7,		PULL_NONE,	EDGE_HIGH),	// N/A
	PAD_SCI(GPIO_8,		PULL_UP,	EDGE_HIGH),	// EC_SCI#
	PAD_SCI(GPIO_9,		PULL_NONE,	EDGE_LOW),	// TCHPAD_INT_N
	PAD_GPO(GPIO_10,			HIGH),		// S0A3GPIO_R
	PAD_NF(GPIO_11,		GPIOxx,		PULL_NONE),	// N/A
	PAD_GPI(GPIO_12,			PULL_NONE),	// PM_BATLOW_N
	PAD_NF(GPIO_16,		USB_OC0_L,	PULL_UP),	// USB_OCP#
	PAD_SCI(GPIO_17,	PULL_NONE,	EDGE_HIGH),	// N/A
	PAD_SCI(GPIO_18,	PULL_UP,	EDGE_HIGH),	// WLAN_WAKE_N#
	PAD_NF(GPIO_19,		I2C3_SCL,	PULL_NONE),	// I2C3_SCL
	PAD_NF(GPIO_20,		I2C3_SDA,	PULL_NONE),	// I2C3_SDA
	PAD_NF(GPIO_21,		LPC_PD_L,	PULL_NONE),	// N/A
	PAD_NF_SCI(GPIO_22,	LPC_PME_L,	PULL_UP,	EDGE_HIGH),	// LPC_PME#
	PAD_NF(GPIO_23,		AC_PRES,	PULL_UP),	// AC_PRES
	PAD_SCI(GPIO_24,	PULL_NONE,	EDGE_HIGH),	// N/A
	PAD_NF(GPIO_32,		LPC_RST_L,	PULL_UP),	// LPC_RST_L
	PAD_NF(GPIO_42,		GPIOxx,		PULL_UP),	// N/A
	PAD_NF(GPIO_67,		SPI_ROM_REQ,	PULL_NONE),	// N/A
	PAD_NF(GPIO_68,		GPIOxx,		PULL_NONE),	// N/A
	PAD_GPO(GPIO_69,			HIGH),		// BT_RF_KILL_N
	PAD_NF(GPIO_74,		LPCCLK0,	PULL_NONE),	// N/A
	PAD_NF(GPIO_75,		LPCCLK1,	PULL_NONE),	// N/A
	PAD_NF(GPIO_76,		SPI_ROM_GNT,	PULL_NONE),	// N/A
	PAD_NF(GPIO_84,		FANIN0,		PULL_NONE),	// N/A
	PAD_NF(GPIO_85,		FANOUT0,	PULL_NONE),	// N/A
	PAD_NF(GPIO_86,		LPC_SMI_L,	PULL_UP),	// EC_SMI_N
	PAD_NF(GPIO_87,		SERIRQ,		PULL_NONE),	// LPC_SERIRQ
	PAD_NF(GPIO_88,		LPC_CLKRUN_L,	PULL_NONE),	// PM_CLKRUN_N
	PAD_GPI(GPIO_89,			PULL_NONE),	// TOUCH_INT_N
	PAD_GPO(GPIO_90,			HIGH),		// TOUCH_RST_N
	PAD_GPO(GPIO_91,			HIGH),		// WIFI_DISABLE_N
	PAD_NF(GPIO_92,		CLK_REQ0_L,	PULL_UP),	// N/A
	PAD_NF(GPIO_104,	LAD0,		PULL_NONE),	// LAD0
	PAD_NF(GPIO_105,	LAD1,		PULL_NONE),	// LAD1
	PAD_NF(GPIO_106,	LAD2,		PULL_NONE),	// LAD2
	PAD_NF(GPIO_107,	LAD3,		PULL_NONE),	// LAD3
	PAD_GPI(GPIO_108,			PULL_UP),	// AUDIO_INT_N
	PAD_NF(GPIO_109,	LFRAME_L,	PULL_NONE),	// LFRAME_L
	PAD_NF(GPIO_113,	SCL0,		PULL_NONE),	// SCL0
	PAD_NF(GPIO_114,	SDA0,		PULL_NONE),	// SDA0
	PAD_NF(GPIO_115,	CLK_REQ1_L,	PULL_UP),	// PCIE_SSD_CLK_REQ_N
	PAD_NF(GPIO_116,	CLK_REQ2_L,	PULL_UP),	// N/A
	PAD_NF(GPIO_120,	CLK_REQ5_L,	PULL_UP),	// N/A
	PAD_NF(GPIO_121,	CLK_REQ6_L,	PULL_UP),	// WIFI_CLK_REQ
	PAD_NF(GPIO_129,	GPIOxx,		PULL_UP),	// N/A
	PAD_NF(GPIO_130,	SATA_ACT_L,	PULL_UP),	// N/A
	PAD_NF(GPIO_131,	CLK_REQ3_L,	PULL_UP),	// N/A
	PAD_NF(GPIO_132,	CLK_REQ4_L,	PULL_UP),	// WLAN_CLK_REQ
	PAD_NC(GPIO_144),					// N/A
};

const struct soc_amd_gpio *variant_gpio_table(size_t *num)
{
	*num = ARRAY_SIZE(gpio_table);
	return gpio_table;
}
