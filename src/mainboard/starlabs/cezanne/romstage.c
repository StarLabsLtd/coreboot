/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <soc/platform_descriptors.h>
#include <variants.h>

void mb_pre_fspm(void)
{
	const struct soc_amd_gpio *pads;
	size_t num;

	pads = variant_gpio_table(&num);
	gpio_configure_pads(pads, num);
}
