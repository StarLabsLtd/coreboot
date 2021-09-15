/* SPDX-License-Identifier: GPL-2.0-only */

#include <chip.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <option.h>
#include <types.h>

#include "baseboard/variants.h"

struct device *variant_devtree_update(void)
{
	config_t *cfg = config_of_soc();
	struct soc_power_limits_config *soc_conf = &cfg->power_limits_config;

	/* Update PL2 based on CMOS settings */
	switch (get_uint_option("tdp", 0)) {
	case 1:
		soc_conf->tdp_pl1_override = 17;
		soc_conf->tdp_pl2_override = 20;
		break;
	case 2:
		soc_conf->tdp_pl1_override = 20;
		soc_conf->tdp_pl2_override = 25;
		break;
	default:
		soc_conf->tdp_pl1_override = 15;
		soc_conf->tdp_pl2_override = 15;
		break;
	}

	/* Return the correct network device for this platform. */
	return pcidev_on_root(0x14, 3);
}
