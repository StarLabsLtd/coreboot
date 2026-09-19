/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdio.h>

#include "q35_dma_policy.h"

static int check(bool condition, const char *message)
{
	if (condition)
		return 0;
	fprintf(stderr, "Q35 DMA policy test: %s\n", message);
	return 1;
}

int main(void)
{
	const uint16_t requester = 0x18;
	uint16_t duplicates[] = { 0x18, 0x18 };
	struct q35_dma_facts facts = {
		.generation = 7,
		.resource_generation = 7,
		.requesters = &requester,
		.requester_count = 1,
		.translation_active = true,
		.tables_resident = true,
		.tables_unchanged = true,
		.bus_master_clear = true,
	};
	int failures = 0;

	failures += check(q35_dma_facts_valid(&facts), "valid facts rejected");
	facts.requester_count = 0;
	failures += check(!q35_dma_facts_valid(&facts), "missing requester accepted");
	facts.requester_count = 1;
	facts.generation = 6;
	failures += check(!q35_dma_facts_valid(&facts), "stale generation accepted");
	facts.generation = 7;
	facts.requesters = duplicates;
	facts.requester_count = 2;
	failures += check(!q35_dma_facts_valid(&facts), "duplicate requester accepted");
	facts.requesters = &requester;
	facts.requester_count = 1;
	facts.translation_active = false;
	failures += check(!q35_dma_facts_valid(&facts), "missing translation accepted");
	facts.translation_active = true;
	facts.tables_unchanged = false;
	failures += check(!q35_dma_facts_valid(&facts), "mutated tables accepted");
	facts.tables_unchanged = true;
	facts.bus_master_clear = false;
	failures += check(!q35_dma_facts_valid(&facts), "active BME accepted");

	if (!failures)
		puts("Q35 DMA policy hostile cases: PASS");
	return failures != 0;
}
