/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef AMD_BLOCK_IOMMU_RUNTIME_H
#define AMD_BLOCK_IOMMU_RUNTIME_H

#include <types.h>

#define AMD_IOMMU_DEVICE_TABLE_SEGMENTS_MAX 8
#define AMD_IOMMU_PAGE_SIZE 4096U
#define AMD_IOMMU_DEVICE_TABLE_ENTRY_SIZE 32U

struct amd_iommu_dma_requester {
	uint16_t device_id;
	uint16_t protection_domain;
	uint64_t arena_cpu_base;
	uint64_t arena_device_base;
	uint32_t arena_pages;
};

struct amd_iommu_device_table_segment {
	uint64_t base;
	uint32_t entries;
};

struct amd_iommu_device_table {
	uint8_t segment_count;
	struct amd_iommu_device_table_segment segment[AMD_IOMMU_DEVICE_TABLE_SEGMENTS_MAX];
};

/*
 * Decode the device-table registers already programmed by an earlier owner.
 * This only describes existing hardware state; it does not establish ownership
 * of the table or of the page tables referenced by its entries.
 */
enum cb_err amd_iommu_decode_device_table(uint64_t control,
					  uint64_t extended_features,
					  const uint64_t *segment_register,
					  struct amd_iommu_device_table *table);

enum cb_err
amd_iommu_device_table_entry_address(const struct amd_iommu_device_table *table,
				     uint16_t device_id, uint64_t *address);

size_t amd_iommu_device_table_bytes(uint16_t maximum_device_id);

enum cb_err amd_iommu_build_dma_state(void *device_table, size_t device_table_bytes,
				      void *page_tables, size_t page_table_bytes,
				      const struct amd_iommu_dma_requester *requesters,
				      size_t requester_count);

bool amd_iommu_dma_state_matches(const void *device_table, size_t device_table_bytes,
				 const void *page_tables, size_t page_table_bytes,
				 const struct amd_iommu_dma_requester *requesters,
				 size_t requester_count);

#endif /* AMD_BLOCK_IOMMU_RUNTIME_H */
