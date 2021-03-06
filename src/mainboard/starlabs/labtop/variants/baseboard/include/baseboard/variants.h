/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _BASEBOARD_VARIANTS_H_
#define _BASEBOARD_VARIANTS_H_

#include <soc/gpio.h>

/*
 * The next set of functions return the gpio table and fill in the number of
 * entries for each table.
 */
const struct pad_config *variant_gpio_table(size_t *num);
const struct pad_config *variant_early_gpio_table(size_t *num);

void devtree_update(void);
struct device *variant_devtree_update(void);

#endif /* _BASEBOARD_VARIANTS_H_ */
