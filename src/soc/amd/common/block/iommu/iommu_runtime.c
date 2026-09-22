/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/iommu_runtime.h>
#include <string.h>

#define IOMMU_CONTROL_ENABLE		BIT(0)
#define IOMMU_CONTROL_SEGMENT_SHIFT	34
#define IOMMU_CONTROL_SEGMENT_MASK	(7ULL << IOMMU_CONTROL_SEGMENT_SHIFT)

#define IOMMU_EFR_SEGMENT_SUPPORT_SHIFT	38
#define IOMMU_EFR_SEGMENT_SUPPORT_MASK	(3ULL << IOMMU_EFR_SEGMENT_SUPPORT_SHIFT)

#define DEVICE_TABLE_BASE_MASK		0x000ffffffffff000ULL
#define DEVICE_TABLE_SIZE_MASK		0x1ffULL
#define DEVICE_TABLE_REGISTER_MASK	(DEVICE_TABLE_BASE_MASK | DEVICE_TABLE_SIZE_MASK)
#define DEVICE_TABLE_ENTRY_SIZE		32
#define DEVICE_ID_COUNT			BIT(16)

static bool ranges_overlap(uint64_t first_base, uint64_t first_size,
			   uint64_t second_base, uint64_t second_size)
{
	return first_base < second_base + second_size &&
		second_base < first_base + first_size;
}

static bool device_table_valid(const struct amd_iommu_device_table *table)
{
	uint32_t entries_per_segment;
	uint8_t i;

	if (!table || !table->segment_count ||
	    table->segment_count > AMD_IOMMU_DEVICE_TABLE_SEGMENTS_MAX ||
	    (table->segment_count & (table->segment_count - 1)))
		return false;

	entries_per_segment = DEVICE_ID_COUNT / table->segment_count;
	for (i = 0; i < table->segment_count; i++) {
		uint8_t previous;
		const uint64_t size = (uint64_t)table->segment[i].entries *
			DEVICE_TABLE_ENTRY_SIZE;

		if (!table->segment[i].base ||
		    (table->segment[i].base & ~DEVICE_TABLE_BASE_MASK) ||
		    !table->segment[i].entries ||
		    table->segment[i].entries > entries_per_segment)
			return false;

		for (previous = 0; previous < i; previous++) {
			const uint64_t previous_size =
				(uint64_t)table->segment[previous].entries *
				DEVICE_TABLE_ENTRY_SIZE;

			if (ranges_overlap(table->segment[i].base, size,
					   table->segment[previous].base, previous_size))
				return false;
		}
	}

	return true;
}

enum cb_err amd_iommu_decode_device_table(uint64_t control,
					  uint64_t extended_features,
					  const uint64_t *segment_register,
					  struct amd_iommu_device_table *table)
{
	uint8_t enabled_segments;
	uint8_t supported_segments;
	uint8_t segment_count;
	uint8_t i;

	if (!table)
		return CB_ERR_ARG;
	memset(table, 0, sizeof(*table));
	if (!segment_register || !(control & IOMMU_CONTROL_ENABLE))
		return CB_ERR_ARG;

	enabled_segments = (control & IOMMU_CONTROL_SEGMENT_MASK) >>
		IOMMU_CONTROL_SEGMENT_SHIFT;
	supported_segments = (extended_features & IOMMU_EFR_SEGMENT_SUPPORT_MASK) >>
		IOMMU_EFR_SEGMENT_SUPPORT_SHIFT;
	if (enabled_segments > supported_segments || enabled_segments > 3)
		return CB_ERR_ARG;
	segment_count = 1U << enabled_segments;
	table->segment_count = segment_count;

	for (i = 0; i < segment_count; i++) {
		const uint64_t value = segment_register[i];
		const uint64_t base = value & DEVICE_TABLE_BASE_MASK;
		const uint64_t pages = (value & DEVICE_TABLE_SIZE_MASK) + 1;
		const uint64_t size = pages << 12;

		if (value & ~DEVICE_TABLE_REGISTER_MASK)
			goto invalid;

		table->segment[i].base = base;
		table->segment[i].entries = size / DEVICE_TABLE_ENTRY_SIZE;
	}
	if (!device_table_valid(table))
		goto invalid;

	return CB_SUCCESS;

invalid:
	memset(table, 0, sizeof(*table));
	return CB_ERR_ARG;
}

enum cb_err
amd_iommu_device_table_entry_address(const struct amd_iommu_device_table *table,
				     uint16_t device_id, uint64_t *address)
{
	uint32_t entries_per_segment;
	uint32_t segment;
	uint32_t index;

	if (!address || !device_table_valid(table))
		return CB_ERR_ARG;

	entries_per_segment = DEVICE_ID_COUNT / table->segment_count;
	segment = device_id / entries_per_segment;
	index = device_id % entries_per_segment;
	if (index >= table->segment[segment].entries ||
	    !table->segment[segment].base)
		return CB_ERR_ARG;

	*address = table->segment[segment].base +
		(uint64_t)index * DEVICE_TABLE_ENTRY_SIZE;
	return CB_SUCCESS;
}
