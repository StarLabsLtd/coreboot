/* SPDX-License-Identifier: GPL-2.0-only */

#include "q35_dma_policy.h"

#define Q35_DMA_MAX_TEST_REQUESTERS 2U

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
