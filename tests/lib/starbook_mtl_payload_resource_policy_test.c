/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdio.h>
#include <stdlib.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/payload_resource_policy.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
		exit(1); \
	} \
} while (0)

static struct starbook_mtl_pci_identity identity(uint32_t class, uint8_t bus,
	uint8_t devfn)
{
	return (struct starbook_mtl_pci_identity) {
		.class = class,
		.bus = bus,
		.devfn = devfn,
	};
}

int main(void)
{
	struct starbook_mtl_pci_identity value;
	size_t counts[STARBOOK_MTL_BOOT_CONTROLLER_COUNT] = { 0, 1, 1, 1 };

	value = identity(0x0c0330, 0, STARBOOK_MTL_PCH_XHCI_DEVFN);
	CHECK(starbook_mtl_boot_controller_kind(&value) ==
	       STARBOOK_MTL_BOOT_CONTROLLER_PCH_XHCI);
	value.devfn = STARBOOK_MTL_TCSS_XHCI_DEVFN;
	CHECK(starbook_mtl_boot_controller_kind(&value) ==
	       STARBOOK_MTL_BOOT_CONTROLLER_TCSS_XHCI);
	value.bus = 1;
	CHECK(starbook_mtl_boot_controller_kind(&value) ==
	       STARBOOK_MTL_BOOT_CONTROLLER_NONE);
	value.bus = 0;
	value.segment = 1;
	CHECK(starbook_mtl_boot_controller_kind(&value) ==
	       STARBOOK_MTL_BOOT_CONTROLLER_NONE);

	value = identity(0x010802, 2, 0);
	value.parent_is_pci = true;
	value.parent_devfn = STARBOOK_MTL_RP10_DEVFN;
	CHECK(starbook_mtl_boot_controller_kind(&value) ==
	       STARBOOK_MTL_BOOT_CONTROLLER_NVME);
	value.parent_devfn = 0x30;
	CHECK(starbook_mtl_boot_controller_kind(&value) ==
	       STARBOOK_MTL_BOOT_CONTROLLER_NONE);
	value.parent_devfn = STARBOOK_MTL_RP10_DEVFN;
	value.parent_segment = 1;
	CHECK(starbook_mtl_boot_controller_kind(&value) ==
	       STARBOOK_MTL_BOOT_CONTROLLER_NONE);

	CHECK(starbook_mtl_boot_inventory_complete(counts));
	counts[STARBOOK_MTL_BOOT_CONTROLLER_NVME] = 0;
	CHECK(!starbook_mtl_boot_inventory_complete(counts));
	counts[STARBOOK_MTL_BOOT_CONTROLLER_NVME] = 2;
	CHECK(!starbook_mtl_boot_inventory_complete(counts));
	counts[STARBOOK_MTL_BOOT_CONTROLLER_NVME] = 1;
	counts[STARBOOK_MTL_BOOT_CONTROLLER_TCSS_XHCI] = 2;
	CHECK(!starbook_mtl_boot_inventory_complete(counts));

	return 0;
}
