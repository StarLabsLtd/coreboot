/* SPDX-License-Identifier: GPL-2.0-only */

#include <variants.h>

/* Early pad configuration in bootblock */
const struct soc_amd_gpio early_gpio_table[] = {
	/* Debug Connector */
	PAD_NF(GPIO_141,	UART0_RXD,	PULL_NONE),		// UART0_RXD
	PAD_NF(GPIO_143,	UART0_TXD,	PULL_NONE),		// UART0_TXD
};

const struct soc_amd_gpio *variant_early_gpio_table(size_t *num)
{
	*num = ARRAY_SIZE(early_gpio_table);
	return early_gpio_table;
}

const struct soc_amd_gpio gpio_table[] = {
	/* General Purpose I/O Deep */
	PAD_NF(GPIO_0,		PWR_BTN_L,	PULL_UP),			// PWR_BTN#_EC
	PAD_NF(GPIO_1,		SYS_RESET_L,	PULL_UP),			// SYS_RST#
	PAD_NF_SCI(GPIO_2,	WAKE_L,		PULL_NONE,	EDGE_HIGH),	// PCIE_SSD_WAKE_N#
	PAD_NF(GPIO_5,		DEVSLP0,	PULL_DOWN),			// SSD_DEVSLP0
	PAD_SCI(GPIO_8,		PULL_UP,	EDGE_HIGH),			// EC_SCI_N#
	PAD_NF(GPIO_12,		LLB_L,		PULL_NONE),			// PM_BATLOW_N
	PAD_NF_SCI(GPIO_22,	LPC_PME_L,	PULL_UP,	EDGE_HIGH),	// LPC_PME#
	PAD_NF(GPIO_23,		AC_PRES,	PULL_UP),			// AC_PRESENT
	PAD_GPI(GPIO_144,			PULL_NONE),			// SHUTDOWN_L

	/* Touchpad */
	PAD_SCI(GPIO_9,		PULL_NONE,	LEVEL_LOW),		// TCHPAD_INT_N
	PAD_NF(GPIO_19,		I2C3_SCL,	PULL_NONE),		// TCHPAD_I2C3_SCL
	PAD_NF(GPIO_20,		I2C3_SDA,	PULL_NONE),		// TCHPAD_I2C3_SDA

	/* USB */
	PAD_NF(GPIO_16,		USB_OC0_L,	PULL_UP),			// TYPEC_VBUS_OC
	PAD_NF(GPIO_17,		USB_OC1_L,	PULL_NONE),		// USB_OC1_L

	/* Wireless */
	PAD_SCI(GPIO_18,	PULL_UP,	EDGE_HIGH),		// WLAN_WAKE_N
	PAD_GPO(GPIO_69,			HIGH),			// BT_RF_KILL_N
	PAD_GPO(GPIO_91,			HIGH),			// WIFI_DISABLE_N
	PAD_NF(GPIO_121,	CLK_REQ6_L,	PULL_UP),		// M.2 WLAN CLK_REQ_N

	/* SSD */
	PAD_NFO(GPIO_26,	PCIE_RST_L,	HIGH),			// PCIE_RST_L
	PAD_GPO(GPIO_40,			HIGH),			// SSD_RESET
	PAD_NF(GPIO_115,	CLK_REQ1_L,	PULL_UP),		// PCIE_SSD_CLK_REQ_N
	PAD_NF(GPIO_130,	SATA_ACT_L,	PULL_UP),		// SATA_ACT_L

	/* EC */
	PAD_NF(GPIO_74,		LPCCLK0,	PULL_NONE),		// LPC_CLK_EC
	PAD_NF(GPIO_86,		LPC_SMI_L,	PULL_UP),		// EC_SMI_N

	/* LPC */
	PAD_NF(GPIO_32,		LPC_RST_L,	PULL_UP),			// LPC_RST#
	PAD_NF(GPIO_87,		SERIRQ,		PULL_NONE),		// LPC_SERIRQ
	PAD_NF(GPIO_88,		LPC_CLKRUN_L,	PULL_NONE),		// PM_CLKRUN_N
	PAD_NF(GPIO_104,	LAD0,		PULL_NONE),		// LPC_LAD0
	PAD_NF(GPIO_105,	LAD1,		PULL_NONE),		// LPC_LAD1
	PAD_NF(GPIO_106,	LAD2,		PULL_NONE),		// LPC_LAD2
	PAD_NF(GPIO_107,	LAD3,		PULL_NONE),		// LPC_LAD3
	PAD_NF(GPIO_109,	LFRAME_L,	PULL_NONE),		// LPC_FRAME_N

	/* SMBus */
	PAD_NF(GPIO_113,	SCL0,		PULL_NONE),		// SMB_SCL0
	PAD_NF(GPIO_114,	SDA0,		PULL_NONE),		// SMB_SDA0

	/* Miscellaneous */
	PAD_NF(GPIO_129,	KBRST_L,	PULL_UP),		// KBRST_L

	/* Unused */
	PAD_NC(GPIO_3),
	PAD_NC(GPIO_4),
	PAD_NC(GPIO_6),
	PAD_NC(GPIO_7),
	PAD_NC(GPIO_10),
	PAD_NC(GPIO_11),
	PAD_NC(GPIO_21),
	PAD_NC(GPIO_24),
	PAD_NC(GPIO_42),
	PAD_NC(GPIO_67),
	PAD_NC(GPIO_68),
	PAD_NC(GPIO_84),
	PAD_NC(GPIO_85),
	PAD_NC(GPIO_89),						// Optional touch panel interrupt
	PAD_NC(GPIO_90),
	PAD_NC(GPIO_92),
	PAD_NC(GPIO_108),
	PAD_NC(GPIO_75),
	PAD_NC(GPIO_76),
	PAD_NC(GPIO_116),
	PAD_NC(GPIO_120),
	PAD_NC(GPIO_131),
	PAD_NC(GPIO_132),
};


/* Pad configuration in romstage. */
const struct soc_amd_gpio *variant_gpio_table(size_t *num)
{
	*num = ARRAY_SIZE(gpio_table);
	return gpio_table;
}
