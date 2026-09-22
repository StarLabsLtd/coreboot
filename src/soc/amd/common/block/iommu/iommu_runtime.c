/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/iommu_runtime.h>
#include <string.h>

#define IOMMU_CONTROL_ENABLE		BIT(0)
#define IOMMU_CONTROL_SEGMENT_SHIFT	34
#define IOMMU_CONTROL_SEGMENT_MASK	(7ULL << IOMMU_CONTROL_SEGMENT_SHIFT)

#define IOMMU_EFR_SEGMENT_SUPPORT_SHIFT	38
#define IOMMU_EFR_SEGMENT_SUPPORT_MASK	(3ULL << IOMMU_EFR_SEGMENT_SUPPORT_SHIFT)

#define DEVICE_TABLE_BASE_MASK		0x000ffffffffff000ULL
#define DEVICE_TABLE_SIZE_MASK		0x1ffULL
#define DEVICE_TABLE_REGISTER_MASK	(DEVICE_TABLE_BASE_MASK | DEVICE_TABLE_SIZE_MASK)
#define DEVICE_TABLE_ENTRY_SIZE		AMD_IOMMU_DEVICE_TABLE_ENTRY_SIZE
#define DEVICE_ID_COUNT			BIT(16)

#define DTE_VALID			BIT(0)
#define DTE_TRANSLATION_VALID		BIT(1)
#define DTE_MODE_ONE_LEVEL		(1ULL << 9)
#define DTE_READ			BIT(61)
#define DTE_WRITE			BIT(62)
#define DTE_ROOT_MASK			0x000ffffffffff000ULL
#define DTE_DOMAIN_MASK			0xffffULL

#define PTE_PRESENT			BIT(0)
#define PTE_FORCE_COHERENT		BIT(60)
#define PTE_READ				BIT(61)
#define PTE_WRITE			BIT(62)
#define PTE_ADDRESS_MASK		0x000ffffffffff000ULL
#define AMD_IOMMU_ONE_LEVEL_ENTRIES	512U

static bool ranges_overlap(uint64_t first_base, uint64_t first_size,
			   uint64_t second_base, uint64_t second_size)
{
	return first_base < second_base + second_size &&
		second_base < first_base + first_size;
}

static bool device_table_valid(const struct amd_iommu_device_table *table)
{
	uint32_t entries_per_segment;
	uint8_t i;

	if (!table || !table->segment_count ||
	    table->segment_count > AMD_IOMMU_DEVICE_TABLE_SEGMENTS_MAX ||
	    (table->segment_count & (table->segment_count - 1)))
		return false;

	entries_per_segment = DEVICE_ID_COUNT / table->segment_count;
	for (i = 0; i < table->segment_count; i++) {
		uint8_t previous;
		const uint64_t size = (uint64_t)table->segment[i].entries *
			DEVICE_TABLE_ENTRY_SIZE;

		if (!table->segment[i].base ||
		    (table->segment[i].base & ~DEVICE_TABLE_BASE_MASK) ||
		    !table->segment[i].entries ||
		    table->segment[i].entries > entries_per_segment)
			return false;

		for (previous = 0; previous < i; previous++) {
			const uint64_t previous_size =
				(uint64_t)table->segment[previous].entries *
				DEVICE_TABLE_ENTRY_SIZE;

			if (ranges_overlap(table->segment[i].base, size,
					   table->segment[previous].base, previous_size))
				return false;
		}
	}

	return true;
}

enum cb_err amd_iommu_decode_device_table(uint64_t control,
					  uint64_t extended_features,
					  const uint64_t *segment_register,
					  struct amd_iommu_device_table *table)
{
	uint8_t enabled_segments;
	uint8_t supported_segments;
	uint8_t segment_count;
	uint8_t i;

	if (!table)
		return CB_ERR_ARG;
	memset(table, 0, sizeof(*table));
	if (!segment_register || !(control & IOMMU_CONTROL_ENABLE))
		return CB_ERR_ARG;

	enabled_segments = (control & IOMMU_CONTROL_SEGMENT_MASK) >>
		IOMMU_CONTROL_SEGMENT_SHIFT;
	supported_segments = (extended_features & IOMMU_EFR_SEGMENT_SUPPORT_MASK) >>
		IOMMU_EFR_SEGMENT_SUPPORT_SHIFT;
	if (enabled_segments > supported_segments || enabled_segments > 3)
		return CB_ERR_ARG;
	segment_count = 1U << enabled_segments;
	table->segment_count = segment_count;

	for (i = 0; i < segment_count; i++) {
		const uint64_t value = segment_register[i];
		const uint64_t base = value & DEVICE_TABLE_BASE_MASK;
		const uint64_t pages = (value & DEVICE_TABLE_SIZE_MASK) + 1;
		const uint64_t size = pages << 12;

		if (value & ~DEVICE_TABLE_REGISTER_MASK)
			goto invalid;

		table->segment[i].base = base;
		table->segment[i].entries = size / DEVICE_TABLE_ENTRY_SIZE;
	}
	if (!device_table_valid(table))
		goto invalid;

	return CB_SUCCESS;

invalid:
	memset(table, 0, sizeof(*table));
	return CB_ERR_ARG;
}

enum cb_err
amd_iommu_device_table_entry_address(const struct amd_iommu_device_table *table,
				     uint16_t device_id, uint64_t *address)
{
	uint32_t entries_per_segment;
	uint32_t segment;
	uint32_t index;

	if (!address || !device_table_valid(table))
		return CB_ERR_ARG;

	entries_per_segment = DEVICE_ID_COUNT / table->segment_count;
	segment = device_id / entries_per_segment;
	index = device_id % entries_per_segment;
	if (index >= table->segment[segment].entries ||
	    !table->segment[segment].base)
		return CB_ERR_ARG;

	*address = table->segment[segment].base +
		(uint64_t)index * DEVICE_TABLE_ENTRY_SIZE;
	return CB_SUCCESS;
}

size_t amd_iommu_device_table_bytes(uint16_t maximum_device_id)
{
	const size_t entries = (size_t)maximum_device_id + 1U;
	const size_t bytes = entries * DEVICE_TABLE_ENTRY_SIZE;

	return ALIGN_UP(bytes, AMD_IOMMU_PAGE_SIZE);
}

static bool dma_requesters_valid(const struct amd_iommu_dma_requester *requesters,
				 size_t requester_count, size_t device_table_bytes,
				 size_t page_table_bytes)
{
	if (!requesters || !requester_count ||
	    device_table_bytes < AMD_IOMMU_PAGE_SIZE ||
	    device_table_bytes % AMD_IOMMU_PAGE_SIZE ||
	    page_table_bytes != requester_count * AMD_IOMMU_PAGE_SIZE)
		return false;

	for (size_t index = 0; index < requester_count; index++) {
		const struct amd_iommu_dma_requester *requester = &requesters[index];
		const uint64_t arena_bytes =
			(uint64_t)requester->arena_pages * AMD_IOMMU_PAGE_SIZE;
		const uint64_t aperture =
			(uint64_t)AMD_IOMMU_ONE_LEVEL_ENTRIES * AMD_IOMMU_PAGE_SIZE;

		if (!requester->protection_domain || !requester->arena_cpu_base ||
		    !requester->arena_device_base || !requester->arena_pages ||
		    requester->arena_pages >= AMD_IOMMU_ONE_LEVEL_ENTRIES ||
		    (requester->arena_cpu_base & (AMD_IOMMU_PAGE_SIZE - 1U)) ||
		    (requester->arena_device_base & (AMD_IOMMU_PAGE_SIZE - 1U)) ||
		    requester->arena_cpu_base > PTE_ADDRESS_MASK -
			(arena_bytes - AMD_IOMMU_PAGE_SIZE) ||
		    requester->arena_cpu_base > UINT64_MAX - arena_bytes ||
		    requester->arena_device_base >= aperture ||
		    arena_bytes > aperture - requester->arena_device_base ||
		    ((size_t)requester->device_id + 1U) * DEVICE_TABLE_ENTRY_SIZE >
			device_table_bytes)
			return false;
		for (size_t other = 0; other < index; other++) {
			const struct amd_iommu_dma_requester *previous = &requesters[other];
			const uint64_t previous_bytes =
				(uint64_t)previous->arena_pages * AMD_IOMMU_PAGE_SIZE;

			if (previous->device_id == requester->device_id ||
			    previous->protection_domain == requester->protection_domain ||
			    ranges_overlap(previous->arena_cpu_base, previous_bytes,
					   requester->arena_cpu_base, arena_bytes))
				return false;
		}
	}
	return true;
}

static uint64_t expected_dte0(uint64_t page_table)
{
	return (page_table & DTE_ROOT_MASK) | DTE_VALID | DTE_TRANSLATION_VALID |
		DTE_MODE_ONE_LEVEL | DTE_READ | DTE_WRITE;
}

static uint64_t expected_pte(uint64_t address)
{
	return (address & PTE_ADDRESS_MASK) | PTE_PRESENT | PTE_FORCE_COHERENT |
		PTE_READ | PTE_WRITE;
}

enum cb_err amd_iommu_build_dma_state(void *device_table, size_t device_table_bytes,
				      void *page_tables, size_t page_table_bytes,
				      const struct amd_iommu_dma_requester *requesters,
				      size_t requester_count)
{
	uint64_t *device_words = device_table;
	uint64_t *page_words = page_tables;

	if (!device_table || !page_tables ||
	    ((uintptr_t)device_table & (AMD_IOMMU_PAGE_SIZE - 1U)) ||
	    ((uintptr_t)page_tables & (AMD_IOMMU_PAGE_SIZE - 1U)) ||
	    !dma_requesters_valid(requesters, requester_count, device_table_bytes,
		page_table_bytes))
		return CB_ERR_ARG;

	memset(device_table, 0, device_table_bytes);
	memset(page_tables, 0, page_table_bytes);
	for (size_t index = 0; index < requester_count; index++) {
		const struct amd_iommu_dma_requester *requester = &requesters[index];
		uint64_t *dte = &device_words[(size_t)requester->device_id * 4U];
		uint64_t *page_table = &page_words[index * AMD_IOMMU_ONE_LEVEL_ENTRIES];
		const size_t first_page = requester->arena_device_base >> 12;

		dte[0] = expected_dte0((uintptr_t)page_table);
		dte[1] = requester->protection_domain & DTE_DOMAIN_MASK;
		for (size_t page = 0; page < requester->arena_pages; page++)
			page_table[first_page + page] = expected_pte(
				requester->arena_cpu_base + page * AMD_IOMMU_PAGE_SIZE);
	}
	return CB_SUCCESS;
}

bool amd_iommu_dma_state_matches(const void *device_table, size_t device_table_bytes,
				 const void *page_tables, size_t page_table_bytes,
				 const struct amd_iommu_dma_requester *requesters,
				 size_t requester_count)
{
	const uint64_t *device_words = device_table;
	const uint64_t *page_words = page_tables;

	if (!device_table || !page_tables ||
	    ((uintptr_t)device_table & (AMD_IOMMU_PAGE_SIZE - 1U)) ||
	    ((uintptr_t)page_tables & (AMD_IOMMU_PAGE_SIZE - 1U)) ||
	    !dma_requesters_valid(requesters, requester_count, device_table_bytes,
		page_table_bytes))
		return false;

	for (size_t word = 0; word < device_table_bytes / sizeof(*device_words); word++) {
		uint64_t expected = 0;

		for (size_t index = 0; index < requester_count; index++) {
			const size_t dte_word = (size_t)requesters[index].device_id * 4U;

			if (word == dte_word)
				expected = expected_dte0((uintptr_t)&page_words[
					index * AMD_IOMMU_ONE_LEVEL_ENTRIES]);
			else if (word == dte_word + 1U)
				expected = requesters[index].protection_domain;
		}
		if (device_words[word] != expected)
			return false;
	}
	for (size_t index = 0; index < requester_count; index++) {
		const struct amd_iommu_dma_requester *requester = &requesters[index];
		const size_t first_page = requester->arena_device_base >> 12;

		for (size_t page = 0; page < AMD_IOMMU_ONE_LEVEL_ENTRIES; page++) {
			uint64_t expected = 0;

			if (page >= first_page &&
			    page < first_page + requester->arena_pages)
				expected = expected_pte(requester->arena_cpu_base +
					(page - first_page) * AMD_IOMMU_PAGE_SIZE);
			if (page_words[index * AMD_IOMMU_ONE_LEVEL_ENTRIES + page] != expected)
				return false;
		}
	}
	return true;
}
