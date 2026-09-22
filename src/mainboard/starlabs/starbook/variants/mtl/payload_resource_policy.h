/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_PAYLOAD_RESOURCE_POLICY_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_PAYLOAD_RESOURCE_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STARBOOK_MTL_RP10_DEVFN 0x31U
#define STARBOOK_MTL_PCH_XHCI_DEVFN 0xa0U
#define STARBOOK_MTL_TCSS_XHCI_DEVFN 0x68U

enum starbook_mtl_boot_controller {
	STARBOOK_MTL_BOOT_CONTROLLER_NONE,
	STARBOOK_MTL_BOOT_CONTROLLER_NVME,
	STARBOOK_MTL_BOOT_CONTROLLER_PCH_XHCI,
	STARBOOK_MTL_BOOT_CONTROLLER_TCSS_XHCI,
	STARBOOK_MTL_BOOT_CONTROLLER_COUNT,
};

struct starbook_mtl_pci_identity {
	uint32_t class;
	uint16_t segment;
	uint8_t bus;
	uint8_t devfn;
	bool parent_is_pci;
	uint16_t parent_segment;
	uint8_t parent_bus;
	uint8_t parent_devfn;
};

enum starbook_mtl_boot_controller starbook_mtl_boot_controller_kind(
	const struct starbook_mtl_pci_identity *identity);
bool starbook_mtl_boot_inventory_complete(const size_t counts[
	STARBOOK_MTL_BOOT_CONTROLLER_COUNT]);

#endif
