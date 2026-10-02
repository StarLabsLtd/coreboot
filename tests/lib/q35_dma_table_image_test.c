/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/bsd/helpers.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q35_dma_table_image.h"

#define Q35_DMA_PAGE_SIZE 4096U
#define Q35_DMA_REQUESTERS 2U
#define VTD_PAGE_READ_WRITE 3ULL
#define VTD_CONTEXT_PRESENT 1ULL
#define VTD_CONTEXT_AW_48BIT 2ULL

static uint64_t tables[10U * Q35_DMA_PAGE_SIZE / sizeof(uint64_t)]
	__aligned(Q35_DMA_PAGE_SIZE);
static uint8_t arena_storage[163U * Q35_DMA_PAGE_SIZE]
	__aligned(Q35_DMA_PAGE_SIZE);
static uint16_t requester_bdfs[] = {0x18U, 0x20U};
static uint16_t requester_domains[] = {1U, 2U};
static uint8_t *requester_arenas[Q35_DMA_REQUESTERS];
static const uint32_t requester_arena_pages[] = {32U, 128U};
static const uint64_t requester_iovas[] = {0x80000000ULL, 0x90000000ULL};

static void check(bool condition, const char *expression)
{
	if (!condition) {
		fprintf(stderr, "DMA_TABLE_ASSERT: %s\n", expression);
		abort();
	}
}

#define CHECK(condition) check(!!(condition), #condition)

static uint64_t *table_page(size_t page)
{
	return &tables[page * Q35_DMA_PAGE_SIZE / sizeof(*tables)];
}

/* Actual existing ramstage hierarchy/population function bodies. */
#include "population.h"

int main(void)
{
	const uintptr_t arena = (uintptr_t)arena_storage + 16U;
	const size_t arena_size = 161U * Q35_DMA_PAGE_SIZE;
	const uintptr_t aligned = (arena + Q35_DMA_PAGE_SIZE - 1U) &
		~(uintptr_t)(Q35_DMA_PAGE_SIZE - 1U);
	struct q35_capsule_dma_geometry targets = {
		.communication_base = 0x18000000U,
		.communication_reserved_size = 4096U,
		.communication_size = 168U,
		.staging_base = 0x18100000U,
		.staging_size = 9437184U,
	};
	const struct q35_capsule_dma_geometry original = targets;

	requester_arenas[0] = (void *)aligned;
	requester_arenas[1] = (void *)(aligned + 32U * Q35_DMA_PAGE_SIZE);
	for (size_t requester = 0; requester < Q35_DMA_REQUESTERS; requester++)
		populate_requester_hierarchy(requester);
	CHECK(q35_dma_table_image_valid(tables, sizeof(tables), arena, arena_size, &targets));
	CHECK(!q35_dma_table_image_valid(NULL, sizeof(tables), arena, arena_size, &targets));
	CHECK(!q35_dma_table_image_valid(tables, sizeof(tables), arena, arena_size, NULL));
	CHECK(!q35_dma_table_image_valid(tables, sizeof(tables) - 1U, arena,
		arena_size, &targets));
	CHECK(!q35_dma_table_image_valid((const void *)((uintptr_t)tables + 8U),
		sizeof(tables), arena, arena_size, &targets));
	CHECK(!q35_dma_table_image_valid(tables, sizeof(tables), arena,
		arena_size + Q35_DMA_PAGE_SIZE, &targets));
	CHECK(!q35_dma_table_image_valid(tables, sizeof(tables), UINT32_MAX - 1U,
		arena_size, &targets));
	CHECK(!q35_dma_table_image_valid(tables, sizeof(tables), 0, arena_size, &targets));
	CHECK(!q35_dma_table_image_valid(tables, sizeof(tables), (uintptr_t)tables,
		arena_size, &targets));
	for (size_t index = 0; index < ARRAY_SIZE(tables); index++) {
		tables[index] ^= 1U;
		CHECK(!q35_dma_table_image_valid(tables, sizeof(tables), arena,
			arena_size, &targets));
		tables[index] ^= 1U;
	}
	/* Same IOVA permissions, but malicious destination is the protected target. */
	for (size_t requester = 0; requester < Q35_DMA_REQUESTERS; requester++) {
		uint64_t *leaf = table_page(hierarchy_page(requester, 3));
		const uint64_t saved = leaf[0];

		leaf[0] = targets.communication_base | VTD_PAGE_READ_WRITE;
		CHECK(!q35_dma_table_image_valid(tables, sizeof(tables), arena,
			arena_size, &targets));
		leaf[0] = (uintptr_t)tables | VTD_PAGE_READ_WRITE;
		CHECK(!q35_dma_table_image_valid(tables, sizeof(tables), arena,
			arena_size, &targets));
		leaf[0] = saved;
	}
	targets.communication_base = (uintptr_t)tables;
	CHECK(!q35_dma_table_image_valid(tables, sizeof(tables), arena, arena_size, &targets));
	targets = original;
	targets.communication_base = aligned;
	CHECK(!q35_dma_table_image_valid(tables, sizeof(tables), arena, arena_size, &targets));
	targets = original;
	targets.staging_base = aligned;
	CHECK(!q35_dma_table_image_valid(tables, sizeof(tables), arena, arena_size, &targets));
	targets = original;
	targets.communication_base = UINT64_MAX - 4095U;
	CHECK(!q35_dma_table_image_valid(tables, sizeof(tables), arena, arena_size, &targets));
	targets = original;
	CHECK(q35_dma_table_image_valid(tables, sizeof(tables), arena, arena_size, &targets));
	puts("HOST actual ramstage table image/physical destinations: PASS (not DMA authority)");
	return 0;
}
