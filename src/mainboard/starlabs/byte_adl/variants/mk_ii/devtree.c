/* SPDX-License-Identifier: GPL-2.0-only */

#include <chip.h>
#include <cpu/intel/turbo.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <option.h>
#include <static.h>
#include <types.h>
#include <variants.h>

#define TJ_MAX		105
#define TCC(temp)	(TJ_MAX - temp)

void devtree_update(void)
{
	config_t *cfg = config_of_soc();

	struct device *nic_dev = pcidev_on_root(0x14, 3);
	struct device *gna_dev = pcidev_on_root(0x08, 0);

	update_power_limits(cfg);

	/* Enable/Disable Bluetooth based on CMOS settings */
	if (get_uint_option("wireless", 1) == 0) {
		cfg->usb2_ports[9].enable = 0;
		nic_dev->enabled = 0;
	}

	/* Enable/Disable GNA based on CMOS settings */
	if (get_uint_option("gna", 0) == 0)
		gna_dev->enabled = 0;
}
