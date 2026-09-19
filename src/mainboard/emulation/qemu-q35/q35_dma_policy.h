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

bool q35_dma_facts_valid(const struct q35_dma_facts *facts);

#endif
