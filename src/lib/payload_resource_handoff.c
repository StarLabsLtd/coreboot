/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <crc_byte.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <string.h>
#include <version.h>

enum aperture_index {
	APERTURE_IO,
	APERTURE_MEM32,
	APERTURE_MEM64,
	APERTURE_PREF_MEM32,
	APERTURE_PREF_MEM64,
	APERTURE_COUNT,
};

struct aperture {
	uint64_t base;
	uint64_t end;
	bool present;
};

struct root_bridge {
	struct aperture aperture[APERTURE_COUNT];
	uint16_t segment;
	uint8_t bus_start;
	uint8_t bus_end;
	bool present;
};

static bool resource_is_pci_aperture(const struct resource *resource)
{
	const unsigned long type = resource->flags & IORESOURCE_TYPE_MASK;

	return resource->size != 0 && (resource->flags & IORESOURCE_ASSIGNED) != 0 &&
	       (resource->flags & (IORESOURCE_SUBTRACTIVE | IORESOURCE_RESERVE |
				    IORESOURCE_BRIDGE)) == 0 &&
	       (type == IORESOURCE_IO || type == IORESOURCE_MEM);
}

static bool range_end(uint64_t base, uint64_t length, uint64_t *end)
{
	if (length == 0 || base > UINT64_MAX - (length - 1))
		return false;

	*end = base + length - 1;
	return true;
}

static enum aperture_index resource_aperture(const struct resource *resource)
{
	if (resource->flags & IORESOURCE_IO)
		return APERTURE_IO;

	if (resource->base < 4ULL * GiB)
		return resource->flags & IORESOURCE_PREFETCH ?
			APERTURE_PREF_MEM32 : APERTURE_MEM32;

	return resource->flags & IORESOURCE_PREFETCH ?
		APERTURE_PREF_MEM64 : APERTURE_MEM64;
}

static void aperture_add(struct aperture *aperture, uint64_t base, uint64_t end)
{
	if (!aperture->present) {
		aperture->base = base;
		aperture->end = end;
		aperture->present = true;
		return;
	}

	aperture->base = MIN(aperture->base, base);
	aperture->end = MAX(aperture->end, end);
}

static bool ranges_overlap(uint64_t left_base, uint64_t left_end,
			   uint64_t right_base, uint64_t right_end)
{
	return left_base <= right_end && right_base <= left_end;
}

static bool resources_overlap(const struct device *device, const struct resource *resource)
{
	const struct device *other_device;
	const struct resource *other;
	uint64_t end;

	if (!range_end(resource->base, resource->size, &end))
		return true;

	for (other_device = all_devices; other_device; other_device = other_device->next) {
		if (!other_device->enabled || other_device->path.type != DEVICE_PATH_PCI)
			continue;

		for (other = other_device->resource_list; other; other = other->next) {
			uint64_t other_end;

			if (other == resource || !resource_is_pci_aperture(other))
				continue;
			if ((resource->flags & IORESOURCE_TYPE_MASK) !=
			    (other->flags & IORESOURCE_TYPE_MASK))
				continue;
			if (!range_end(other->base, other->size, &other_end))
				return true;
			if (ranges_overlap(resource->base, end, other->base, other_end)) {
				printk(BIOS_ERR, "PRH: %s resource %lx overlaps %s resource %lx\n",
				       dev_path(device), resource->index, dev_path(other_device),
				       other->index);
				return true;
			}
		}
	}

	return false;
}

static bool aperture_contains(const struct aperture *aperture, uint64_t base, uint64_t end)
{
	return aperture->present && aperture->base <= base && end <= aperture->end;
}

static void combine_overlapping_memory(struct aperture *memory, struct aperture *prefetch)
{
	if (!memory->present || !prefetch->present ||
	    !ranges_overlap(memory->base, memory->end, prefetch->base, prefetch->end))
		return;

	memory->base = MIN(memory->base, prefetch->base);
	memory->end = MAX(memory->end, prefetch->end);
	prefetch->present = false;
}

static enum cb_err collect_root_bridges(struct root_bridge *roots, size_t *root_count)
{
	const struct device *device;
	size_t count = 0;

	memset(roots, 0, sizeof(*roots) * PCI_SEGMENT_GROUP_COUNT);

	for (device = all_devices; device; device = device->next) {
		const struct bus *bus;
		struct root_bridge *root;

		if (!device->enabled || device->path.type != DEVICE_PATH_DOMAIN ||
		    device->downstream == NULL)
			continue;

		bus = device->downstream;
		if (bus->segment_group >= PCI_SEGMENT_GROUP_COUNT ||
		    bus->secondary > bus->subordinate ||
		    bus->subordinate >= PCI_BUSES_PER_SEGMENT_GROUP)
			return CB_ERR;

		root = &roots[bus->segment_group];
		if (root->present)
			return CB_ERR;
		root->present = true;
		root->segment = bus->segment_group;
		root->bus_start = bus->secondary;
		root->bus_end = bus->subordinate;
		count++;
	}

	if (count == 0)
		return CB_ERR;

	for (device = all_devices; device; device = device->next) {
		const struct resource *resource;
		struct root_bridge *root;

		if (!device->enabled || device->path.type != DEVICE_PATH_PCI)
			continue;
		if (device->upstream == NULL ||
		    device->upstream->segment_group >= PCI_SEGMENT_GROUP_COUNT)
			return CB_ERR;

		root = &roots[device->upstream->segment_group];
		if (!root->present || device->upstream->secondary < root->bus_start ||
		    device->upstream->secondary > root->bus_end)
			return CB_ERR;

		for (resource = device->resource_list; resource; resource = resource->next) {
			enum aperture_index index;
			uint64_t end;

			if (!resource_is_pci_aperture(resource))
				continue;
			if (!range_end(resource->base, resource->size, &end) ||
			    ((resource->flags & IORESOURCE_IO) && end > UINT16_MAX) ||
			    ((resource->flags & IORESOURCE_MEM) && resource->base < 4ULL * GiB &&
			     end >= 4ULL * GiB) || resources_overlap(device, resource))
				return CB_ERR;

			index = resource_aperture(resource);
			aperture_add(&root->aperture[index], resource->base, end);
		}
	}

	for (size_t index = 0; index < PCI_SEGMENT_GROUP_COUNT; index++) {
		struct root_bridge *root = &roots[index];

		if (!root->present)
			continue;
		combine_overlapping_memory(&root->aperture[APERTURE_MEM32],
					   &root->aperture[APERTURE_PREF_MEM32]);
		combine_overlapping_memory(&root->aperture[APERTURE_MEM64],
					   &root->aperture[APERTURE_PREF_MEM64]);
	}

	/* Prove that every assignment is represented after any aperture coalescing. */
	for (device = all_devices; device; device = device->next) {
		const struct resource *resource;
		const struct root_bridge *root;

		if (!device->enabled || device->path.type != DEVICE_PATH_PCI)
			continue;
		root = &roots[device->upstream->segment_group];
		for (resource = device->resource_list; resource; resource = resource->next) {
			enum aperture_index index;
			uint64_t end;

			if (!resource_is_pci_aperture(resource))
				continue;
			if (!range_end(resource->base, resource->size, &end))
				return CB_ERR;
			index = resource_aperture(resource);
			if (!root->aperture[index].present)
				index = index == APERTURE_PREF_MEM32 ? APERTURE_MEM32 :
					index == APERTURE_PREF_MEM64 ? APERTURE_MEM64 : index;
			if (!aperture_contains(&root->aperture[index], resource->base, end))
				return CB_ERR;
		}
	}

	*root_count = count;
	return CB_SUCCESS;
}

static void set_range(lb_uint64_t *base, lb_uint64_t *length,
		      const struct aperture *aperture)
{
	if (!aperture->present) {
		*base = 0;
		*length = 0;
		return;
	}

	*base = aperture->base;
	*length = aperture->end - aperture->base + 1;
}

static void print_range(const char *name, const struct aperture *aperture)
{
	if (aperture->present)
		printk(BIOS_INFO, "PRH RootBridge %s: %llx - %llx\n", name,
		       aperture->base, aperture->end);
	else
		printk(BIOS_INFO, "PRH RootBridge %s: disabled\n", name);
}

static uint32_t handoff_crc32(const struct lb_payload_resource_handoff *handoff)
{
	const uint8_t *bytes = (const uint8_t *)handoff;
	const size_t crc_offset = offsetof(struct lb_payload_resource_handoff, crc32);
	uint32_t crc = 0;

	for (size_t index = 0; index < handoff->size; index++) {
		const uint8_t byte = index >= crc_offset && index < crc_offset + sizeof(handoff->crc32) ?
			0 : bytes[index];
		crc = crc32_byte(crc, byte);
	}

	return crc;
}

enum cb_err lb_add_payload_resource_handoff(struct lb_header *header)
{
	struct root_bridge roots[PCI_SEGMENT_GROUP_COUNT];
	struct lb_payload_resource_handoff *handoff;
	struct lb_payload_resource_section *section;
	struct lb_prh_pci_root_bridge *output;
	size_t root_count;
	size_t output_index = 0;

	if (header == NULL || collect_root_bridges(roots, &root_count) != CB_SUCCESS)
		return CB_ERR;

	handoff = (void *)lb_new_record(header);
	handoff->tag = LB_TAG_PAYLOAD_RESOURCE_HANDOFF;
	handoff->size = sizeof(*handoff) + sizeof(*section) +
		root_count * sizeof(*output);
	handoff->revision = LB_PAYLOAD_RESOURCE_HANDOFF_REVISION;
	handoff->header_length = sizeof(*handoff);
	handoff->section_header_length = sizeof(*section);
	handoff->flags = 0;
	handoff->crc32 = 0;
	handoff->section_count = 1;
	handoff->producer_stage = 1;
	handoff->producer_generation = coreboot_version_timestamp;
	handoff->lifetime_flags = LB_PRH_LIFETIME_COLD_BOOT |
		LB_PRH_LIFETIME_EXIT_BOOT_SERVICES;

	section = handoff->sections;
	section->type = LB_PRH_SECTION_PCI_ROOT_BRIDGES;
	section->flags = LB_PRH_SECTION_FLAG_AUTHORITATIVE;
	section->header_length = sizeof(*section);
	section->entry_size = sizeof(*output);
	section->entry_count = root_count;
	section->offset = sizeof(*handoff) + sizeof(*section);
	section->length = root_count * sizeof(*output);
	output = (void *)((uint8_t *)handoff + section->offset);

	for (size_t index = 0; index < PCI_SEGMENT_GROUP_COUNT; index++) {
		const struct root_bridge *root = &roots[index];
		struct lb_prh_pci_root_bridge *bridge;

		if (!root->present)
			continue;
		bridge = &output[output_index++];
		memset(bridge, 0, sizeof(*bridge));
		bridge->segment = root->segment;
		bridge->bus_start = root->bus_start;
		bridge->bus_end = root->bus_end;
		set_range(&bridge->io_base, &bridge->io_length,
			  &root->aperture[APERTURE_IO]);
		set_range(&bridge->mem32_base, &bridge->mem32_length,
			  &root->aperture[APERTURE_MEM32]);
		set_range(&bridge->mem64_base, &bridge->mem64_length,
			  &root->aperture[APERTURE_MEM64]);
		set_range(&bridge->pref_mem32_base, &bridge->pref_mem32_length,
			  &root->aperture[APERTURE_PREF_MEM32]);
		set_range(&bridge->pref_mem64_base, &bridge->pref_mem64_length,
			  &root->aperture[APERTURE_PREF_MEM64]);

		printk(BIOS_INFO, "PRH RootBridge: segment %u, Bus: %x - %x\n",
		       root->segment, root->bus_start, root->bus_end);
		printk(BIOS_INFO, "PRH RootBridge ECAM: %llx, NoExtConfSpace: No\n",
		       (uint64_t)CONFIG_ECAM_MMCONF_BASE_ADDRESS +
			root->segment * PCI_PER_SEGMENT_GROUP_ECAM_SIZE);
		print_range("Io", &root->aperture[APERTURE_IO]);
		print_range("Mem", &root->aperture[APERTURE_MEM32]);
		print_range("MemAbove4G", &root->aperture[APERTURE_MEM64]);
		print_range("PMem", &root->aperture[APERTURE_PREF_MEM32]);
		print_range("PMemAbove4G", &root->aperture[APERTURE_PREF_MEM64]);
	}

	handoff->crc32 = handoff_crc32(handoff);
	return CB_SUCCESS;
}
