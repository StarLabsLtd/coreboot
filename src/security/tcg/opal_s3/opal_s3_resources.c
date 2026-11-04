/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <cpu/x86/smm.h>
#include <device/device.h>
#include <device/pci.h>
#include <security/tcg/opal_s3.h>

#include "opal_secure.h"

void opal_s3_store_pci_resources(struct smm_pci_resource_info *slots, size_t count)
{
	for (const struct device *dev = all_devices; dev; dev = dev->next) {
		if (!dev->enabled || !is_pci(dev) || dev->class != OPAL_NVME_CLASS)
			continue;

		size_t slot;
		for (slot = 0; slot < count; slot++) {
			if (!slots[slot].vendor_id || slots[slot].pci_addr == PCI_BDF(dev))
				break;
		}
		if (slot == count) {
			printk(BIOS_ERR, "OPAL: no trusted PCI resource slot for %s\n", dev_path(dev));
			continue;
		}
		/* Reuse the normal enumerator's identity and assigned resources. */
		smm_pci_resource_store_fill_resources(&slots[slot], 1, &dev, 1);
	}
}
