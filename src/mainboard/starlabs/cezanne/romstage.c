/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <console/console.h>
#include <soc/platform_descriptors.h>
#include "soc/amd/common/block/psp/psp_def.h"
#include <variants.h>

void mb_pre_fspm(FSP_M_CONFIG *mcfg)
{
	const struct soc_amd_gpio *pads;
	size_t num;

	pads = variant_gpio_table(&num);
	gpio_configure_pads(pads, num);
}
