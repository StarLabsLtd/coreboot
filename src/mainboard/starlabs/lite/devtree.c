/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/device.h>
#include <device/pci_def.h>
#include <option.h>
#include <types.h>

#include "baseboard/variants.h"
#include "soc/intel/apollolake/chip.h"

struct device *variant_devtree_update(void)
{
	config_t *cfg = config_of_soc();
	struct soc_power_limits_config *soc_conf = &cfg->power_limits_config;

	/* Update PL2 based on CMOS settings */
	switch (get_uint_option("tdp", 0)) {
	case 1:
		soc_conf->tdp_pl1_override = 5;
		soc_conf->tdp_pl2_override = 6;
		break;
	case 2:
		soc_conf->tdp_pl1_override = 8;
		soc_conf->tdp_pl2_override = 9;
		break;
	default:
		soc_conf->tdp_pl1_override = 4;
		soc_conf->tdp_pl2_override = 4;
		break;
	}

	/* Return the correct network device for this platform */
	return pcidev_on_root(0x0c, 0);
}
