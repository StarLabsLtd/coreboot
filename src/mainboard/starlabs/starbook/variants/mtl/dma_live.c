/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_live.h"

#include <string.h>

#include "../../../../../soc/intel/common/block/vtd/vtd_transition.h"
#include "../../../../../soc/intel/common/block/vtd/vtd_translation.h"

#define PCI_VENDOR_DEVICE 0x00U
#define PCI_COMMAND 0x04U
#define PCI_CLASS_REVISION 0x08U
#define PCI_COMMAND_MASTER (1U << 2)
#define DMA_LIVE_PAGE_SIZE VTD_TRANSLATION_PAGE_SIZE
#define DMA_LIVE_HANDOFF_BYTES \
	(sizeof(struct dma_handoff_header) + STARBOOK_MTL_DMA_LIVE_REQUESTERS * \
	 sizeof(struct dma_handoff_requester))
#define VTD_ROOT_POINTER_SET (1U << 30)
#define VTD_TRANSLATION_ENABLE (1U << 31)
#define VTD_PROTECTED_MEMORY_ACTIVE (1U << 0)
#define VTD_PROTECTED_MEMORY_REQUEST (1U << 31)

struct dma_live_function {
	struct starbook_mtl_dma_live_identity identity;
	uint16_t command;
};

struct dma_live_snapshot {
	struct dma_live_function functions[STARBOOK_MTL_DMA_LIVE_MAX_FUNCTIONS];
	size_t count;
};

enum dma_live_phase {
	DMA_LIVE_EMPTY,
	DMA_LIVE_FAILED,
	DMA_LIVE_ACTIVE,
};

struct dma_live_state {
	enum dma_live_phase phase;
	struct dma_live_snapshot snapshot;
	struct starbook_mtl_dma_live_identity requesters[
		STARBOOK_MTL_DMA_LIVE_REQUESTERS];
	struct starbook_mtl_dma_live_layout layout;
};

static struct dma_live_state live_state;

static bool identity_equal(const struct starbook_mtl_dma_live_identity *first,
	const struct starbook_mtl_dma_live_identity *second)
{
	return first->bdf == second->bdf && first->vendor == second->vendor &&
		first->device == second->device && first->class == second->class;
}

static bool expected_valid(const struct starbook_mtl_dma_live_identity expected[
	STARBOOK_MTL_DMA_LIVE_REQUESTERS], uint16_t bus_count)
{
	for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++) {
		if ((expected[index].bdf >> 8) >= bus_count ||
		    expected[index].vendor == UINT16_MAX ||
		    expected[index].class > 0xffffffU)
			return false;
		for (size_t prior = 0; prior < index; prior++)
			if (expected[index].bdf == expected[prior].bdf)
				return false;
	}
	return true;
}

static int snapshot_pci(const struct starbook_mtl_dma_live_pci_io *io,
	uint16_t bus_count,
	const struct starbook_mtl_dma_live_identity expected[
		STARBOOK_MTL_DMA_LIVE_REQUESTERS],
	struct dma_live_snapshot *snapshot)
{
	bool found[STARBOOK_MTL_DMA_LIVE_REQUESTERS] = { false };

	memset(snapshot, 0, sizeof(*snapshot));
	if (!io || !io->read32 || !bus_count || bus_count > 256U || !expected ||
	    !expected_valid(expected, bus_count))
		return -1;
	for (uint16_t bus = 0; bus < bus_count; bus++) {
		for (uint16_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
			const uint32_t vendor_device = io->read32(io->context, bus,
				devfn, PCI_VENDOR_DEVICE);
			struct dma_live_function *function;

			if ((uint16_t)vendor_device == UINT16_MAX)
				continue;
			if (snapshot->count == STARBOOK_MTL_DMA_LIVE_MAX_FUNCTIONS)
				return -1;
			function = &snapshot->functions[snapshot->count++];
			function->identity = (struct starbook_mtl_dma_live_identity) {
				.bdf = bus << 8 | devfn,
				.vendor = vendor_device,
				.device = vendor_device >> 16,
				.class = io->read32(io->context, bus, devfn,
					PCI_CLASS_REVISION) >> 8,
			};
			function->command = io->read32(io->context, bus, devfn,
				PCI_COMMAND);
			for (size_t index = 0;
			     index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++) {
				if (function->identity.bdf != expected[index].bdf)
					continue;
				if (!identity_equal(&function->identity, &expected[index]))
					return -1;
				found[index] = true;
			}
		}
	}
	if (!snapshot->count)
		return -1;
	for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++)
		if (!found[index])
			return -1;
	return 0;
}

static int verify_pci(const struct starbook_mtl_dma_live_pci_io *io,
	uint16_t bus_count, const struct dma_live_snapshot *snapshot)
{
	size_t index = 0;

	if (!io || !io->read32 || !bus_count || bus_count > 256U || !snapshot ||
	    !snapshot->count ||
	    snapshot->count > STARBOOK_MTL_DMA_LIVE_MAX_FUNCTIONS)
		return -1;
	for (uint16_t bus = 0; bus < bus_count; bus++) {
		for (uint16_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
			const uint32_t vendor_device = io->read32(io->context, bus,
				devfn, PCI_VENDOR_DEVICE);
			struct starbook_mtl_dma_live_identity identity;
			uint16_t command;

			if ((uint16_t)vendor_device == UINT16_MAX)
				continue;
			if (index == snapshot->count)
				return -1;
			identity = (struct starbook_mtl_dma_live_identity) {
				.bdf = bus << 8 | devfn,
				.vendor = vendor_device,
				.device = vendor_device >> 16,
				.class = io->read32(io->context, bus, devfn,
					PCI_CLASS_REVISION) >> 8,
			};
			command = io->read32(io->context, bus, devfn, PCI_COMMAND);
			if (!identity_equal(&identity,
				&snapshot->functions[index].identity) ||
			    (command & PCI_COMMAND_MASTER) ||
			    (command & ~PCI_COMMAND_MASTER) !=
				(snapshot->functions[index].command & ~PCI_COMMAND_MASTER))
				return -1;
			index++;
		}
	}
	return index == snapshot->count ? 0 : -1;
}

static int quiesce_pci(const struct starbook_mtl_dma_live_pci_io *io,
	uint16_t bus_count, const struct dma_live_snapshot *snapshot)
{
	if (!io || !io->write16)
		return -1;
	for (size_t index = 0; index < snapshot->count; index++) {
		const struct dma_live_function *function = &snapshot->functions[index];

		io->write16(io->context, function->identity.bdf >> 8,
			function->identity.bdf, PCI_COMMAND,
			function->command & ~PCI_COMMAND_MASTER);
	}
	return verify_pci(io, bus_count, snapshot);
}

static void terminal_quiesce_pci(const struct starbook_mtl_dma_live_pci_io *io,
	uint16_t bus_count)
{
	if (!io || !io->read32 || !io->write16 || !bus_count || bus_count > 256U)
		return;
	for (uint16_t bus = 0; bus < bus_count; bus++) {
		for (uint16_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
			uint32_t vendor_device = io->read32(io->context, bus, devfn,
				PCI_VENDOR_DEVICE);
			uint16_t command;

			if ((uint16_t)vendor_device == UINT16_MAX)
				continue;
			command = io->read32(io->context, bus, devfn, PCI_COMMAND);
			io->write16(io->context, bus, devfn, PCI_COMMAND,
				command & ~PCI_COMMAND_MASTER);
		}
	}
}

static size_t span_count(uint64_t base, uint64_t bytes, unsigned int shift)
{
	const uint64_t last = base + bytes - 1U;

	return (last >> shift) - (base >> shift) + 1U;
}

static int partition_buffer(void *memory, uint64_t physical_base, size_t size,
	struct starbook_mtl_dma_live_layout *layout)
{
	const uintptr_t virtual_base = (uintptr_t)memory;
	uint64_t usable_base;
	uint64_t usable_bytes;
	size_t table_pages;
	size_t total_pages;
	size_t arena_pages;

	if (!memory || !layout || size < 5U * DMA_LIVE_PAGE_SIZE ||
	    (virtual_base & (DMA_LIVE_PAGE_SIZE - 1U)) ||
	    (physical_base & (DMA_LIVE_PAGE_SIZE - 1U)) ||
	    size - 1U > (uintptr_t)-1 - virtual_base ||
	    size - 1U > UINT64_MAX - physical_base ||
	    physical_base + size > (1ULL << 48) || physical_base > UINT32_MAX ||
	    size - 1U > UINT32_MAX - physical_base ||
	    DMA_LIVE_HANDOFF_BYTES > DMA_LIVE_PAGE_SIZE)
		return -1;
	usable_base = physical_base + DMA_LIVE_PAGE_SIZE;
	usable_bytes = (size - DMA_LIVE_PAGE_SIZE) &
		~(uint64_t)(DMA_LIVE_PAGE_SIZE - 1U);
	total_pages = usable_bytes / DMA_LIVE_PAGE_SIZE;
	table_pages = 1U + STARBOOK_MTL_DMA_LIVE_REQUESTERS;
	for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++)
		table_pages += 1U + span_count(usable_base, usable_bytes, 39) +
			span_count(usable_base, usable_bytes, 30) +
			span_count(usable_base, usable_bytes, 21);
	if (table_pages > total_pages ||
	    total_pages - table_pages < STARBOOK_MTL_DMA_LIVE_REQUESTERS)
		return -1;
	arena_pages = (total_pages - table_pages) /
		STARBOOK_MTL_DMA_LIVE_REQUESTERS;
	if (!arena_pages || arena_pages > UINT32_MAX)
		return -1;
	memset(memory, 0, size);
	*layout = (struct starbook_mtl_dma_live_layout) {
		.handoff = memory,
		.handoff_capacity = DMA_LIVE_HANDOFF_BYTES,
		.table_memory = (uint8_t *)memory + DMA_LIVE_PAGE_SIZE,
		.table_physical = usable_base,
		.table_capacity_pages = table_pages,
	};
	for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++) {
		layout->arena_base[index] = usable_base +
			(table_pages + index * arena_pages) * DMA_LIVE_PAGE_SIZE;
		layout->arena_pages[index] = arena_pages;
	}
	return 0;
}

static int activate(struct starbook_mtl_dma_live_layout *layout,
	const struct starbook_mtl_dma_live_identity requesters[
		STARBOOK_MTL_DMA_LIVE_REQUESTERS],
	const struct vtd_transition_io *transition)
{
	struct vtd_translation_requester translation[
		STARBOOK_MTL_DMA_LIVE_REQUESTERS];
	struct vtd_translation_image image;

	memset(translation, 0, sizeof(translation));
	for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++) {
		translation[index] = (struct vtd_translation_requester) {
			.bdf = requesters[index].bdf,
			.domain = index + 1U,
			.cpu_base = layout->arena_base[index],
			.device_base = layout->arena_base[index],
			.pages = layout->arena_pages[index],
		};
	}
	image = (struct vtd_translation_image) {
		.memory = layout->table_memory,
		.physical_base = layout->table_physical,
		.capacity_pages = layout->table_capacity_pages,
	};
	if (vtd_translation_build(&image, translation,
		STARBOOK_MTL_DMA_LIVE_REQUESTERS))
		return -1;
	layout->table_used_pages = image.used_pages;
	return vtd_transition_from_pmr(transition, layout->table_physical);
}

int starbook_mtl_dma_live_establish(
	const struct starbook_mtl_dma_live_pci_io *pci_io, uint16_t bus_count,
	const struct starbook_mtl_dma_live_identity requesters[
		STARBOOK_MTL_DMA_LIVE_REQUESTERS],
	void *memory, uint64_t physical_base, size_t size,
	const struct vtd_transition_io *transition)
{
	struct vtd_transition_facts facts;

	if (live_state.phase != DMA_LIVE_EMPTY)
		return -1;
	live_state.phase = DMA_LIVE_FAILED;
	if (!pci_io || !requesters || !transition ||
	    vtd_transition_probe(transition, &facts) || !facts.coherent ||
	    (facts.status & (VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE)) ||
	    (facts.protected_memory_enable &
	     (VTD_PROTECTED_MEMORY_REQUEST | VTD_PROTECTED_MEMORY_ACTIVE)) !=
		(VTD_PROTECTED_MEMORY_REQUEST | VTD_PROTECTED_MEMORY_ACTIVE) ||
	    snapshot_pci(pci_io, bus_count, requesters, &live_state.snapshot) ||
	    quiesce_pci(pci_io, bus_count, &live_state.snapshot))
		return -1;
	if (verify_pci(pci_io, bus_count, &live_state.snapshot) ||
	    partition_buffer(memory, physical_base, size, &live_state.layout)) {
		terminal_quiesce_pci(pci_io, bus_count);
		return -1;
	}
	memcpy(live_state.requesters, requesters, sizeof(live_state.requesters));
	if (activate(&live_state.layout, live_state.requesters, transition) ||
	    verify_pci(pci_io, bus_count, &live_state.snapshot)) {
		terminal_quiesce_pci(pci_io, bus_count);
		return -1;
	}
	live_state.phase = DMA_LIVE_ACTIVE;
	return 0;
}

bool starbook_mtl_dma_live_verify_active(
	const struct starbook_mtl_dma_live_pci_io *pci_io, uint16_t bus_count)
{
	if (live_state.phase != DMA_LIVE_ACTIVE)
		return false;
	if (!verify_pci(pci_io, bus_count, &live_state.snapshot))
		return true;
	live_state.phase = DMA_LIVE_FAILED;
	terminal_quiesce_pci(pci_io, bus_count);
	return false;
}

const struct starbook_mtl_dma_live_layout *starbook_mtl_dma_live_layout(void)
{
	return live_state.phase == DMA_LIVE_ACTIVE ? &live_state.layout : NULL;
}

bool starbook_mtl_dma_live_handoff_requesters(
	struct dma_handoff_requester output[STARBOOK_MTL_DMA_LIVE_REQUESTERS])
{
	if (live_state.phase != DMA_LIVE_ACTIVE || !output)
		return false;
	memset(output, 0, STARBOOK_MTL_DMA_LIVE_REQUESTERS * sizeof(*output));
	for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++) {
		output[index] = (struct dma_handoff_requester) {
			.bdf = live_state.requesters[index].bdf,
			.protection_domain = index + 1U,
			.flags = DMA_HANDOFF_REQUESTER_FLAGS,
			.arena_cpu_base = live_state.layout.arena_base[index],
			.arena_device_base = live_state.layout.arena_base[index],
			.arena_pages = live_state.layout.arena_pages[index],
			.arena_flags = DMA_HANDOFF_ARENA_FLAGS,
		};
	}
	return true;
}
