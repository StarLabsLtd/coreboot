/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <cbmem.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pci_ids.h>
#include <root_bridge_info.h>
#include <string.h>

/* EFI_PCI_ATTRIBUTE_* legacy-decode bits from PciRootBridgeIo. */
#define ISA_IO			0x10003
#define VGA_PALETTE_IO		0x20004
#define VGA_IO			0x40010
#define VGA_MEMORY		0x00008
#define IDE_PRIMARY_IO		0x00020
#define IDE_SECONDARY_IO	0x00040

static void include_aperture(struct root_bridge_aperture *aperture,
			     uint64_t base, uint64_t limit)
{
	aperture->base = MIN(aperture->base, base);
	aperture->limit = MAX(aperture->limit, limit);
}

static bool include_resource(struct pci_root_bridge_info *root, const struct resource *res)
{
	if (!res || !(res->flags & IORESOURCE_ASSIGNED) || !res->size ||
	    res->size - 1 > UINT64_MAX - res->base)
		return false;

	uint64_t limit = res->base + res->size - 1;
	if (res->flags & IORESOURCE_IO) {
		include_aperture(&root->io, res->base, limit);
	} else if (res->flags & IORESOURCE_MEM) {
		/* Match the payload's assigned-resource scan, which merges prefetchable BARs. */
		struct root_bridge_aperture *aperture = res->base < 4ULL * GiB ?
			&root->mem : &root->mem_above4g;
		include_aperture(aperture, res->base, limit);
	} else {
		return false;
	}

	return true;
}

static bool include_bars(struct pci_root_bridge_info *root, const struct device *dev,
			 unsigned int count, uint16_t command)
{
	for (unsigned int i = 0; i < count; i++) {
		unsigned int index = PCI_BASE_ADDRESS_0 + 4 * i;
		uint32_t bar = pci_read_config32(dev, index);
		uint64_t base;
		unsigned long type;

		if (bar & PCI_BASE_ADDRESS_SPACE_IO) {
			if (!(command & PCI_COMMAND_IO))
				continue;
			base = bar & ~PCI_BASE_ADDRESS_IO_ATTR_MASK;
			type = IORESOURCE_IO;
		} else {
			if (!(command & PCI_COMMAND_MEMORY))
				continue;
			base = bar & ~PCI_BASE_ADDRESS_MEM_ATTR_MASK;
			type = IORESOURCE_MEM;
			if ((bar & PCI_BASE_ADDRESS_MEM_LIMIT_MASK) == PCI_BASE_ADDRESS_MEM_LIMIT_64) {
				if (++i >= count)
					return false;
				base |= (uint64_t)pci_read_config32(dev, index + 4) << 32;
			}
		}

		if (!base)
			continue;

		/* Chipset resources may use BAR-like indices without being PCI BARs. */
		const struct resource *res = probe_resource(dev, index);
		if (!res || res->base != base || !(res->flags & type) ||
		    !include_resource(root, res))
			return false;
	}

	return true;
}

static bool fill_root_bridge(struct pci_root_bridge_info *root, const struct device *domain)
{
	const struct bus *bus = domain->downstream;
	if (!bus || bus->segment_group || bus->secondary > bus->subordinate ||
	    bus->subordinate > UINT8_MAX)
		return false;

	memset(root, 0, sizeof(*root));
	root->segment = bus->segment_group;
	root->bus.base = bus->secondary;
	root->bus.limit = bus->subordinate;
	root->io.base = root->mem.base = root->mem_above4g.base = UINT64_MAX;
	root->pmem.base = root->pmem_above4g.base = UINT64_MAX;
	/* Preserve the existing EDK2 PciRoot device path and bus-number UID. */
	root->hid = 0x0a0341d0;
	root->uid = bus->secondary;

	bool has_pci_device = false;
	for (const struct device *dev = bus->children; dev; dev = dev->sibling) {
		if (!dev->enabled || dev->hidden || dev->path.type != DEVICE_PATH_PCI)
			continue;

		has_pci_device = true;
		uint16_t command = pci_read_config16(dev, PCI_COMMAND);
		if (command & PCI_COMMAND_VGA_PALETTE)
			root->attributes |= VGA_PALETTE_IO;

		switch (dev->class >> 8) {
		case PCI_CLASS_STORAGE_IDE:
			if (dev->class & 0x81)
				root->attributes |= IDE_PRIMARY_IO;
			if (dev->class & 0x84)
				root->attributes |= IDE_SECONDARY_IO;
			break;
		case PCI_CLASS_NOT_DEFINED_VGA:
		case PCI_CLASS_DISPLAY_VGA:
			root->attributes |= VGA_PALETTE_IO | VGA_MEMORY | VGA_IO;
			break;
		case PCI_CLASS_BRIDGE_ISA:
		case PCI_CLASS_BRIDGE_EISA:
		case 0x0680: /* ISA-positive-decode bridge. */
			root->attributes |= ISA_IO;
			break;
		}

		unsigned int header = dev->hdr_type & 0x7f;
		if (header != PCI_HEADER_TYPE_NORMAL && header != PCI_HEADER_TYPE_BRIDGE)
			return false;
		if (!include_bars(root, dev, header == PCI_HEADER_TYPE_NORMAL ? 6 : 2, command))
			return false;

		if (header != PCI_HEADER_TYPE_BRIDGE)
			continue;

		uint16_t control = pci_read_config16(dev, PCI_BRIDGE_CONTROL);
		if (control & PCI_BRIDGE_CTL_ISA)
			root->attributes |= ISA_IO;
		if (control & PCI_BRIDGE_CTL_VGA) {
			root->attributes |= 4 | VGA_MEMORY | 0x10;
			if (control & PCI_BRIDGE_CTL_VGA16)
				root->attributes |= VGA_PALETTE_IO | VGA_IO;
		}

		for (const struct resource *res = dev->resource_list; res; res = res->next) {
			if ((res->flags & IORESOURCE_PCI_BRIDGE) && res->size &&
			    !include_resource(root, res))
				return false;
		}
	}

	root->supports = root->attributes;
	return has_pci_device;
}

static void publish_root_bridges(void *unused)
{
	unsigned int count = 0;
	if (cbmem_find(CBMEM_ID_RB_INFO))
		return;

	for (const struct device *dev = all_devices; dev; dev = dev->next) {
		if (dev->enabled && dev->path.type == DEVICE_PATH_DOMAIN)
			count++;
	}
	if (!count || count > UINT8_MAX)
		return;

	size_t size = sizeof(struct pci_root_bridges_info) +
		count * sizeof(struct pci_root_bridge_info);
	struct pci_root_bridges_info *info = cbmem_add(CBMEM_ID_RB_INFO, size);
	if (!info)
		return;
	memset(info, 0, size);

	unsigned int i = 0;
	for (const struct device *dev = all_devices; dev; dev = dev->next) {
		if (!dev->enabled || dev->path.type != DEVICE_PATH_DOMAIN)
			continue;
		if (!fill_root_bridge(&info->root_bridge[i++], dev)) {
			cbmem_entry_remove(cbmem_entry_find(CBMEM_ID_RB_INFO));
			return;
		}
	}

	info->revision = 1;
	info->length = size;
	info->resource_assigned = true;
	info->count = count;
}

BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_ENTRY, publish_root_bridges, NULL);
