/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _VARIANT_GPIO_H_
#define _VARIANT_GPIO_H_

#include "baseboard/variants.h"

#ifndef __ACPI__

/*
 * All definitions are taken from a comparison of the output of "inteltool -a"
 * using the stock BIOS and with coreboot.
 */

/* Early pad configuration in romstage.c */
const struct pad_config early_gpio_table[] = {

};

const struct pad_config *variant_early_gpio_table(size_t *num)
{
	*num = ARRAY_SIZE(early_gpio_table);
	return early_gpio_table;
}

/* Pad configuration in ramstage.c */
const struct pad_config gpio_table[] = {

};

const struct pad_config *variant_gpio_table(size_t *num)
{
	*num = ARRAY_SIZE(gpio_table);
	return gpio_table;
}

#endif

#endif
