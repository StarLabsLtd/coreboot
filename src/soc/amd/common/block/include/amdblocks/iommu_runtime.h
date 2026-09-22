/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef AMD_BLOCK_IOMMU_RUNTIME_H
#define AMD_BLOCK_IOMMU_RUNTIME_H

#include <types.h>

#define AMD_IOMMU_DEVICE_TABLE_SEGMENTS_MAX 8

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

#endif /* AMD_BLOCK_IOMMU_RUNTIME_H */
