/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <device/device.h>

#define Q35_BOOT_PRIORITY_NVME 10
#define Q35_BOOT_PRIORITY_XHCI 20
#define Q35_BOOT_PRIORITY_AHCI 30

bool payload_resource_revision4_ready(void)
{
	/* Q35 supplies an authoritative generated tree and fixed boot policy below. */
	return true;
}

bool payload_resource_boot_controller(const struct device *device, uint16_t *priority)
{
	if (!device || !priority)
		return false;
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
