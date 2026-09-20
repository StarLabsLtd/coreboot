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
	const struct q35_dma_pmr_state pmr = {
		.enable = 0,
		.low_base = 0x1000,
		.low_limit = 0x1fff,
		.high_base = 0x100000000ULL,
		.high_limit = 0x1ffffffffULL,
	};
	const struct q35_capsule_dma_geometry geometry = {
		.communication_base = 0x100000,
		.communication_reserved_size = 0x1000,
		.communication_size = 168,
		.staging_base = 0x101000,
		.staging_size = 0x900000,
	};
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
	failures += check(q35_dma_pmr_state_matches(&pmr, &pmr),
		"identical PMR state rejected");
	{
		struct q35_dma_pmr_state changed = pmr;

		changed.high_base ^= 1ULL << 32;
		failures += check(!q35_dma_pmr_state_matches(&pmr, &changed),
			"high protected base mutation accepted");
		changed = pmr;
		changed.high_limit ^= 1ULL << 32;
		failures += check(!q35_dma_pmr_state_matches(&pmr, &changed),
			"high protected limit mutation accepted");
	}
	failures += check(q35_capsule_dma_range_valid(&geometry,
		geometry.communication_base, geometry.communication_size),
		"exact communication range rejected");
	failures += check(q35_capsule_dma_range_valid(&geometry,
		geometry.staging_base, geometry.staging_size),
		"exact staging range rejected");
	failures += check(!q35_capsule_dma_range_valid(&geometry,
		geometry.communication_base, geometry.communication_reserved_size),
		"communication reservation accepted as transport geometry");
	failures += check(!q35_capsule_dma_range_valid(&geometry,
		geometry.staging_base + 1, geometry.staging_size - 1),
		"staging subrange accepted");
	{
		struct q35_capsule_dma_geometry invalid = geometry;

		invalid.staging_base = geometry.communication_base;
		failures += check(!q35_capsule_dma_range_valid(&invalid,
			invalid.staging_base, invalid.staging_size),
			"overlapping reservations accepted");
	}

	if (!failures)
		puts("Q35 DMA policy hostile cases: PASS");
	return failures != 0;
}
