/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <console/console.h>
#include <device/device.h>
#include <soc/pci_devs.h>
#include <stdint.h>

#include "payload_resource_policy.h"

#define STARBOOK_MTL_BOOT_PRIORITY_NVME 10U
#define STARBOOK_MTL_BOOT_PRIORITY_PCH_XHCI 20U
#define STARBOOK_MTL_BOOT_PRIORITY_TCSS_XHCI 30U

_Static_assert(STARBOOK_MTL_RP10_DEVFN == PCI_DEVFN_PCIE10,
	"StarBook MTL RP10 policy must match the SoC topology");
_Static_assert(STARBOOK_MTL_PCH_XHCI_DEVFN == PCI_DEVFN_XHCI,
	"StarBook MTL PCH xHCI policy must match the SoC topology");
_Static_assert(STARBOOK_MTL_TCSS_XHCI_DEVFN == PCI_DEVFN_TCSS_XHCI,
	"StarBook MTL TCSS xHCI policy must match the SoC topology");

static const struct device *boot_controllers[STARBOOK_MTL_BOOT_CONTROLLER_COUNT];
static bool inventory_scanned;
static bool inventory_valid;

static enum starbook_mtl_boot_controller classify_device(const struct device *device)
{
	const struct device *parent;
	struct starbook_mtl_pci_identity identity;

	if (!device || !device->enabled || device->path.type != DEVICE_PATH_PCI ||
	    !device->upstream || device->upstream->secondary > UINT8_MAX ||
	    device->path.pci.devfn > UINT8_MAX)
		return STARBOOK_MTL_BOOT_CONTROLLER_NONE;

	parent = device->upstream->dev;
	identity = (struct starbook_mtl_pci_identity) {
		.class = device->class,
		.segment = device->upstream->segment_group,
		.bus = device->upstream->secondary,
		.devfn = device->path.pci.devfn,
	};
	if (parent && parent->path.type == DEVICE_PATH_PCI && parent->upstream &&
	    parent->upstream->secondary <= UINT8_MAX &&
	    parent->path.pci.devfn <= UINT8_MAX) {
		identity.parent_is_pci = true;
		identity.parent_segment = parent->upstream->segment_group;
		identity.parent_bus = parent->upstream->secondary;
		identity.parent_devfn = parent->path.pci.devfn;
	}
	return starbook_mtl_boot_controller_kind(&identity);
}

static bool scan_boot_controllers(void)
{
	const struct device *device;
	size_t counts[STARBOOK_MTL_BOOT_CONTROLLER_COUNT] = { 0 };

	for (device = all_devices; device; device = device->next) {
		enum starbook_mtl_boot_controller kind = classify_device(device);

		if (kind == STARBOOK_MTL_BOOT_CONTROLLER_NONE)
			continue;
		counts[kind]++;
		if (counts[kind] == 1)
			boot_controllers[kind] = device;
	}

	inventory_scanned = true;
	inventory_valid = starbook_mtl_boot_inventory_complete(counts);
	if (!inventory_valid)
		printk(BIOS_ERR,
		       "StarBook MTL PRH: unsafe boot-controller inventory NVMe=%zu PCH-xHCI=%zu TCSS-xHCI=%zu\n",
		       counts[STARBOOK_MTL_BOOT_CONTROLLER_NVME],
		       counts[STARBOOK_MTL_BOOT_CONTROLLER_PCH_XHCI],
		       counts[STARBOOK_MTL_BOOT_CONTROLLER_TCSS_XHCI]);
	return inventory_valid;
}

bool payload_resource_revision4_ready(void)
{
	return scan_boot_controllers();
}

bool payload_resource_boot_controller(const struct device *device, uint16_t *priority)
{
	if (!device || !priority || !inventory_scanned || !inventory_valid)
		return false;

	if (device == boot_controllers[STARBOOK_MTL_BOOT_CONTROLLER_NVME])
		*priority = STARBOOK_MTL_BOOT_PRIORITY_NVME;
	else if (device == boot_controllers[STARBOOK_MTL_BOOT_CONTROLLER_PCH_XHCI])
		*priority = STARBOOK_MTL_BOOT_PRIORITY_PCH_XHCI;
	else if (device == boot_controllers[STARBOOK_MTL_BOOT_CONTROLLER_TCSS_XHCI])
		*priority = STARBOOK_MTL_BOOT_PRIORITY_TCSS_XHCI;
	else
		return false;
	return true;
}
