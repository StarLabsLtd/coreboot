/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GPIO_H_
#define _GPIO_H_

#include "baseboard/variants.h"

#ifndef __ACPI__

#define PAD_CFG_NC(pad) PAD_NC(pad, NONE)

/*
 * All definitions are taken from a comparison of the output of "inteltool -a"
 * using the stock BIOS and with coreboot.
 */

/* Early pad configuration in romstage. */
static const struct pad_config early_gpio_table[] = {
/* Temporary >apollolake_rvp< */
        PAD_CFG_NF(GPIO_46, NATIVE, DEEP, NF1), /* UART2 RX */
        PAD_CFG_NF(GPIO_47, NATIVE, DEEP, NF1), /* UART2 TX */

};

const struct pad_config *variant_early_gpio_table(size_t *num)
{
        *num = ARRAY_SIZE(early_gpio_table);
        return early_gpio_table;
}

/* Pad configuration in ramstage. */
static const struct pad_config gpio_table[] = {

};

const struct pad_config *variant_gpio_table(size_t *num)
{
        *num = ARRAY_SIZE(gpio_table);
        return gpio_table;
}

#endif

#endif
