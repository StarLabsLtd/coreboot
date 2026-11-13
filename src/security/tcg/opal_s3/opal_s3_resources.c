/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <bootstate.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <device/device.h>
#include <device/pci.h>
#include <security/tcg/opal_s3.h>

#include "opal_secure.h"

static struct opal_s3_pci_resources pending;

void opal_s3_prepare_pci_resources(struct smm_runtime *runtime)
{
	/* Preserve other users of the shared resource store. */
	memcpy(pending.slots, runtime->pci_resources, sizeof(pending.slots));
	pending.ready = false;
	runtime->opal_resources_pending = &pending;
}

static void store_pci_resources(void *unused)
{
	struct smm_pci_resource_info *slots = pending.slots;
	const size_t count = ARRAY_SIZE(pending.slots);

	if (acpi_is_wakeup_s3())
		return;
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
			return;
		}
		/* Reuse the normal enumerator's identity and assigned resources. */
		if (!smm_pci_resource_store_fill_resources(&slots[slot], 1, &dev, 1))
			return;
	}
	pending.ready = true;
}

/* SMM setup may run during device initialization on other platforms. */
BOOT_STATE_INIT_ENTRY(BS_DEV_INIT, BS_ON_EXIT, store_pci_resources, NULL);
