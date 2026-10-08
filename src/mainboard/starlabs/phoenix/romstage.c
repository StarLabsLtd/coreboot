/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <amdblocks/gpio.h>
#include <variants.h>
#include <cbmem.h>

static void mainboard_romstage_gpios(int is_recovery)
{
	size_t num_base_gpios;
	const struct soc_amd_gpio *base_gpios;

	baseboard_romstage_gpio_table(&base_gpios, &num_base_gpios);
	gpio_configure_pads(base_gpios, num_base_gpios);
}

CBMEM_CREATION_HOOK(mainboard_romstage_gpios);
