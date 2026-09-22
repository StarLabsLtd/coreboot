/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/iommu_dma.h>

#define IOMMU_DEVICE_TABLE_OFFSET 0x0000U
#define IOMMU_CONTROL_OFFSET 0x0018U
#define IOMMU_EXTENDED_FEATURES_OFFSET 0x0030U

#define IOMMU_CONTROL_ENABLE BIT(0)
#define IOMMU_CONTROL_COHERENT BIT(10)
#define IOMMU_CONTROL_ISOCHRONOUS BIT(11)
#define IOMMU_CONTROL_SEGMENT_MASK (7ULL << 34)
#define IOMMU_DMA_CONTROL \
	(IOMMU_CONTROL_ENABLE | IOMMU_CONTROL_COHERENT | IOMMU_CONTROL_ISOCHRONOUS)

#define IOMMU_DEVICE_TABLE_BASE_MASK 0x000ffffffffff000ULL
#define IOMMU_DEVICE_TABLE_SIZE_MASK 0x1ffULL

static bool arguments_valid(const struct amd_iommu_dma_io *io,
	const void *device_table, size_t device_table_bytes, const void *page_tables,
	size_t page_table_bytes, const struct amd_iommu_dma_requester *requesters,
	size_t requester_count)
{
	const size_t pages = device_table_bytes / AMD_IOMMU_PAGE_SIZE;

	return io && io->read64 && io->write64 && io->commit_tables &&
		io->quiescence_held && io->fail_closed &&
		device_table && page_tables && requesters && requester_count &&
		!((uintptr_t)device_table & (AMD_IOMMU_PAGE_SIZE - 1U)) &&
		device_table_bytes >= AMD_IOMMU_PAGE_SIZE &&
		!(device_table_bytes % AMD_IOMMU_PAGE_SIZE) &&
		pages <= IOMMU_DEVICE_TABLE_SIZE_MASK + 1U &&
		!((uintptr_t)device_table & ~IOMMU_DEVICE_TABLE_BASE_MASK) &&
		amd_iommu_dma_state_matches(device_table, device_table_bytes,
			page_tables, page_table_bytes, requesters, requester_count);
}

static void transition_failure(const struct amd_iommu_dma_io *io)
{
	io->fail_closed(io->context);
	__builtin_unreachable();
}

static void require_quiescence(const struct amd_iommu_dma_io *io)
{
	if (!io->quiescence_held(io->context))
		transition_failure(io);
}

static uint64_t device_table_register(const void *device_table,
	size_t device_table_bytes)
{
	return (uintptr_t)device_table |
		(device_table_bytes / AMD_IOMMU_PAGE_SIZE - 1U);
}

bool amd_iommu_dma_active(const struct amd_iommu_dma_io *io,
			  const void *device_table, size_t device_table_bytes,
			  const void *page_tables, size_t page_table_bytes,
			  const struct amd_iommu_dma_requester *requesters,
			  size_t requester_count)
{
	struct amd_iommu_device_table decoded;
	uint64_t segment_register;
	uint64_t entry_address;

	if (!arguments_valid(io, device_table, device_table_bytes, page_tables,
		page_table_bytes, requesters, requester_count) ||
	    !io->quiescence_held(io->context) ||
	    io->read64(io->context, IOMMU_CONTROL_OFFSET) != IOMMU_DMA_CONTROL)
		return false;
	segment_register = io->read64(io->context, IOMMU_DEVICE_TABLE_OFFSET);
	if (segment_register != device_table_register(device_table,
		device_table_bytes) || amd_iommu_decode_device_table(IOMMU_DMA_CONTROL,
		io->read64(io->context, IOMMU_EXTENDED_FEATURES_OFFSET),
		&segment_register, &decoded) != CB_SUCCESS || decoded.segment_count != 1)
		return false;
	for (size_t index = 0; index < requester_count; index++)
		if (amd_iommu_device_table_entry_address(&decoded,
			requesters[index].device_id, &entry_address) != CB_SUCCESS ||
		    entry_address != (uintptr_t)device_table +
			(size_t)requesters[index].device_id *
			AMD_IOMMU_DEVICE_TABLE_ENTRY_SIZE)
			return false;
	return amd_iommu_dma_state_matches(device_table, device_table_bytes,
		page_tables, page_table_bytes, requesters, requester_count);
}

enum cb_err amd_iommu_dma_replace(const struct amd_iommu_dma_io *io,
				  void *device_table, size_t device_table_bytes,
				  void *page_tables, size_t page_table_bytes,
				  const struct amd_iommu_dma_requester *requesters,
				  size_t requester_count)
{
	struct amd_iommu_device_table observed;
	uint64_t old_segment_register;
	uint64_t old_control;
	uint64_t features;

	if (!arguments_valid(io, device_table, device_table_bytes, page_tables,
		page_table_bytes, requesters, requester_count))
		return CB_ERR_ARG;
	if (!io->quiescence_held(io->context))
		return CB_ERR;
	old_control = io->read64(io->context, IOMMU_CONTROL_OFFSET);
	features = io->read64(io->context, IOMMU_EXTENDED_FEATURES_OFFSET);
	old_segment_register = io->read64(io->context, IOMMU_DEVICE_TABLE_OFFSET);
	/* Observe FSP state only to require a live, internally consistent source. */
	if ((old_control & IOMMU_CONTROL_SEGMENT_MASK) ||
	    amd_iommu_decode_device_table(old_control, features,
		&old_segment_register, &observed) != CB_SUCCESS ||
	    observed.segment_count != 1)
		return CB_ERR;

	/* Stop translation and every FSP-owned auxiliary queue before replacement. */
	io->write64(io->context, IOMMU_CONTROL_OFFSET, 0);
	require_quiescence(io);
	if (io->read64(io->context, IOMMU_CONTROL_OFFSET) != 0)
		transition_failure(io);
	io->commit_tables(io->context, device_table, device_table_bytes);
	require_quiescence(io);
	io->commit_tables(io->context, page_tables, page_table_bytes);
	require_quiescence(io);
	io->write64(io->context, IOMMU_DEVICE_TABLE_OFFSET,
		device_table_register(device_table, device_table_bytes));
	require_quiescence(io);
	if (io->read64(io->context, IOMMU_DEVICE_TABLE_OFFSET) !=
	    device_table_register(device_table, device_table_bytes))
		transition_failure(io);
	/* Drop every FSP-owned auxiliary queue and enable only translation. */
	io->write64(io->context, IOMMU_CONTROL_OFFSET, IOMMU_DMA_CONTROL);
	require_quiescence(io);
	if (!amd_iommu_dma_active(io, device_table, device_table_bytes, page_tables,
		page_table_bytes, requesters, requester_count))
		transition_failure(io);
	return CB_SUCCESS;
}
