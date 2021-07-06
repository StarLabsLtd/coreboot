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

/* Baseboard default swizzle. Can be reused if swizzle is same. */
extern const struct lpddr4_swizzle_cfg baseboard_lpddr4_swizzle;
/* Return LPDDR4 configuration structure. */
const struct lpddr4_cfg *variant_lpddr4_config(void);
/* Return memory SKU for the board. */
size_t variant_memory_sku(void);

#endif /* _BASEBOARD_VARIANTS_H_ */
