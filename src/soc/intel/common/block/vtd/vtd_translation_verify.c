/* SPDX-License-Identifier: GPL-2.0-only */

#include "vtd_translation.h"
#include "vtd_translation_internal.h"

#include <stdbool.h>

#define VTD_TRANSLATION_ENTRIES_PER_PAGE \
	(VTD_TRANSLATION_PAGE_SIZE / sizeof(uint64_t))

static bool range_valid(uint64_t base, uint32_t pages)
{
	const uint64_t bytes = (uint64_t)pages << VTD_TRANSLATION_PAGE_SHIFT;

	return base && !(base & (VTD_TRANSLATION_PAGE_SIZE - 1U)) && pages &&
		base <= UINT64_MAX - bytes && base + bytes <= (1ULL << 48);
}

static int read_entry(const struct vtd_translation_view *view, size_t page,
	size_t entry, uint64_t *value)
{
	uint64_t offset;

	if (page >= view->capacity_pages ||
	    entry >= VTD_TRANSLATION_ENTRIES_PER_PAGE)
		return -1;
	offset = (uint64_t)page * VTD_TRANSLATION_PAGE_SIZE +
		entry * sizeof(*value);
	if (offset > UINT64_MAX - view->physical_base)
		return -1;
	return view->read64(view->context, view->physical_base + offset, value);
}

static int requester_valid(const struct vtd_translation_view *view,
	const struct vtd_translation_requester *requesters, size_t index)
{
	const struct vtd_translation_requester *requester = &requesters[index];
	const uint64_t table_end = view->physical_base +
		view->capacity_pages * VTD_TRANSLATION_PAGE_SIZE;
	const uint64_t bytes = (uint64_t)requester->pages <<
		VTD_TRANSLATION_PAGE_SHIFT;

	if (requester->segment || !requester->domain ||
	    !range_valid(requester->cpu_base, requester->pages) ||
	    !range_valid(requester->device_base, requester->pages) ||
	    (requester->cpu_base < table_end &&
	     view->physical_base < requester->cpu_base + bytes))
		return -1;
	for (size_t prior = 0; prior < index; prior++) {
		const struct vtd_translation_requester *other = &requesters[prior];
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

static int exact_entry(const struct vtd_translation_view *view, size_t page,
	size_t entry, uint64_t expected)
{
	uint64_t observed;

	return read_entry(view, page, entry, &observed) || observed != expected ?
		-1 : 0;
}

static int verify_level(const struct vtd_translation_view *view, size_t page,
	unsigned int level, uint64_t device_base, uint64_t cpu_base,
	uint64_t bytes, size_t *next_page)
{
	const unsigned int shift = VTD_TRANSLATION_PAGE_SHIFT + 9U * level;
	const uint64_t entry_span = 1ULL << shift;
	const uint64_t first = device_base >> shift & 0x1ffU;
	const uint64_t last = (device_base + bytes - 1U) >> shift & 0x1ffU;

	for (size_t entry = 0; entry < VTD_TRANSLATION_ENTRIES_PER_PAGE; entry++) {
		uint64_t expected = 0;

		if (entry >= first && entry <= last) {
			const uint64_t entry_base = (device_base & ~(entry_span - 1U)) +
				(entry - first) * entry_span;
			const uint64_t range_base = entry_base > device_base ?
				entry_base : device_base;
			const uint64_t entry_end = entry_base + entry_span;
			const uint64_t range_end = entry_end < device_base + bytes ?
				entry_end : device_base + bytes;

			if (!level) {
				expected = (cpu_base + (range_base - device_base)) |
					VTD_TRANSLATION_ENTRY_PRESENT |
					VTD_TRANSLATION_ENTRY_WRITE;
			} else {
				const size_t child = (*next_page)++;

				if (child >= view->used_pages)
					return -1;
				expected = (view->physical_base +
					(uint64_t)child * VTD_TRANSLATION_PAGE_SIZE) |
					VTD_TRANSLATION_ENTRY_PRESENT |
					VTD_TRANSLATION_ENTRY_WRITE;
				if (exact_entry(view, page, entry, expected) ||
				    verify_level(view, child, level - 1U, range_base,
					cpu_base + (range_base - device_base),
					range_end - range_base, next_page))
					return -1;
				continue;
			}
		}
		if (exact_entry(view, page, entry, expected))
			return -1;
	}
	return 0;
}

static bool bus_expected(const struct vtd_translation_requester *requesters,
	size_t requester_count, uint8_t bus)
{
	for (size_t index = 0; index < requester_count; index++)
		if ((requesters[index].bdf >> 8) == bus)
			return true;
	return false;
}

static int verify_context(const struct vtd_translation_view *view, size_t page,
	uint8_t bus, const struct vtd_translation_requester *requesters,
	size_t requester_count)
{
	for (size_t devfn = 0; devfn < 256U; devfn++) {
		const struct vtd_translation_requester *requester = NULL;

		for (size_t index = 0; index < requester_count; index++)
			if (requesters[index].bdf == ((uint16_t)bus << 8 | devfn)) {
				requester = &requesters[index];
				break;
			}
		if (!requester) {
			if (exact_entry(view, page, devfn * 2U, 0) ||
			    exact_entry(view, page, devfn * 2U + 1U, 0))
				return -1;
			continue;
		}
		if (exact_entry(view, page, devfn * 2U + 1U,
			((uint64_t)requester->domain << 8) |
			 VTD_TRANSLATION_CONTEXT_ADDRESS_WIDTH_48))
			return -1;
	}
	return 0;
}

int vtd_translation_verify(const struct vtd_translation_view *view,
	const struct vtd_translation_requester *requesters, size_t requester_count)
{
	size_t next_page = 1U;

	if (!view || !view->read64 || !requesters || !requester_count ||
	    requester_count > VTD_TRANSLATION_VERIFY_MAX_REQUESTERS ||
	    !view->capacity_pages || !view->used_pages ||
	    view->capacity_pages > VTD_TRANSLATION_VERIFY_MAX_PAGES ||
	    view->used_pages > view->capacity_pages ||
	    (view->physical_base & (VTD_TRANSLATION_PAGE_SIZE - 1U)) ||
	    view->capacity_pages > (UINT64_MAX - view->physical_base) /
		VTD_TRANSLATION_PAGE_SIZE ||
	    view->physical_base + view->capacity_pages *
		VTD_TRANSLATION_PAGE_SIZE > (1ULL << 48))
		return -1;

	for (size_t index = 0; index < requester_count; index++) {
		const struct vtd_translation_requester *requester = &requesters[index];
		const uint8_t bus = requester->bdf >> 8;
		const uint8_t devfn = requester->bdf;
		size_t context_page;
		size_t pml4_page;
		bool new_bus = true;

		if (requester_valid(view, requesters, index))
			return -1;
		for (size_t prior = 0; prior < index; prior++)
			if ((requesters[prior].bdf >> 8) == bus) {
				new_bus = false;
				break;
			}
		if (new_bus) {
			context_page = next_page++;
			if (context_page >= view->used_pages ||
			    exact_entry(view, 0, (size_t)bus * 2U,
				(view->physical_base +
				 (uint64_t)context_page * VTD_TRANSLATION_PAGE_SIZE) |
				VTD_TRANSLATION_ENTRY_PRESENT))
				return -1;
		} else {
			uint64_t root;

			if (read_entry(view, 0, (size_t)bus * 2U, &root) ||
			    (root & ~VTD_TRANSLATION_ENTRY_ADDRESS_MASK) !=
				VTD_TRANSLATION_ENTRY_PRESENT ||
			    (root & VTD_TRANSLATION_ENTRY_ADDRESS_MASK) <
				view->physical_base)
				return -1;
			context_page = (root & VTD_TRANSLATION_ENTRY_ADDRESS_MASK) -
				view->physical_base;
			if (context_page & (VTD_TRANSLATION_PAGE_SIZE - 1U))
				return -1;
			context_page >>= VTD_TRANSLATION_PAGE_SHIFT;
			if (context_page >= view->used_pages)
				return -1;
		}
		pml4_page = next_page++;
		if (pml4_page >= view->used_pages ||
		    exact_entry(view, context_page, (size_t)devfn * 2U,
			(view->physical_base +
			 (uint64_t)pml4_page * VTD_TRANSLATION_PAGE_SIZE) |
			VTD_TRANSLATION_ENTRY_PRESENT) ||
		    verify_level(view, pml4_page, 3U, requester->device_base,
			requester->cpu_base,
			(uint64_t)requester->pages << VTD_TRANSLATION_PAGE_SHIFT,
			&next_page))
			return -1;
	}

	if (next_page != view->used_pages)
		return -1;
	for (size_t bus = 0; bus < 256U; bus++) {
		uint64_t root;
		uint64_t root_high;

		if (read_entry(view, 0, bus * 2U, &root) ||
		    read_entry(view, 0, bus * 2U + 1U, &root_high) || root_high)
			return -1;
		if (!bus_expected(requesters, requester_count, bus)) {
			if (root)
				return -1;
			continue;
		}
		if ((root & ~VTD_TRANSLATION_ENTRY_ADDRESS_MASK) !=
			VTD_TRANSLATION_ENTRY_PRESENT ||
		    (root & VTD_TRANSLATION_ENTRY_ADDRESS_MASK) < view->physical_base)
			return -1;
		root = (root & VTD_TRANSLATION_ENTRY_ADDRESS_MASK) -
			view->physical_base;
		if ((root & (VTD_TRANSLATION_PAGE_SIZE - 1U)) ||
		    (root >> VTD_TRANSLATION_PAGE_SHIFT) >= view->used_pages ||
		    verify_context(view, root >> VTD_TRANSLATION_PAGE_SHIFT, bus,
			requesters, requester_count))
			return -1;
	}
	for (size_t page = view->used_pages; page < view->capacity_pages; page++)
		for (size_t entry = 0; entry < VTD_TRANSLATION_ENTRIES_PER_PAGE;
		     entry++)
			if (exact_entry(view, page, entry, 0))
				return -1;
	return 0;
}
