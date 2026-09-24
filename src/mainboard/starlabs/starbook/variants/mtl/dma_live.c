/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_live.h"

#include <string.h>

#include "../../../../../soc/intel/common/block/vtd/vtd_transition.h"
#include "../../../../../soc/intel/common/block/vtd/vtd_translation.h"

#define DMA_LIVE_PAGE_SIZE VTD_TRANSLATION_PAGE_SIZE
#define DMA_LIVE_HANDOFF_BYTES \
	(sizeof(struct dma_handoff_header) + STARBOOK_MTL_DMA_LIVE_REQUESTERS * \
	 sizeof(struct dma_handoff_requester))
#define VTD_ROOT_POINTER_SET (1U << 30)
#define VTD_TRANSLATION_ENABLE (1U << 31)
#define VTD_PROTECTED_MEMORY_ACTIVE (1U << 0)
#define VTD_PROTECTED_MEMORY_REQUEST (1U << 31)

enum dma_live_phase {
	DMA_LIVE_EMPTY,
	DMA_LIVE_EARLY_PREPARED,
	DMA_LIVE_FAILED,
	DMA_LIVE_ACTIVE,
};

struct dma_live_state {
	enum dma_live_phase phase;
	struct pci_bme_quiesce_snapshot snapshot;
	struct pci_bme_quiesce_snapshot snapshot_workspace;
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

static bool expected_snapshot_valid(
	const struct pci_bme_quiesce_snapshot *snapshot,
	const struct starbook_mtl_dma_live_identity expected[
		STARBOOK_MTL_DMA_LIVE_REQUESTERS])
{
	bool found[STARBOOK_MTL_DMA_LIVE_REQUESTERS] = { false };

	if (!snapshot || snapshot->failed)
		return false;
	for (size_t function = 0; function < snapshot->count; function++)
		for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++) {
			const struct pci_bme_quiesce_function *entry =
				&snapshot->functions[function];
			struct starbook_mtl_dma_live_identity identity = {
				.bdf = entry->bdf, .vendor = entry->vendor,
				.device = entry->device, .class = entry->class,
			};

			if (identity.bdf == expected[index].bdf) {
				if (!identity_equal(&identity, &expected[index]))
					return false;
				found[index] = true;
			}
		}
	for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++)
		if (!found[index])
			return false;
	return true;
}

int starbook_mtl_dma_live_prepare_early(
	const struct pci_bme_quiesce_snapshot *snapshot)
{
	const uintptr_t base = (uintptr_t)snapshot;

	if (live_state.phase != DMA_LIVE_EMPTY || !snapshot ||
	    base % _Alignof(*snapshot) ||
	    base > (uintptr_t)-1 - (sizeof(*snapshot) - 1U) || snapshot->failed ||
	    !snapshot->bus_count || snapshot->bus_count > 256U || !snapshot->count ||
	    snapshot->count > PCI_BME_QUIESCE_MAX_FUNCTIONS)
		return -1;
	live_state.phase = DMA_LIVE_FAILED;
	for (size_t index = 0; index < snapshot->count; index++) {
		const struct pci_bme_quiesce_function *function =
			&snapshot->functions[index];

		if ((function->command & (1U << 2)) ||
		    (index && function[-1].bdf >= function->bdf) ||
		    (function->bdf >> 8) >= snapshot->bus_count)
			return -1;
	}
	for (size_t index = snapshot->count;
	     index < PCI_BME_QUIESCE_MAX_FUNCTIONS; index++)
		if (memcmp(&snapshot->functions[index],
			&(const struct pci_bme_quiesce_function) { 0 },
			sizeof(snapshot->functions[index])))
			return -1;
	memcpy(&live_state.snapshot, snapshot, sizeof(live_state.snapshot));
	live_state.phase = DMA_LIVE_EARLY_PREPARED;
	return 0;
}

static size_t span_count(uint64_t base, uint64_t bytes, unsigned int shift)
{
	const uint64_t last = base + bytes - 1U;

	return (last >> shift) - (base >> shift) + 1U;
}

static bool buffer_range_valid(const void *memory, uint64_t physical_base,
	size_t size)
{
	const uintptr_t virtual_base = (uintptr_t)memory;

	return memory && size >= 5U * DMA_LIVE_PAGE_SIZE &&
		!(size & (DMA_LIVE_PAGE_SIZE - 1U)) &&
		!(virtual_base & (DMA_LIVE_PAGE_SIZE - 1U)) &&
		!(physical_base & (DMA_LIVE_PAGE_SIZE - 1U)) &&
		size - 1U <= (uintptr_t)-1 - virtual_base &&
		size - 1U <= UINT64_MAX - physical_base &&
		physical_base + size <= (1ULL << 48) &&
		physical_base <= UINT32_MAX &&
		size - 1U <= UINT32_MAX - physical_base &&
		DMA_LIVE_HANDOFF_BYTES <= DMA_LIVE_PAGE_SIZE;
}

int starbook_mtl_dma_live_table_mirror_size(uint64_t physical_base, size_t size,
	size_t *mirror_size)
{
	uint64_t usable_base;
	uint64_t usable_bytes;
	size_t table_pages;

	if (!mirror_size || !size || (size & (DMA_LIVE_PAGE_SIZE - 1U)) ||
	    (physical_base & (DMA_LIVE_PAGE_SIZE - 1U)) ||
	    size < 4U * DMA_LIVE_PAGE_SIZE || size - 1U > UINT64_MAX - physical_base)
		return -1;
	usable_base = physical_base + DMA_LIVE_PAGE_SIZE;
	usable_bytes = size - DMA_LIVE_PAGE_SIZE;
	table_pages = 1U + STARBOOK_MTL_DMA_LIVE_REQUESTERS;
	for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++)
		table_pages += 1U + span_count(usable_base, usable_bytes, 39) +
			span_count(usable_base, usable_bytes, 30) +
			span_count(usable_base, usable_bytes, 21);
	if (table_pages > SIZE_MAX / DMA_LIVE_PAGE_SIZE ||
	    table_pages + STARBOOK_MTL_DMA_LIVE_REQUESTERS >
		size / DMA_LIVE_PAGE_SIZE - 1U)
		return -1;
	*mirror_size = table_pages * DMA_LIVE_PAGE_SIZE;
	return 0;
}

static int partition_buffer(void *memory, uint64_t physical_base, size_t size,
	void *table_mirror, uint64_t table_mirror_physical,
	size_t table_mirror_size, struct starbook_mtl_dma_live_layout *layout)
{
	uint64_t usable_base;
	uint64_t usable_bytes;
	size_t table_pages;
	size_t total_pages;
	size_t remaining_pages;
	size_t arena_page = 0;

	if (!layout || !buffer_range_valid(memory, physical_base, size) ||
	    !table_mirror || !table_mirror_size ||
	    table_mirror_size % DMA_LIVE_PAGE_SIZE ||
	    ((uintptr_t)table_mirror & (DMA_LIVE_PAGE_SIZE - 1U)) ||
	    (table_mirror_physical & (DMA_LIVE_PAGE_SIZE - 1U)) ||
	    (uintptr_t)table_mirror > (uintptr_t)-1 - (table_mirror_size - 1U) ||
	    table_mirror_physical > UINT64_MAX - (table_mirror_size - 1U) ||
	    ((uintptr_t)memory <= (uintptr_t)table_mirror + table_mirror_size - 1U &&
	     (uintptr_t)table_mirror <= (uintptr_t)memory + size - 1U) ||
	    (physical_base <= table_mirror_physical + table_mirror_size - 1U &&
	     table_mirror_physical <= physical_base + size - 1U))
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
	if (table_mirror_size != table_pages * DMA_LIVE_PAGE_SIZE ||
	    table_pages > total_pages ||
	    total_pages - table_pages < STARBOOK_MTL_DMA_LIVE_REQUESTERS)
		return -1;
	remaining_pages = total_pages - table_pages;
	memset(memory, 0, size);
	memset(table_mirror, 0, table_mirror_size);
	*layout = (struct starbook_mtl_dma_live_layout) {
		.buffer = memory,
		.buffer_physical = physical_base,
		.buffer_size = size,
		.handoff = memory,
		.handoff_capacity = DMA_LIVE_HANDOFF_BYTES,
		.table_memory = (uint8_t *)memory + DMA_LIVE_PAGE_SIZE,
		.table_physical = usable_base,
		.table_mirror = table_mirror,
		.table_mirror_physical = table_mirror_physical,
		.table_capacity_pages = table_pages,
	};
	for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++) {
		const size_t requester_count =
			STARBOOK_MTL_DMA_LIVE_REQUESTERS - index;
		const size_t arena_pages = remaining_pages / requester_count;

		if (!arena_pages || arena_pages > UINT32_MAX)
			return -1;
		layout->arena_base[index] = usable_base +
			(table_pages + arena_page) * DMA_LIVE_PAGE_SIZE;
		layout->arena_pages[index] = arena_pages;
		arena_page += arena_pages;
		remaining_pages -= arena_pages;
	}
	if (remaining_pages)
		return -1;
	return 0;
}

int starbook_mtl_dma_live_establish(
	const struct pci_bme_quiesce_io *pci_io, uint16_t bus_count,
	const struct starbook_mtl_dma_live_identity requesters[
		STARBOOK_MTL_DMA_LIVE_REQUESTERS],
	void *memory, uint64_t physical_base, size_t size,
	void *table_mirror, uint64_t table_mirror_physical,
	size_t table_mirror_size,
	const struct vtd_transition_io *transition)
{
	struct vtd_transition_facts facts;
	struct vtd_translation_requester translation[
		STARBOOK_MTL_DMA_LIVE_REQUESTERS] = { 0 };
	struct vtd_translation_image image;

	const bool early_prepared = live_state.phase == DMA_LIVE_EARLY_PREPARED;

	if (live_state.phase != DMA_LIVE_EMPTY && !early_prepared)
		return -1;
	live_state.phase = DMA_LIVE_FAILED;
	if (!pci_io || !requesters || !transition ||
	    !buffer_range_valid(memory, physical_base, size) ||
	    vtd_transition_probe(transition, &facts) || !facts.coherent ||
	    (facts.status & (VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE)) ||
	    (facts.protected_memory_enable &
	     (VTD_PROTECTED_MEMORY_REQUEST | VTD_PROTECTED_MEMORY_ACTIVE)) !=
		(VTD_PROTECTED_MEMORY_REQUEST | VTD_PROTECTED_MEMORY_ACTIVE) ||
	    !expected_valid(requesters, bus_count) ||
	    (early_prepared ?
		(pci_bme_quiesce_revalidate(pci_io, &live_state.snapshot,
			&live_state.snapshot_workspace) != CB_SUCCESS) :
		(pci_bme_quiesce(pci_io, bus_count, &live_state.snapshot,
			&live_state.snapshot_workspace) != CB_SUCCESS)) ||
	    live_state.snapshot.bus_count != bus_count ||
	    !expected_snapshot_valid(&live_state.snapshot, requesters)) {
		if (early_prepared)
			pci_bme_quiesce_terminal(pci_io,
				live_state.snapshot.bus_count);
		return -1;
	}
	if (pci_bme_quiesce_revalidate(pci_io, &live_state.snapshot,
		&live_state.snapshot_workspace) ||
	    partition_buffer(memory, physical_base, size, table_mirror,
		table_mirror_physical, table_mirror_size, &live_state.layout)) {
		pci_bme_quiesce_terminal(pci_io, bus_count);
		return -1;
	}
	memcpy(live_state.requesters, requesters, sizeof(live_state.requesters));
	for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++)
		translation[index] = (struct vtd_translation_requester) {
			.bdf = live_state.requesters[index].bdf,
			.domain = index + 1U,
			.cpu_base = live_state.layout.arena_base[index],
			.device_base = live_state.layout.arena_base[index],
			.pages = live_state.layout.arena_pages[index],
		};
	image = (struct vtd_translation_image) {
		.memory = live_state.layout.table_memory,
		.physical_base = live_state.layout.table_physical,
		.capacity_pages = live_state.layout.table_capacity_pages,
	};
	if (vtd_translation_build(&image, translation,
		STARBOOK_MTL_DMA_LIVE_REQUESTERS))
		return -1;
	live_state.layout.table_used_pages = image.used_pages;
	memcpy(live_state.layout.table_mirror, live_state.layout.table_memory,
		live_state.layout.table_used_pages * DMA_LIVE_PAGE_SIZE);
	if (pci_bme_quiesce_revalidate(pci_io, &live_state.snapshot,
		&live_state.snapshot_workspace) ||
	    !starbook_mtl_dma_live_tables_match(&live_state.layout) ||
	    vtd_transition_from_pmr(transition, live_state.layout.table_physical) ||
	    !starbook_mtl_dma_live_tables_match(&live_state.layout) ||
	    pci_bme_quiesce_revalidate(pci_io, &live_state.snapshot,
		&live_state.snapshot_workspace)) {
		pci_bme_quiesce_terminal(pci_io, bus_count);
		return -1;
	}
	live_state.phase = DMA_LIVE_ACTIVE;
	return 0;
}

bool starbook_mtl_dma_live_verify_active(
	const struct pci_bme_quiesce_io *pci_io, uint16_t bus_count)
{
	if (live_state.phase != DMA_LIVE_ACTIVE)
		return false;
	if (!pci_bme_quiesce_revalidate(pci_io, &live_state.snapshot,
		&live_state.snapshot_workspace))
		return true;
	live_state.phase = DMA_LIVE_FAILED;
	pci_bme_quiesce_terminal(pci_io, bus_count);
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

bool starbook_mtl_dma_live_devices_are_verified(
	const struct pci_bme_quiesce_io *pci_io, uint16_t bus_count,
	const uint16_t *bdfs, size_t count)
{
	if (!starbook_mtl_dma_live_verify_active(pci_io, bus_count) ||
	    !bdfs || !count)
		return false;
	for (size_t wanted = 0; wanted < count; wanted++) {
		bool found = false;

		for (size_t index = 0; index < live_state.snapshot.count; index++)
			found |= live_state.snapshot.functions[index].bdf ==
				bdfs[wanted];
		if (!found)
			return false;
	}
	return true;
}

void starbook_mtl_dma_live_poison(
	const struct pci_bme_quiesce_io *pci_io, uint16_t bus_count)
{
	live_state.phase = DMA_LIVE_FAILED;
	pci_bme_quiesce_terminal(pci_io, bus_count);
}

bool starbook_mtl_dma_live_tables_match(
	const struct starbook_mtl_dma_live_layout *layout)
{
	if (!layout || !layout->table_memory || !layout->table_mirror ||
	    !layout->table_used_pages ||
	    layout->table_used_pages > layout->table_capacity_pages ||
	    layout->table_capacity_pages > SIZE_MAX / DMA_LIVE_PAGE_SIZE)
		return false;
	return !memcmp(layout->table_memory, layout->table_mirror,
		layout->table_capacity_pages * DMA_LIVE_PAGE_SIZE);
}
