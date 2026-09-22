/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_resource_policy.h"

#define PCI_CLASS_NVME 0x010802U
#define PCI_CLASS_XHCI 0x0c0330U

enum starbook_mtl_boot_controller starbook_mtl_boot_controller_kind(
	const struct starbook_mtl_pci_identity *identity)
{
	if (!identity || identity->segment != 0)
		return STARBOOK_MTL_BOOT_CONTROLLER_NONE;

	if (identity->class == PCI_CLASS_XHCI && identity->bus == 0) {
		if (identity->devfn == STARBOOK_MTL_PCH_XHCI_DEVFN)
			return STARBOOK_MTL_BOOT_CONTROLLER_PCH_XHCI;
		if (identity->devfn == STARBOOK_MTL_TCSS_XHCI_DEVFN)
			return STARBOOK_MTL_BOOT_CONTROLLER_TCSS_XHCI;
	}

	if (identity->class == PCI_CLASS_NVME && identity->parent_is_pci &&
	    identity->parent_segment == 0 && identity->parent_bus == 0 &&
	    identity->parent_devfn == STARBOOK_MTL_RP10_DEVFN)
		return STARBOOK_MTL_BOOT_CONTROLLER_NVME;

	return STARBOOK_MTL_BOOT_CONTROLLER_NONE;
}

bool starbook_mtl_boot_inventory_complete(const size_t counts[
	STARBOOK_MTL_BOOT_CONTROLLER_COUNT])
{
	if (!counts)
		return false;

	for (size_t kind = STARBOOK_MTL_BOOT_CONTROLLER_NVME;
	     kind < STARBOOK_MTL_BOOT_CONTROLLER_COUNT; kind++)
		if (counts[kind] != 1)
			return false;

	return true;
}
