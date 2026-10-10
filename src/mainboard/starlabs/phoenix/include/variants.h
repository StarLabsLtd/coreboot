/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _VARIANTS_H_
#define _VARIANTS_H_

#include <gpio.h>
#include <soc/pci_devs.h>

enum {
	PHX_ASPM_DISABLED = 1,
	PHX_ASPM_L0S,
	PHX_ASPM_L1,
	PHX_ASPM_L0S_L1,
	PHX_ASPM_DEFAULT,
};

enum {
	PHX_L1SS_DISABLED = 1,
	PHX_L1SS_L1_1,
	PHX_L1SS_L1_2,
	PHX_L1SS_DEFAULT,
};

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

#endif /* _VARIANTS_H_ */
