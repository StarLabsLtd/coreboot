/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <amdblocks/gpio.h>
#include <variants.h>
#include <drivers/amd/opensil/opensil.h>

void mainboard_opensil_after_cbmem_init(void)
{
	size_t num_base_gpios;
	const struct soc_amd_gpio *base_gpios;

	baseboard_romstage_gpio_table(&base_gpios, &num_base_gpios);
	gpio_configure_pads(base_gpios, num_base_gpios);
}
