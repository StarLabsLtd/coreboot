/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <crc_byte.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <string.h>
#include <version.h>

static bool range_end(uint64_t base, uint64_t length, uint64_t *end)
{
	if (!length || base > UINT64_MAX - (length - 1))
		return false;
	*end = base + length - 1;
	return true;
}

static bool ranges_overlap(uint64_t a_base, uint64_t a_end, uint64_t b_base, uint64_t b_end)
{
	return a_base <= b_end && b_base <= a_end;
}

static bool resource_is_assignment(const struct resource *resource)
{
	const unsigned long type = resource->flags & IORESOURCE_TYPE_MASK;

	return resource->size && (resource->flags & IORESOURCE_ASSIGNED) &&
		!(resource->flags & (IORESOURCE_SUBTRACTIVE | IORESOURCE_RESERVE |
				    IORESOURCE_BRIDGE | IORESOURCE_CACHEABLE)) &&
		(type == IORESOURCE_IO || type == IORESOURCE_MEM);
}

static bool resource_is_reserved(const struct resource *resource)
{
	const unsigned long type = resource->flags & IORESOURCE_TYPE_MASK;

	return resource->size && (type == IORESOURCE_IO || type == IORESOURCE_MEM) &&
		(resource->flags & (IORESOURCE_RESERVE | IORESOURCE_CACHEABLE));
}

static bool resource_bar(const struct resource *resource, uint8_t *bar)
{
	if (resource->index < PCI_BASE_ADDRESS_0 || resource->index > PCI_BASE_ADDRESS_5 ||
	    (resource->index - PCI_BASE_ADDRESS_0) % sizeof(uint32_t))
		return false;
	*bar = (resource->index - PCI_BASE_ADDRESS_0) / sizeof(uint32_t);
	return true;
}

static const struct device *root_domain(const struct device *device)
{
	const struct bus *bus = device->upstream;

	while (bus && bus->dev && bus->dev->path.type != DEVICE_PATH_DOMAIN)
		bus = bus->dev->upstream;
	return bus ? bus->dev : NULL;
}

static bool valid_domain(const struct device *domain)
{
	const struct bus *bus = domain->downstream;
	const struct device *other;

	if (!domain->enabled || domain->path.type != DEVICE_PATH_DOMAIN || !bus ||
	    bus->segment_group >= PCI_SEGMENT_GROUP_COUNT ||
	    bus->secondary > bus->subordinate ||
	    bus->subordinate >= CONFIG_ECAM_MMCONF_BUS_NUMBER)
		return false;
	for (other = all_devices; other; other = other->next) {
		const struct bus *other_bus;

		if (other == domain || !other->enabled || other->path.type != DEVICE_PATH_DOMAIN)
			continue;
		other_bus = other->downstream;
		if (!other_bus)
			return false;
		if (bus->segment_group == other_bus->segment_group &&
		    ranges_overlap(bus->secondary, bus->subordinate,
				   other_bus->secondary, other_bus->subordinate))
			return false;
	}
	return true;
}

static bool assignment_valid(const struct device *device, const struct resource *resource)
{
	const struct device *domain = root_domain(device);
	const struct device *other_device;
	uint8_t bar;
	uint64_t end;

	if (!domain || !valid_domain(domain) || !resource_bar(resource, &bar) ||
	    !range_end(resource->base, resource->size, &end))
		return false;
	if (!device->upstream ||
	    device->upstream->segment_group != domain->downstream->segment_group ||
	    device->upstream->secondary < domain->downstream->secondary ||
	    device->upstream->secondary > domain->downstream->subordinate)
		return false;
	if (device->path.pci.devfn > UINT8_MAX)
		return false;
	if ((resource->flags & IORESOURCE_IO) && end > UINT16_MAX)
		return false;
	if ((resource->flags & IORESOURCE_MEM) && resource->base < 4ULL * GiB && end >= 4ULL * GiB)
		return false;
	if ((resource->flags & IORESOURCE_MEM) && end > UINT32_MAX &&
	    !(resource->flags & IORESOURCE_PCI64))
		return false;
	if ((resource->flags & IORESOURCE_PCI64) &&
	    !(resource->flags & IORESOURCE_MEM))
		return false;
	if ((resource->flags & IORESOURCE_PCI64) && bar == 5)
		return false;
	/* ECAM is configuration space, never an assigned BAR interval. */
	if ((resource->flags & IORESOURCE_MEM) &&
	    ranges_overlap(resource->base, end, CONFIG_ECAM_MMCONF_BASE_ADDRESS,
			   CONFIG_ECAM_MMCONF_BASE_ADDRESS +
			   (uint64_t)CONFIG_ECAM_MMCONF_BUS_NUMBER * MiB - 1))
		return false;

	for (other_device = all_devices; other_device; other_device = other_device->next) {
		const struct resource *other;

		if (!other_device->enabled || other_device->path.type != DEVICE_PATH_PCI)
			continue;
		for (other = other_device->resource_list; other; other = other->next) {
			uint64_t other_end;
			uint8_t other_bar;

			if (other == resource ||
			    (!resource_is_assignment(other) && !resource_is_reserved(other)))
				continue;
			if (resource_bar(other, &other_bar) && other_device->upstream &&
			    root_domain(other_device) == domain &&
			    other_device->upstream->secondary == device->upstream->secondary &&
			    other_device->path.pci.devfn == device->path.pci.devfn &&
			    bar <= other_bar + !!(other->flags & IORESOURCE_PCI64) &&
			    other_bar <= bar + !!(resource->flags & IORESOURCE_PCI64))
				return false;
			if ((other->flags & IORESOURCE_TYPE_MASK) !=
			    (resource->flags & IORESOURCE_TYPE_MASK))
				continue;
			if (!range_end(other->base, other->size, &other_end) ||
			    ranges_overlap(resource->base, end, other->base, other_end)) {
				printk(BIOS_ERR, "PRH: %s resource %lx overlaps %s resource %lx\n",
				       dev_path(device), resource->index, dev_path(other_device),
				       other->index);
				return false;
			}
		}
	}
	return true;
}

static uint8_t assignment_type(const struct resource *resource)
{
	if (resource->flags & IORESOURCE_IO)
		return LB_PRH_PCI_RESOURCE_IO;
	if (resource->base < 4ULL * GiB)
		return resource->flags & IORESOURCE_PREFETCH ?
			LB_PRH_PCI_RESOURCE_PREFETCH_MMIO32 : LB_PRH_PCI_RESOURCE_MMIO32;
	return resource->flags & IORESOURCE_PREFETCH ?
		LB_PRH_PCI_RESOURCE_PREFETCH_MMIO64 : LB_PRH_PCI_RESOURCE_MMIO64;
}

static enum cb_err count_records(size_t *root_count, size_t *assignment_count)
{
	const struct device *device;

	*root_count = 0;
	*assignment_count = 0;
	for (device = all_devices; device; device = device->next) {
		const struct resource *resource;

		if (device->enabled && device->path.type == DEVICE_PATH_DOMAIN) {
			if (!valid_domain(device))
				return CB_ERR;
			if (++(*root_count) > LB_PRH_PCI_MAX_ROOTS)
				return CB_ERR;
		}
		if (!device->enabled || device->path.type != DEVICE_PATH_PCI)
			continue;
		if (!root_domain(device))
			return CB_ERR;
		for (resource = device->resource_list; resource; resource = resource->next) {
			if (!resource_is_assignment(resource))
				continue;
			if (!assignment_valid(device, resource))
				return CB_ERR;
			if (++(*assignment_count) > LB_PRH_PCI_MAX_ASSIGNMENTS)
				return CB_ERR;
		}
	}
	return *root_count && *assignment_count ? CB_SUCCESS : CB_ERR;
}

static uint32_t handoff_crc32(const struct lb_payload_resource_handoff *handoff)
{
	const uint8_t *bytes = (const uint8_t *)handoff;
	const size_t crc_offset = offsetof(struct lb_payload_resource_handoff, crc32);
	uint32_t crc = 0;

	for (size_t index = 0; index < handoff->size; index++)
		crc = crc32_byte(crc, index >= crc_offset &&
					     index < crc_offset + sizeof(handoff->crc32) ?
					     0 : bytes[index]);
	return crc;
}

enum cb_err lb_add_payload_resource_handoff(struct lb_header *header)
{
	struct lb_payload_resource_handoff *handoff;
	struct lb_payload_resource_section *root_section, *assignment_section;
	struct lb_prh_pci_root_bridge *roots;
	struct lb_prh_pci_assignment *assignments;
	const struct device *device;
	size_t root_count, assignment_count, root_index = 0, assignment_index = 0;

	if (!header || count_records(&root_count, &assignment_count) != CB_SUCCESS)
		return CB_ERR;
	if (root_count > UINT32_MAX || assignment_count > UINT32_MAX ||
	    root_count > (UINT32_MAX - sizeof(*handoff) - 2 * sizeof(*root_section)) /
			 sizeof(*roots) ||
	    assignment_count > (UINT32_MAX - sizeof(*handoff) - 2 * sizeof(*root_section) -
				      root_count * sizeof(*roots)) / sizeof(*assignments))
		return CB_ERR;

	handoff = (void *)lb_new_record(header);
	handoff->size = sizeof(*handoff) + 2 * sizeof(*root_section) +
		root_count * sizeof(*roots) + assignment_count * sizeof(*assignments);
	memset(handoff, 0, handoff->size);
	handoff->tag = LB_TAG_PAYLOAD_RESOURCE_HANDOFF;
	handoff->size = sizeof(*handoff) + 2 * sizeof(*root_section) +
		root_count * sizeof(*roots) + assignment_count * sizeof(*assignments);
	handoff->revision = LB_PAYLOAD_RESOURCE_HANDOFF_REVISION;
	handoff->header_length = sizeof(*handoff);
	handoff->section_header_length = sizeof(*root_section);
	handoff->section_count = 2;
	handoff->producer_stage = 1;
	handoff->producer_generation = coreboot_version_timestamp;
	handoff->lifetime_flags = LB_PRH_LIFETIME_COLD_BOOT |
		LB_PRH_LIFETIME_EXIT_BOOT_SERVICES;

	root_section = &handoff->sections[0];
	assignment_section = &handoff->sections[1];
	root_section->type = LB_PRH_SECTION_PCI_ROOT_BRIDGES;
	root_section->flags = LB_PRH_SECTION_FLAG_AUTHORITATIVE;
	root_section->header_length = sizeof(*root_section);
	root_section->entry_size = sizeof(*roots);
	root_section->entry_count = root_count;
	root_section->offset = sizeof(*handoff) + 2 * sizeof(*root_section);
	root_section->length = root_count * sizeof(*roots);
	assignment_section->type = LB_PRH_SECTION_PCI_ASSIGNMENTS;
	assignment_section->flags = LB_PRH_SECTION_FLAG_AUTHORITATIVE;
	assignment_section->header_length = sizeof(*assignment_section);
	assignment_section->entry_size = sizeof(*assignments);
	assignment_section->entry_count = assignment_count;
	assignment_section->offset = root_section->offset + root_section->length;
	assignment_section->length = assignment_count * sizeof(*assignments);
	roots = (void *)((uint8_t *)handoff + root_section->offset);
	assignments = (void *)((uint8_t *)handoff + assignment_section->offset);

	for (device = all_devices; device; device = device->next) {
		const struct resource *resource;
		const struct device *domain;
		const struct bus *bus;

		if (device->enabled && device->path.type == DEVICE_PATH_DOMAIN) {
			bus = device->downstream;
			roots[root_index].segment = bus->segment_group;
			roots[root_index].bus_start = bus->secondary;
			roots[root_index].bus_end = bus->subordinate;
			roots[root_index].flags = LB_PRH_PCI_ROOT_TOPOLOGY_ONLY;
			printk(BIOS_INFO, "PRH RootBridge: segment %u, Bus: %x - %x\n",
			       bus->segment_group, bus->secondary, bus->subordinate);
			root_index++;
		}
		if (!device->enabled || device->path.type != DEVICE_PATH_PCI)
			continue;
		domain = root_domain(device);
		bus = domain->downstream;
		for (resource = device->resource_list; resource; resource = resource->next) {
			struct lb_prh_pci_assignment *output;

			if (!resource_is_assignment(resource))
				continue;
			output = &assignments[assignment_index++];
			output->segment = bus->segment_group;
			output->bus = device->upstream->secondary;
			output->device = device->path.pci.devfn >> 3;
			output->function = device->path.pci.devfn & 7;
			if (!resource_bar(resource, &output->bar))
				return CB_ERR;
			output->resource_type = assignment_type(resource);
			output->flags = resource->flags & IORESOURCE_PCI64 ?
				LB_PRH_PCI_ASSIGNMENT_64BIT : 0;
			output->base = resource->base;
			output->length = resource->size;
			printk(BIOS_INFO, "PRH PCI %02x:%02x.%x BAR %x type %u: %llx + %llx\n",
			       output->bus, output->device, output->function, output->bar,
			       output->resource_type, resource->base, resource->size);
		}
	}
	handoff->crc32 = handoff_crc32(handoff);
	return CB_SUCCESS;
}
