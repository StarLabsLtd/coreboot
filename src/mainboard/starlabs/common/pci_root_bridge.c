/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <cbmem.h>
#include <commonlib/bsd/compiler.h>
#include <commonlib/helpers.h>
#include <device/device.h>
#include <device/resource.h>
#include <halt.h>
#include <intelblocks/systemagent.h>
#include <string.h>

#define CB_PCI_ROOT_BRIDGES_REVISION 1
#define CB_PNP0A03_HID               0x0a0341d0

#define CB_PCI_ATTRIBUTE_ISA_MOTHERBOARD_IO BIT(0)
#define CB_PCI_ATTRIBUTE_ISA_IO             BIT(1)
#define CB_PCI_ATTRIBUTE_VGA_PALETTE_IO     BIT(2)
#define CB_PCI_ATTRIBUTE_VGA_MEMORY         BIT(3)
#define CB_PCI_ATTRIBUTE_VGA_IO             BIT(4)
#define CB_PCI_ATTRIBUTE_ISA_IO_16          BIT(16)
#define CB_PCI_ATTRIBUTE_VGA_PALETTE_IO_16  BIT(17)
#define CB_PCI_ATTRIBUTE_VGA_IO_16          BIT(18)
#define CB_PCI_ROOT_BRIDGE_ATTRIBUTES \
	(CB_PCI_ATTRIBUTE_ISA_MOTHERBOARD_IO | CB_PCI_ATTRIBUTE_ISA_IO | \
	 CB_PCI_ATTRIBUTE_VGA_PALETTE_IO | CB_PCI_ATTRIBUTE_VGA_MEMORY | \
	 CB_PCI_ATTRIBUTE_VGA_IO | CB_PCI_ATTRIBUTE_ISA_IO_16 | \
	 CB_PCI_ATTRIBUTE_VGA_PALETTE_IO_16 | CB_PCI_ATTRIBUTE_VGA_IO_16)

/*
 * This packed ABI mirrors UNIVERSAL_PAYLOAD_PCI_ROOT_BRIDGES revision 1
 * from MdeModulePkg/Include/UniversalPayload/PciRootBridges.h.
 */
struct cb_pci_handoff_header {
	uint8_t revision;
	uint8_t reserved;
	uint16_t length;
} __packed;

struct cb_pci_aperture {
	uint64_t base;
	uint64_t limit;
	uint64_t translation;
} __packed;

struct cb_pci_root_bridge {
	uint32_t segment;
	uint64_t supports;
	uint64_t attributes;
	uint8_t dma_above_4g;
	uint8_t no_extended_config_space;
	uint64_t allocation_attributes;
	struct cb_pci_aperture bus;
	struct cb_pci_aperture io;
	struct cb_pci_aperture mem;
	struct cb_pci_aperture mem_above_4g;
	struct cb_pci_aperture pmem;
	struct cb_pci_aperture pmem_above_4g;
	uint32_t hid;
	uint32_t uid;
} __packed;

struct cb_pci_root_bridge_info {
	struct cb_pci_handoff_header header;
	uint8_t resource_assigned;
	uint8_t count;
	struct cb_pci_root_bridge root_bridge;
} __packed;

_Static_assert(sizeof(struct cb_pci_handoff_header) == 4,
	       "PCI handoff header layout changed");
_Static_assert(sizeof(struct cb_pci_root_bridge) == 182,
	       "PCI root bridge layout changed");
_Static_assert(sizeof(struct cb_pci_root_bridge_info) == 188,
	       "PCI root bridge handoff layout changed");

static void invalidate_pci_aperture(struct cb_pci_aperture *aperture)
{
	aperture->base = UINT64_MAX;
	aperture->limit = 0;
}

static bool set_pci_aperture(struct cb_pci_aperture *aperture, uint64_t base,
			     uint64_t limit)
{
	if (base > limit || aperture->base <= aperture->limit)
		return false;

	aperture->base = base;
	aperture->limit = limit;
	return true;
}

static bool set_domain_apertures(const struct device *domain,
				 struct cb_pci_root_bridge *root_bridge,
				 uint64_t tolud, uint64_t touud)
{
	const struct resource *resource;

	for (resource = domain->resource_list; resource; resource = resource->next) {
		uint64_t base;

		if (!(resource->flags & IORESOURCE_ASSIGNED) ||
		    !(resource->flags & IORESOURCE_SUBTRACTIVE) ||
		    (resource->flags & IORESOURCE_FIXED) || resource->base > resource->limit)
			continue;

		if (resource->flags & IORESOURCE_IO) {
			if (resource->limit > UINT16_MAX ||
			    !set_pci_aperture(&root_bridge->io, resource->base,
					      resource->limit))
				return false;
			continue;
		}

		if (!(resource->flags & IORESOURCE_MEM))
			continue;

		if (resource->limit < 4ULL * GiB) {
			base = MAX(resource->base, tolud);
			if (!set_pci_aperture(&root_bridge->mem, base, resource->limit))
				return false;
			continue;
		}

		if (resource->base < 4ULL * GiB)
			return false;

		base = MAX(resource->base, touud);
		if (!set_pci_aperture(&root_bridge->mem_above_4g, base, resource->limit))
			return false;
	}

	return root_bridge->io.base <= root_bridge->io.limit &&
		root_bridge->mem.base <= root_bridge->mem.limit &&
		root_bridge->mem_above_4g.base <= root_bridge->mem_above_4g.limit;
}

static void export_pci_root_bridge_info(void *unused)
{
	const struct cbmem_entry *entry;
	const struct device *domain;
	struct cb_pci_root_bridge_info *info;
	struct cb_pci_root_bridge *root_bridge;
	uint64_t tolud;
	uint64_t touud;

	domain = dev_find_path(NULL, DEVICE_PATH_DOMAIN);
	if (!domain || !domain->enabled || !domain->downstream ||
	    dev_find_path(domain, DEVICE_PATH_DOMAIN))
		die("PCI root bridge handoff requires exactly one enabled domain\n");

	if (domain->downstream->segment_group != 0 || domain->downstream->secondary != 0)
		die("PCI root bridge handoff requires segment 0, bus 0\n");

	tolud = sa_get_tolud_base();
	touud = sa_get_touud_base();
	if (!tolud || tolud >= 4ULL * GiB || touud < 4ULL * GiB)
		die("PCI root bridge handoff has invalid DRAM limits\n");

	entry = cbmem_entry_add(CBMEM_ID_RB_INFO, sizeof(*info));
	if (!entry || cbmem_entry_size(entry) != sizeof(*info))
		die("PCI root bridge handoff allocation failed\n");

	info = cbmem_entry_start(entry);
	memset(info, 0, sizeof(*info));
	info->header.revision = CB_PCI_ROOT_BRIDGES_REVISION;
	info->header.length = sizeof(*info);
	info->resource_assigned = true;
	info->count = 1;

	root_bridge = &info->root_bridge;
	root_bridge->segment = 0;
	root_bridge->supports = CB_PCI_ROOT_BRIDGE_ATTRIBUTES;
	root_bridge->attributes = CB_PCI_ROOT_BRIDGE_ATTRIBUTES;
	root_bridge->bus.base = 0;
	root_bridge->bus.limit = UINT8_MAX;
	root_bridge->hid = CB_PNP0A03_HID;

	invalidate_pci_aperture(&root_bridge->io);
	invalidate_pci_aperture(&root_bridge->mem);
	invalidate_pci_aperture(&root_bridge->mem_above_4g);
	invalidate_pci_aperture(&root_bridge->pmem);
	invalidate_pci_aperture(&root_bridge->pmem_above_4g);

	if (!set_domain_apertures(domain, root_bridge, tolud, touud))
		die("PCI root bridge handoff has invalid resource apertures\n");
}

BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_ENTRY, export_pci_root_bridge_info, NULL);
