/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/bsd/helpers.h>

#include "q35_dma_table_image.h"

#define PAGE_SIZE 4096U
#define TABLE_PAGES 10U
#define ARENA_PAGES 160U

static bool ranges_overlap(uint64_t first, uint64_t first_size,
	uint64_t second, uint64_t second_size)
{
	return first < second + second_size && second < first + first_size;
}

bool q35_dma_table_image_valid(const void *table, size_t table_size,
	uintptr_t arena, size_t arena_size,
	const struct q35_capsule_dma_geometry *targets)
{
	static const uint16_t requesters[] = {0x18U, 0x20U};
	static const uint64_t iovas[] = {0x80000000ULL, 0x90000000ULL};
	static const size_t pages[] = {32U, 128U};
	const uintptr_t base = (uintptr_t)table;
	const volatile uint64_t *entries = table;
	uintptr_t aligned_arena;

	if (!table || table_size != TABLE_PAGES * PAGE_SIZE ||
	    !arena || arena_size != (ARENA_PAGES + 1U) * PAGE_SIZE ||
	    (base & (PAGE_SIZE - 1U)) || base > UINT32_MAX ||
	    table_size - 1U > UINT32_MAX - base || arena > UINT32_MAX ||
	    arena_size - 1U > UINT32_MAX - arena || !targets ||
	    !q35_capsule_dma_range_valid(targets, targets->communication_base,
		targets->communication_size) ||
	    targets->communication_base > UINT32_MAX ||
	    targets->communication_reserved_size - 1U >
		UINT32_MAX - targets->communication_base ||
	    targets->staging_base > UINT32_MAX ||
	    targets->staging_size - 1U > UINT32_MAX - targets->staging_base ||
	    ranges_overlap(base, table_size, arena, arena_size) ||
	    ranges_overlap(base, table_size, targets->communication_base,
		targets->communication_reserved_size) ||
	    ranges_overlap(base, table_size, targets->staging_base, targets->staging_size) ||
	    ranges_overlap(arena, arena_size, targets->communication_base,
		targets->communication_reserved_size) ||
	    ranges_overlap(arena, arena_size, targets->staging_base, targets->staging_size))
		return false;
	aligned_arena = (arena + PAGE_SIZE - 1U) & ~(uintptr_t)(PAGE_SIZE - 1U);
	if (aligned_arena < arena || aligned_arena - arena > arena_size ||
	    ARENA_PAGES * PAGE_SIZE > arena_size - (aligned_arena - arena))
		return false;

	for (size_t page = 0; page < TABLE_PAGES; page++) {
		for (size_t slot = 0; slot < PAGE_SIZE / sizeof(*entries); slot++) {
			uint64_t expected = 0;

			if (page == 0 && slot == 0)
				expected = (base + PAGE_SIZE) | 1U;
			for (size_t requester = 0; requester < ARRAY_SIZE(requesters); requester++) {
				const size_t hierarchy = 2U + requester * 4U;
				const uint64_t iova = iovas[requester];
				const uintptr_t physical = aligned_arena +
					(requester ? pages[0] * PAGE_SIZE : 0U);

				if (page == 1 && slot == requesters[requester] * 2U)
					expected = (base + hierarchy * PAGE_SIZE) | 1U;
				else if (page == 1 && slot == requesters[requester] * 2U + 1U)
					expected = 2U | ((uint64_t)(requester + 1U) << 8);
				else if (page == hierarchy && slot == ((iova >> 39) & 0x1ffU))
					expected = (base + (hierarchy + 1U) * PAGE_SIZE) | 3U;
				else if (page == hierarchy + 1U && slot == ((iova >> 30) & 0x1ffU))
					expected = (base + (hierarchy + 2U) * PAGE_SIZE) | 3U;
				else if (page == hierarchy + 2U && slot == ((iova >> 21) & 0x1ffU))
					expected = (base + (hierarchy + 3U) * PAGE_SIZE) | 3U;
				else if (page == hierarchy + 3U && slot >= ((iova >> 12) & 0x1ffU) &&
					 slot - ((iova >> 12) & 0x1ffU) < pages[requester])
					expected = (physical +
						(slot - ((iova >> 12) & 0x1ffU)) * PAGE_SIZE) | 3U;
			}
			if (entries[page * (PAGE_SIZE / sizeof(*entries)) + slot] != expected)
				return false;
		}
	}
	return true;
}
