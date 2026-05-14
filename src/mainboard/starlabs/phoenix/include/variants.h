/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _VARIANTS_H_
#define _VARIANTS_H_

#include <gpio.h>
#include <soc/pci_devs.h>

/* This function provides base GPIO configuration table. */
void baseboard_gpio_table(const struct soc_amd_gpio **gpio, size_t *size);

/* This function provides GPIO settings in romstage. */
void baseboard_romstage_gpio_table(const struct soc_amd_gpio **gpio, size_t *size);

/* This function provides GPIO init in bootblock. */
void variant_bootblock_gpio_table(const struct soc_amd_gpio **gpio, size_t *size);

/* This function provides early GPIO init in early bootblock or psp. */
void variant_early_gpio_table(const struct soc_amd_gpio **gpio, size_t *size);

/* This function provides GPIO settings for eSPI bus. */
void variant_espi_gpio_table(const struct soc_amd_gpio **gpio, size_t *size);

/*
 * This function allows variant to override any GPIOs that are different than the base GPIO
 * configuration provided by baseboard_gpio_table().
 */
void variant_override_gpio_table(const struct soc_amd_gpio **gpio, size_t *size);

/* This function provides GPIO settings for TPM i2c bus. */
void variant_tpm_gpio_table(const struct soc_amd_gpio **gpio, size_t *size);

#endif /* _VARIANTS_H_ */
