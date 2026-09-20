/* SPDX-License-Identifier: GPL-2.0-only */

#include "q35_dma_policy.h"

#define Q35_DMA_MAX_TEST_REQUESTERS 2U
#define Q35_DMA_PAGE_SIZE 4096U

bool q35_dma_facts_valid(const struct q35_dma_facts *facts)
{
	if (!facts || !facts->generation ||
	    facts->generation != facts->resource_generation ||
	    !facts->requesters || !facts->requester_count ||
	    facts->requester_count > Q35_DMA_MAX_TEST_REQUESTERS ||
	    !facts->translation_active || !facts->tables_resident ||
	    !facts->tables_unchanged || !facts->bus_master_clear)
		return false;

	for (size_t first = 0; first < facts->requester_count; first++)
		for (size_t second = first + 1; second < facts->requester_count; second++)
			if (facts->requesters[first] == facts->requesters[second])
				return false;
	return true;
}

bool q35_capsule_dma_range_valid(
	const struct q35_capsule_dma_geometry *geometry,
	uint64_t base, uint64_t size)
{
	if (!geometry || !geometry->communication_base ||
	    geometry->communication_reserved_size < geometry->communication_size ||
	    !geometry->communication_size || !geometry->staging_base ||
	    !geometry->staging_size ||
	    geometry->communication_base % Q35_DMA_PAGE_SIZE ||
	    geometry->communication_reserved_size % Q35_DMA_PAGE_SIZE ||
	    geometry->staging_base % Q35_DMA_PAGE_SIZE ||
	    geometry->staging_size % Q35_DMA_PAGE_SIZE)
		return false;
	if (geometry->communication_base > UINT64_MAX -
	    geometry->communication_reserved_size ||
	    geometry->staging_base > UINT64_MAX - geometry->staging_size)
		return false;
	if (geometry->communication_base < geometry->staging_base +
	    geometry->staging_size && geometry->staging_base <
	    geometry->communication_base + geometry->communication_reserved_size)
		return false;
	return (base == geometry->communication_base &&
			size == geometry->communication_size) ||
		(base == geometry->staging_base && size == geometry->staging_size);
}

bool q35_dma_pmr_state_matches(const struct q35_dma_pmr_state *expected,
	const struct q35_dma_pmr_state *observed)
{
	return expected && observed &&
		expected->enable == observed->enable &&
		expected->low_base == observed->low_base &&
		expected->low_limit == observed->low_limit &&
		expected->high_base == observed->high_base &&
		expected->high_limit == observed->high_limit;
}
