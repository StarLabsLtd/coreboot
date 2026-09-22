/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/iommu.h>
#include <commonlib/helpers.h>
#include <soc/dma_binding.h>
#include <soc/dma_guard.h>

#define IOMMU_BASE_LOW_MASK 0xffffc000U
#define IOMMU_REGISTER_BYTES (512U * KiB)

bool cezanne_dma_requester_in_aperture(uint16_t bdf, uint32_t function_count)
{
	return function_count && function_count <= CEZANNE_DMA_PCI_FUNCTIONS &&
		bdf < function_count;
}

bool cezanne_dma_iommu_resource_valid(uint64_t base, uint64_t size,
	uint32_t base_low, uint32_t base_high, uint64_t pointer_limit)
{
	const uint64_t programmed = ((uint64_t)base_high << 32) |
		(base_low & IOMMU_BASE_LOW_MASK);

	return base && size == IOMMU_REGISTER_BYTES &&
		!(base & (IOMMU_REGISTER_BYTES - 1U)) &&
		(base_low & IOMMU_ENABLE) &&
		!(base_low & ~(IOMMU_BASE_LOW_MASK | IOMMU_ENABLE)) &&
		programmed == base && base <= pointer_limit &&
		size - 1U <= pointer_limit - base;
}

bool cezanne_dma_cbmem_layout(uintptr_t allocation, size_t allocation_bytes,
	size_t payload_bytes, size_t alignment, uintptr_t *payload)
{
	uintptr_t aligned;

	if (!allocation || !payload || !payload_bytes || !alignment ||
	    (alignment & (alignment - 1U)) || payload_bytes > SIZE_MAX - (alignment - 1U) ||
	    allocation_bytes != payload_bytes + alignment - 1U ||
	    allocation > UINTPTR_MAX - allocation_bytes)
		return false;
	aligned = ALIGN_UP(allocation, alignment);
	if (aligned < allocation || aligned > UINTPTR_MAX - payload_bytes ||
	    aligned + payload_bytes > allocation + allocation_bytes)
		return false;
	*payload = aligned;
	return true;
}

bool cezanne_dma_prh_matches(uint64_t generation, bool published,
	size_t published_count, const struct amd_iommu_dma_requester *requesters,
	size_t requester_count,
	bool (*contains)(void *context, uint16_t segment, uint16_t bdf),
	void *context)
{
	if (!generation || !published || !requesters || !requester_count ||
	    published_count != requester_count || !contains)
		return false;
	for (size_t index = 0; index < requester_count; index++) {
		if (!contains(context, 0, requesters[index].device_id))
			return false;
	}
	return true;
}
