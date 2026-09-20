/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_EMULATION_QEMU_Q35_DMA_POLICY_H
#define MAINBOARD_EMULATION_QEMU_Q35_DMA_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct q35_dma_facts {
	uint64_t generation;
	uint64_t resource_generation;
	const uint16_t *requesters;
	size_t requester_count;
	bool translation_active;
	bool tables_resident;
	bool tables_unchanged;
	bool bus_master_clear;
};

struct q35_capsule_dma_geometry {
	uint64_t communication_base;
	uint64_t communication_reserved_size;
	uint64_t communication_size;
	uint64_t staging_base;
	uint64_t staging_size;
};

struct q35_dma_pmr_state {
	uint32_t enable;
	uint32_t low_base;
	uint32_t low_limit;
	uint64_t high_base;
	uint64_t high_limit;
};

bool q35_dma_facts_valid(const struct q35_dma_facts *facts);
bool q35_capsule_dma_range_valid(
	const struct q35_capsule_dma_geometry *geometry,
	uint64_t base, uint64_t size);
bool q35_dma_pmr_state_matches(const struct q35_dma_pmr_state *expected,
	const struct q35_dma_pmr_state *observed);

#endif
