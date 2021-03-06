/* SPDX-License-Identifier: GPL-2.0-only */

#include "baseboard/variants.h"

/*
 * All definitions are taken from a comparison of the output of "inteltool -a"
 * using the stock BIOS and with coreboot.
 */

/* Early pad configuration in romstage. */
const struct pad_config early_gpio_table[] = {

};

const struct pad_config *variant_early_gpio_table(size_t *num)
{
	*num = ARRAY_SIZE(early_gpio_table);
	return early_gpio_table;
}

/* Pad configuration in ramstage. */
const struct pad_config gpio_table[] = {
	PAD_CFG_NF(GPD0, NONE, PWROK, NF1),
	PAD_CFG_NF(GPD1, NONE, PWROK, NF1),
	_PAD_CFG_STRUCT(GPD2, 0x04000300, 0x1000),
	PAD_CFG_NF(GPD3, UP_20K, PWROK, NF1),
	PAD_CFG_NF(GPD4, NONE, PWROK, NF1),
	PAD_CFG_NF(GPD5, NONE, PWROK, NF1),
	PAD_CFG_NF(GPD6, NONE, PWROK, NF1),
	_PAD_CFG_STRUCT(GPD7, 0x04000101, 0x1000),
	PAD_CFG_NF(GPD8, NONE, PWROK, NF1),
	PAD_CFG_GPI(GPD9, DN_20K, PWROK),
	PAD_CFG_NF(GPD10, DN_20K, PWROK, NF1),
	PAD_CFG_NF(GPD11, DN_20K, PWROK, NF1),
	PAD_NC(GPP_A0, NONE),
	PAD_CFG_NF(GPP_A1, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_A2, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_A3, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_A4, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_A5, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_A6, NONE, DEEP, NF1),
	PAD_NC(GPP_A7, NONE),
	PAD_CFG_NF(GPP_A8, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_A9, DN_20K, DEEP, NF1),
	PAD_NC(GPP_A10, DN_20K),
	PAD_CFG_GPI(GPP_A11, DN_20K, DEEP),
	PAD_NC(GPP_A12, NONE),
	PAD_NC(GPP_A13, DN_20K),
	PAD_NC(GPP_A14, DN_20K),
	PAD_NC(GPP_A15, DN_20K),
	PAD_NC(GPP_A16, DN_20K),
	PAD_NC(GPP_A17, DN_20K),
	PAD_NC(GPP_A18, DN_20K),
	PAD_NC(GPP_A19, DN_20K),
	PAD_NC(GPP_A20, DN_20K),
	PAD_NC(GPP_A21, DN_20K),
	PAD_NC(GPP_A22, DN_20K),
	PAD_NC(GPP_A23, DN_20K),
	PAD_NC(GPP_B0, DN_20K),
	PAD_NC(GPP_B1, DN_20K),
	PAD_NC(GPP_B2, DN_20K),
	PAD_NC(GPP_B3, DN_20K),
	PAD_CFG_TERM_GPO(GPP_B4, 1, UP_20K, DEEP),
	PAD_CFG_NF(GPP_B5, NONE, DEEP, NF1),
	PAD_CFG_GPI(GPP_B6, DN_20K, DEEP),
	PAD_CFG_NF(GPP_B7, DN_20K, DEEP, NF1),
	PAD_CFG_NF(GPP_B8, DN_20K, DEEP, NF1),
	PAD_CFG_NF(GPP_B9, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_B10, DN_20K, DEEP, NF1),
	PAD_NC(GPP_B11, DN_20K),
	PAD_NC(GPP_B12, DN_20K),
	PAD_CFG_NF(GPP_B13, NONE, DEEP, NF1),
	PAD_NC(GPP_B14, DN_20K),
	PAD_NC(GPP_B15, DN_20K),
	PAD_NC(GPP_B16, DN_20K),
	PAD_NC(GPP_B17, DN_20K),
	PAD_NC(GPP_B18, DN_20K),
	PAD_NC(GPP_B19, DN_20K),
	PAD_NC(GPP_B20, DN_20K),
	PAD_NC(GPP_B21, DN_20K),
	PAD_NC(GPP_B22, DN_20K),
	PAD_NC(GPP_B23, DN_20K),
	PAD_CFG_NF(GPP_C0, UP_20K, DEEP, NF1),
	PAD_CFG_NF(GPP_C1, UP_20K, DEEP, NF1),
	PAD_NC(GPP_C2, DN_20K),
	PAD_NC(GPP_C3, DN_20K),
	PAD_NC(GPP_C4, DN_20K),
	PAD_NC(GPP_C5, DN_20K),
	PAD_NC(GPP_C6, DN_20K),
	PAD_NC(GPP_C7, DN_20K),
	PAD_CFG_NF(GPP_C8, UP_20K, DEEP, NF1),
	PAD_CFG_NF(GPP_C9, UP_20K, DEEP, NF1),
	_PAD_CFG_STRUCT(GPP_C10, 0x44000301, 0x3000),
	PAD_CFG_NF(GPP_C11, UP_20K, DEEP, NF1),
	PAD_NC(GPP_C12, UP_20K),
	PAD_NC(GPP_C13, UP_20K),
	PAD_NC(GPP_C14, UP_20K),
	PAD_NC(GPP_C15, UP_20K),
	PAD_CFG_NF(GPP_C16, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_C17, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_C18, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_C19, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_C20, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_C21, NONE, DEEP, NF1),
	PAD_NC(GPP_C22, NONE),
	_PAD_CFG_STRUCT(GPP_C23, 0x80100100, 0x3000),
	PAD_NC(GPP_D0, DN_20K),
	PAD_NC(GPP_D1, DN_20K),
	PAD_NC(GPP_D2, DN_20K),
	PAD_NC(GPP_D3, DN_20K),
	PAD_NC(GPP_D4, DN_20K),
	PAD_NC(GPP_D5, DN_20K),
	PAD_NC(GPP_D6, DN_20K),
	PAD_NC(GPP_D7, DN_20K),
	PAD_NC(GPP_D8, DN_20K),
	PAD_NC(GPP_D9, DN_20K),
	PAD_NC(GPP_D10, DN_20K),
	PAD_NC(GPP_D11, DN_20K),
	PAD_NC(GPP_D12, DN_20K),
	PAD_NC(GPP_D13, DN_20K),
	PAD_NC(GPP_D14, DN_20K),
	PAD_NC(GPP_D15, DN_20K),
	PAD_NC(GPP_D16, DN_20K),
	PAD_NC(GPP_D17, DN_20K),
	PAD_NC(GPP_D18, DN_20K),
	PAD_NC(GPP_D19, DN_20K),
	PAD_CFG_TERM_GPO(GPP_D20, 1, UP_20K, DEEP),
	PAD_NC(GPP_D21, DN_20K),
	PAD_NC(GPP_D22, DN_20K),
	PAD_NC(GPP_D23, DN_20K),
	PAD_NC(GPP_E0, DN_20K),
	PAD_NC(GPP_E1, DN_20K),
	_PAD_CFG_STRUCT(GPP_E2, 0x44000601, 0x0000),
	PAD_NC(GPP_E3, DN_20K),
	PAD_NC(GPP_E4, DN_20K),
	PAD_NC(GPP_E5, DN_20K),
	PAD_CFG_NF(GPP_E6, NONE, PWROK, NF1),
	PAD_NC(GPP_E7, DN_20K),
	PAD_NC(GPP_E8, DN_20K),
	PAD_CFG_NF(GPP_E9, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_E10, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_E11, NONE, DEEP, NF1),
	PAD_NC(GPP_E12, DN_20K),
	PAD_CFG_NF(GPP_E13, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_E14, NONE, DEEP, NF1),
	_PAD_CFG_STRUCT(GPP_E15, 0x42840100, 0x0000),
	_PAD_CFG_STRUCT(GPP_E16, 0x80880100, 0x0000),
	PAD_CFG_NF(GPP_E17, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_E18, NONE, DEEP, NF1),
	PAD_CFG_NF(GPP_E19, DN_20K, DEEP, NF1),
	PAD_NC(GPP_E20, DN_20K),
	PAD_NC(GPP_E21, DN_20K),
	PAD_NC(GPP_E22, DN_20K),
	PAD_NC(GPP_E23, DN_20K),
	PAD_NC(GPP_F0, DN_20K),
	PAD_NC(GPP_F1, DN_20K),
	PAD_NC(GPP_F2, DN_20K),
	PAD_NC(GPP_F3, DN_20K),
	PAD_NC(GPP_F4, DN_20K),
	PAD_NC(GPP_F5, DN_20K),
	PAD_NC(GPP_F6, DN_20K),
	PAD_NC(GPP_F7, DN_20K),
	PAD_CFG_NF(GPP_F8, DN_20K, DEEP, NF1),
	PAD_CFG_NF(GPP_F9, DN_20K, DEEP, NF1),
	PAD_NC(GPP_F10, DN_20K),
	PAD_NC(GPP_F11, DN_20K),
	PAD_NC(GPP_F12, DN_20K),
	PAD_NC(GPP_F13, DN_20K),
	PAD_NC(GPP_F14, DN_20K),
	PAD_NC(GPP_F15, DN_20K),
	PAD_NC(GPP_F16, DN_20K),
	PAD_NC(GPP_F17, DN_20K),
	PAD_NC(GPP_F18, DN_20K),
	PAD_NC(GPP_F19, DN_20K),
	PAD_NC(GPP_F20, DN_20K),
	PAD_NC(GPP_F21, DN_20K),
	PAD_NC(GPP_F22, DN_20K),
	PAD_NC(GPP_F23, DN_20K),
	PAD_CFG_GPI(GPP_G0, DN_20K, DEEP),
	PAD_CFG_GPI(GPP_G1, DN_20K, DEEP),
	PAD_CFG_GPI(GPP_G2, DN_20K, DEEP),
	PAD_CFG_GPI(GPP_G3, DN_20K, DEEP),
	PAD_CFG_GPI(GPP_G4, DN_20K, DEEP),
	PAD_CFG_GPI(GPP_G5, DN_20K, DEEP),
	PAD_CFG_GPI(GPP_G6, DN_20K, DEEP),
	PAD_CFG_GPI(GPP_G7, DN_20K, DEEP),

};

const struct pad_config *variant_gpio_table(size_t *num)
{
	*num = ARRAY_SIZE(gpio_table);
	return gpio_table;
}
