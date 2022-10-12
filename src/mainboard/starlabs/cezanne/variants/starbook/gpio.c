/* SPDX-License-Identifier: GPL-2.0-only */

#include <variants.h>

/* Early pad configuration in bootblock */
const struct soc_amd_gpio early_gpio_table[] = {
	/* EGPIO113:	SMB SCL 0			*/
	PAD_NF(GPIO_113, I2C2_SCL, PULL_NONE),
	/* EGPIO114:	SMB SDA 0			*/
	PAD_NF(GPIO_114, I2C2_SDA, PULL_NONE),
	/* EGPIO141:	UART0 RXD			*/
	PAD_NF(GPIO_141, UART0_RXD, PULL_NONE),
	/* EGPIO143:	UART0 TXD			*/
	PAD_NF(GPIO_143, UART0_TXD, PULL_NONE),
};

const struct soc_amd_gpio *variant_early_gpio_table(size_t *num)
{
	*num = ARRAY_SIZE(early_gpio_table);
	return early_gpio_table;
}

/* Pad configuration in ramstage. */
const struct soc_amd_gpio gpio_table[] = {
	/* AGPIO0:	PWR_BTN_L			*/
	PAD_NF(GPIO_0, PWR_BTN_L, PULL_NONE),
	/* AGPIO1:	SYS_RESET_L			*/
	PAD_NF(GPIO_1, SYS_RESET_L, PULL_NONE),
	/* AGPIO2:	WAKE_L				*/
	PAD_NF_SCI(GPIO_2, WAKE_L, PULL_NONE, EDGE_LOW),
	/* AGPIO3:	TPM ID				*/
	PAD_NC(GPIO_3),
	/* AGPIO4:	SSD_DET				*/
	PAD_WAKE(GPIO_4, PULL_NONE, EDGE_HIGH, S0i3),		// ?
	/* AGPIO5:	SSD_DEVSLP0			*/
	PAD_NC(GPIO_5),						// ?
	/* AGPIO6:	Not Connected			*/
	PAD_NC(GPIO_6),
	/* AGPIO7:	Not Connected			*/
	PAD_NC(GPIO_7),
	/* AGPIO8:	Not Connected			*/
	PAD_GPO(GPIO_8, HIGH),
	/* AGPIO9:	Touchpad Enable			*/
	PAD_SCI(GPIO_9, PULL_NONE, EDGE_LOW),
	/* AGPIO10:	S0A3				*/
	PAD_NF(GPIO_10, S0A3, PULL_NONE),
	/* AGPIO11:	Not idea			*/
	PAD_GPO(GPIO_11, LOW),
	/* AGPIO12:	BATLOW				*/
	PAD_GPO(GPIO_12, LOW),
	/* AGPIO16:	USB OC0				*/
	PAD_NF(GPIO_16, USB_OC0_L, PULL_NONE),
	/* AGPIO17:	Not Connected			*/
	PAD_NC(GPIO_17),
	/* AGPIO18:	WLAN WAKE			*/
	PAD_GPO(GPIO_18, HIGH),					// ?
	/* AGPIO19:	I2C 3 SCL (Touchpad)		*/
	PAD_NF(GPIO_19, I2C3_SCL, PULL_NONE),
	/* AGPIO20:	I2C 3 SDA (Touchpad)		*/
	PAD_NF(GPIO_20, I2C3_SDA, PULL_NONE),
	/* AGPIO21:	Not Connected			*/
	PAD_SCI(GPIO_21, PULL_NONE, EDGE_LOW),
	/* AGPIO22:	LPC_PME				*/
	PAD_SCI(GPIO_22, PULL_NONE, EDGE_LOW),
	/* AGPIO34:	AC_PRESENT			*/
	PAD_NF(GPIO_23, AC_PRES, PULL_UP),
	/* AGPIO35:	Not Used (DB LAN WAKE)		*/
	PAD_NC(GPIO_24),
	/* AGPIO26:	Not Connected			*/
	/* AGPIO27:	AUDIO_INT_N			*/
	/* AGPIO29:	SPI0 TPM 1 CS2			*/
	/* AGPIO30:	Not Connected			*/
	/* AGPIO31:	Not Connected			*/
	/* AGPIO32:	LPC_RST				*/
	/* AGPIO40:	SSD_AUX_RESET_L			*/
	/* AGPIO41:	Unavailable			*/
	/* EGPIO42:	Not Connected			*/
	PAD_NC(GPIO_42),
	/* EGPIO67:	Not Connected			*/
	PAD_NC(GPIO_67),
	/* AGPIO68:	Test Point 90			*/
	/* EGPIO69:	BT_RF_KILL			*/
	PAD_GPO(GPIO_69, HIGH),					// ?
	/* EGPIO70:	Test Point 89			*/
	/* EGPIO74:	LPCCLK0				*/
	PAD_NC(GPIO_74),					// ?
	/* EGPIO75:	Not Connected			*/
	PAD_GPI(GPIO_75, PULL_NONE),
	/* EGPIO76:	Test Point 92			*/
	PAD_NC(GPIO_76),
	/* AGPIO84:	Not Connected			*/
	PAD_NC(GPIO_84),
	/* AGPIO85:	Not Connected			*/
	PAD_NC(GPIO_85),
	/* AGPIO86:	EC_SMI_N			*/
	/* AGPIO87:	LPC Serial Interrupt Request	*/
	PAD_GPI(GPIO_87, PULL_NONE),
	/* AGPIO88:	LPC Clock Run			*/
	PAD_GPI(GPIO_88, PULL_NONE),				// ?
	/* AGPIO89:	Touch Panel Interrupt		*/
	PAD_NC(GPIO_89),
	/* AGPIO90:	Touch Panel Reset		*/
	PAD_NC(GPIO_90),
	/* AGPIO91:	WiFi Disable			*/
	PAD_GPI(GPIO_91, PULL_NONE),				// ?
	/* AGPIO92:	Clock Request 0			*/
	PAD_NF(GPIO_92, CLK_REQ0_L, PULL_NONE),
	/* EGPIO104:	LAD 0				*/
	/* EGPIO105:	LAD 1				*/
	/* EGPIO106:	LAD 2				*/
	/* EGPIO107:	LAD 3				*/
	/* EGPIO108:	ESPI Alert			*/
	/* EGPIO109:	LFRAME				*/
	PAD_GPI(GPIO_109, PULL_NONE),				//?
	/* AGPIO115:	Clock Request 1 (SSD)		*/
	PAD_NF(GPIO_115, CLK_REQ1_L, PULL_NONE),
	/* AGPIO116:	Clock Request 2 (Not Used)	*/
	PAD_NC(GPIO_116),
	/* EGPIO120:	Clock Request 5 (Not Used)	*/
	PAD_NC(GPIO_120),
	/* EGPIO121:	Clock Request 6 (Not Used)	*/
	PAD_NC(GPIO_121),
	/* AGPIO129:	Test Point 92			*/
	PAD_NC(GPIO_129),
	/* AGPIO130:	Not Connected			*/
	PAD_NC(GPIO_130),
	/* EGPIO131:	Clock Request 3 (Not Used)	*/
	PAD_NC(GPIO_131),
	/* EGPIO132:	Clock Request 4 (WLAN)		*/
	PAD_GPO(GPIO_132, LOW),
	/* EGPIO140:	Not Connected			*/
	/* EGPIO142:	Test Point 96			*/
	/* EGPIO144:	Not Connected			*/
	PAD_NC(GPIO_144),
	/* EGPIO145:	I2C 0 SCL (Not Used)		*/
	/* EGPIO146:	I2C 0 SDA (Not Used)		*/
	/* EGPIO147:	I2C 1 SCL (Not Used)		*/
	/* EGPIO147:	I2C 1 SDA (Not Used)		*/
};

const struct soc_amd_gpio *variant_gpio_table(size_t *num)
{
	*num = ARRAY_SIZE(gpio_table);
	return gpio_table;
}
