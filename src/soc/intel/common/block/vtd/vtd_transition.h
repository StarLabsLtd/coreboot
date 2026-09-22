/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_COMMON_BLOCK_VTD_TRANSITION_H
#define SOC_INTEL_COMMON_BLOCK_VTD_TRANSITION_H

#include <stdbool.h>
#include <stdint.h>

struct vtd_transition_io {
	void *context;
	uint32_t (*read32)(void *context, uint32_t offset);
	void (*write32)(void *context, uint32_t offset, uint32_t value);
	void (*commit_tables)(void *context);
};

struct vtd_transition_facts {
	uint32_t version;
	uint64_t capability;
	uint64_t extended_capability;
	uint32_t status;
	uint64_t root;
	uint32_t protected_memory_enable;
	uint32_t iotlb_offset;
	bool coherent;
};

int vtd_transition_probe(const struct vtd_transition_io *io,
	struct vtd_transition_facts *facts);
int vtd_transition_from_pmr(const struct vtd_transition_io *io,
	uint64_t root_physical);

#endif
