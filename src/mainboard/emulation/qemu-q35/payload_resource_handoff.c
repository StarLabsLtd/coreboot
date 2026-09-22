/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <device/device.h>

#define Q35_BOOT_PRIORITY_NVME 10
#define Q35_BOOT_PRIORITY_XHCI 20
#define Q35_BOOT_PRIORITY_AHCI 30
#define Q35_DMA_NVME_DEVFN 0x18
#define Q35_DMA_XHCI_DEVFN 0x20

bool payload_resource_revision4_ready(void)
{
	return true;
}

bool payload_resource_boot_controller(const struct device *device, uint16_t *priority)
{
	if (!device || !priority)
		return false;
	if (CONFIG(Q35_VTD_DMA_TEST_BACKEND)) {
		if (!device->upstream || device->path.type != DEVICE_PATH_PCI ||
		    device->upstream->segment_group != 0 ||
		    device->upstream->secondary != 0)
			return false;
		if (device->class == 0x010802 &&
		    device->path.pci.devfn == Q35_DMA_NVME_DEVFN) {
			*priority = Q35_BOOT_PRIORITY_NVME;
			return true;
		}
		if (device->class == 0x0c0330 &&
		    device->path.pci.devfn == Q35_DMA_XHCI_DEVFN) {
			*priority = Q35_BOOT_PRIORITY_XHCI;
			return true;
		}
		return false;
	}

	switch (device->class) {
	case 0x010802: /* NVM Express */
		*priority = Q35_BOOT_PRIORITY_NVME;
		return true;
	case 0x0c0330: /* USB xHCI */
		*priority = Q35_BOOT_PRIORITY_XHCI;
		return true;
	case 0x010601: /* SATA AHCI */
		*priority = Q35_BOOT_PRIORITY_AHCI;
		return true;
	default:
		return false;
	}
}
