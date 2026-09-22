/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef AMD_BLOCK_IOMMU_DMA_H
#define AMD_BLOCK_IOMMU_DMA_H

#include <amdblocks/iommu_runtime.h>

struct amd_iommu_dma_io {
	void *context;
	uint64_t (*read64)(void *context, uint32_t offset);
	void (*write64)(void *context, uint32_t offset, uint64_t value);
	void (*commit_tables)(void *context, const void *base, size_t bytes);
	/* The caller owns PCI_COMMAND and keeps every requester quiesced. */
	bool (*quiescence_held)(void *context);
	/* Must not return: translation may already be disabled. */
	void (*fail_closed)(void *context);
};

enum cb_err amd_iommu_dma_replace(const struct amd_iommu_dma_io *io,
				  void *device_table, size_t device_table_bytes,
				  void *page_tables, size_t page_table_bytes,
				  const struct amd_iommu_dma_requester *requesters,
				  size_t requester_count);

bool amd_iommu_dma_active(const struct amd_iommu_dma_io *io,
			  const void *device_table, size_t device_table_bytes,
			  const void *page_tables, size_t page_table_bytes,
			  const struct amd_iommu_dma_requester *requesters,
			  size_t requester_count);

#endif /* AMD_BLOCK_IOMMU_DMA_H */
