/* SPDX-License-Identifier: GPL-2.0-only */

#include <types.h>
#include <option.h>
#include <chip.h>
#include <device/device.h>
#include <device/pci_def.h>

#include "baseboard/variants.h"

void variant_devtree_update(struct device *nic_dev)
{
	// TODO: TGL needs power_limits_config[POWER_LIMITS_U_2_CORE] and power_limits_config[POWER_LIMITS_U_4_CORE]

	/* Return the correct network device for this platform. */
	nic_dev = pcidev_on_root(0x1d, 5);
}
