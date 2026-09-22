/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_AMD_CEZANNE_DMA_BINDING_H
#define SOC_AMD_CEZANNE_DMA_BINDING_H

#include <amdblocks/iommu_runtime.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool cezanne_dma_requester_in_aperture(uint16_t bdf, uint32_t function_count);
bool cezanne_dma_iommu_resource_valid(uint64_t base, uint64_t size,
	uint32_t base_low, uint32_t base_high, uint64_t pointer_limit);
bool cezanne_dma_cbmem_layout(uintptr_t allocation, size_t allocation_bytes,
	size_t payload_bytes, size_t alignment, uintptr_t *payload);
bool cezanne_dma_prh_matches(uint64_t generation, bool published,
	size_t published_count, const struct amd_iommu_dma_requester *requesters,
	size_t requester_count,
	bool (*contains)(void *context, uint16_t segment, uint16_t bdf),
	void *context);

#endif
