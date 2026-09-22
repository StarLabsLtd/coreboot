/* SPDX-License-Identifier: GPL-2.0-only */

#include "vtd_translation.h"

#include <stdbool.h>
#include <string.h>

#define VTD_ENTRY_PRESENT (1ULL << 0)
#define VTD_ENTRY_WRITE (1ULL << 1)
#define VTD_ENTRY_ADDRESS_MASK 0x000ffffffffff000ULL
#define VTD_CONTEXT_ADDRESS_WIDTH_48 2ULL

static bool range_valid(uint64_t base, uint32_t pages)
{
	const uint64_t bytes = (uint64_t)pages << VTD_TRANSLATION_PAGE_SHIFT;

	return base && !(base & (VTD_TRANSLATION_PAGE_SIZE - 1U)) && pages &&
		base <= UINT64_MAX - bytes &&
		base + bytes <= (1ULL << 48);
}

static uint64_t page_physical(const struct vtd_translation_image *image,
	size_t page)
{
	return image->physical_base + page * VTD_TRANSLATION_PAGE_SIZE;
}

static uint64_t *page_virtual(const struct vtd_translation_image *image,
	size_t page)
{
	return (void *)((uint8_t *)image->memory +
		page * VTD_TRANSLATION_PAGE_SIZE);
}

static int allocate_page(struct vtd_translation_image *image, size_t *page)
{
	if (image->used_pages >= image->capacity_pages)
		return -1;
	*page = image->used_pages++;
	memset(page_virtual(image, *page), 0, VTD_TRANSLATION_PAGE_SIZE);
	return 0;
}

static int entry_page(const struct vtd_translation_image *image,
	uint64_t entry, size_t *page)
{
	const uint64_t address = entry & VTD_ENTRY_ADDRESS_MASK;
	uint64_t offset;

	if (!(entry & VTD_ENTRY_PRESENT) || address < image->physical_base)
		return -1;
	offset = address - image->physical_base;
	if (offset & (VTD_TRANSLATION_PAGE_SIZE - 1U))
		return -1;
	*page = offset >> VTD_TRANSLATION_PAGE_SHIFT;
	return *page < image->used_pages ? 0 : -1;
}

static int child_page(struct vtd_translation_image *image, uint64_t *entry,
	size_t *page)
{
	if (*entry & VTD_ENTRY_PRESENT)
		return entry_page(image, *entry, page);
	if (allocate_page(image, page))
		return -1;
	*entry = page_physical(image, *page) |
		VTD_ENTRY_PRESENT | VTD_ENTRY_WRITE;
	return 0;
}

static int map_page(struct vtd_translation_image *image, size_t pml4_page,
	uint64_t device, uint64_t cpu)
{
	uint64_t *table;
	size_t page;

	table = page_virtual(image, pml4_page);
	if (child_page(image, &table[(device >> 39) & 0x1ffU], &page))
		return -1;
	table = page_virtual(image, page);
	if (child_page(image, &table[(device >> 30) & 0x1ffU], &page))
		return -1;
	table = page_virtual(image, page);
	if (child_page(image, &table[(device >> 21) & 0x1ffU], &page))
		return -1;
	table = page_virtual(image, page);
	if (table[(device >> 12) & 0x1ffU] & VTD_ENTRY_PRESENT)
		return -1;
	table[(device >> 12) & 0x1ffU] = cpu |
		VTD_ENTRY_PRESENT | VTD_ENTRY_WRITE;
	return 0;
}

static int requester_valid(const struct vtd_translation_requester *requesters,
	size_t index)
{
	const struct vtd_translation_requester *requester = &requesters[index];

	if (requester->segment || !requester->domain ||
	    !range_valid(requester->cpu_base, requester->pages) ||
	    !range_valid(requester->device_base, requester->pages))
		return -1;
	for (size_t prior = 0; prior < index; prior++) {
		const struct vtd_translation_requester *other = &requesters[prior];
		const uint64_t bytes = (uint64_t)requester->pages <<
			VTD_TRANSLATION_PAGE_SHIFT;
		const uint64_t other_bytes = (uint64_t)other->pages <<
			VTD_TRANSLATION_PAGE_SHIFT;

		if (requester->bdf == other->bdf ||
		    requester->domain == other->domain ||
		    (requester->cpu_base < other->cpu_base + other_bytes &&
		     other->cpu_base < requester->cpu_base + bytes))
			return -1;
	}
	return 0;
}

int vtd_translation_build(struct vtd_translation_image *image,
	const struct vtd_translation_requester *requesters, size_t requester_count)
{
	uint64_t *root;
	size_t root_page;

	if (!image || !image->memory || !requesters || !requester_count ||
	    !image->capacity_pages ||
	    ((uintptr_t)image->memory & (VTD_TRANSLATION_PAGE_SIZE - 1U)) ||
	    (image->physical_base & (VTD_TRANSLATION_PAGE_SIZE - 1U)) ||
	    image->capacity_pages > (UINT64_MAX - image->physical_base) /
		VTD_TRANSLATION_PAGE_SIZE ||
	    image->physical_base + image->capacity_pages *
		VTD_TRANSLATION_PAGE_SIZE > (1ULL << 48))
		return -1;
	image->used_pages = 0;
	if (allocate_page(image, &root_page) || root_page)
		return -1;
	root = page_virtual(image, root_page);

	for (size_t index = 0; index < requester_count; index++) {
		const struct vtd_translation_requester *requester = &requesters[index];
		const uint8_t bus = requester->bdf >> 8;
		const uint8_t devfn = requester->bdf;
		uint64_t *context;
		size_t context_page;
		size_t pml4_page;

		if (requester_valid(requesters, index))
			return -1;
		if (root[(size_t)bus * 2U] & VTD_ENTRY_PRESENT) {
			if (entry_page(image, root[(size_t)bus * 2U], &context_page))
				return -1;
		} else {
			if (allocate_page(image, &context_page))
				return -1;
			root[(size_t)bus * 2U] = page_physical(image, context_page) |
				VTD_ENTRY_PRESENT;
		}
		context = page_virtual(image, context_page);
		if (context[(size_t)devfn * 2U] & VTD_ENTRY_PRESENT ||
		    allocate_page(image, &pml4_page))
			return -1;
		context[(size_t)devfn * 2U] = page_physical(image, pml4_page) |
			VTD_ENTRY_PRESENT;
		context[(size_t)devfn * 2U + 1U] =
			((uint64_t)requester->domain << 8) |
			VTD_CONTEXT_ADDRESS_WIDTH_48;
		for (uint32_t arena_page = 0; arena_page < requester->pages;
		     arena_page++) {
			const uint64_t offset = (uint64_t)arena_page <<
				VTD_TRANSLATION_PAGE_SHIFT;

			if (map_page(image, pml4_page,
				requester->device_base + offset,
				requester->cpu_base + offset))
				return -1;
		}
	}
	return 0;
}
