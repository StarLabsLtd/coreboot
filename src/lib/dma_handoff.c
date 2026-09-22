/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <boot/dma_handoff.h>
#include <commonlib/helpers.h>
#include <crc_byte.h>
#include <stddef.h>
#include <string.h>

static size_t handoff_size(size_t requester_count)
{
	return sizeof(struct dma_handoff_header) +
		requester_count * sizeof(struct dma_handoff_requester);
}

static const struct dma_handoff_requester *handoff_requesters(
	const struct dma_handoff_header *header)
{
	return (const void *)((const uint8_t *)header + header->header_size);
}

static uint32_t handoff_crc32(const struct dma_handoff_header *header)
{
	const uint8_t *bytes = (const void *)header;
	const size_t crc_offset = offsetof(struct dma_handoff_header, crc32);
	uint32_t crc = 0;

	for (size_t index = 0; index < header->size; index++)
		crc = crc32_byte(crc, index >= crc_offset &&
			index < crc_offset + sizeof(header->crc32) ? 0 : bytes[index]);
	return crc;
}

static bool range_valid(uint64_t base, uint32_t pages, uint16_t granule_shift)
{
	const uint64_t granule = 1ULL << granule_shift;
	uint64_t bytes;

	if (!base || (base & (granule - 1U)) || !pages ||
	    pages > (UINT64_MAX >> granule_shift))
		return false;
	bytes = (uint64_t)pages << granule_shift;
	return base <= UINT64_MAX - bytes;
}

static bool ranges_overlap(uint64_t first_base, uint32_t first_pages,
	uint64_t second_base, uint32_t second_pages, uint16_t granule_shift)
{
	const uint64_t first_end = first_base +
		((uint64_t)first_pages << granule_shift);
	const uint64_t second_end = second_base +
		((uint64_t)second_pages << granule_shift);

	return first_base < second_end && second_base < first_end;
}

static bool range_overlaps_blob(uint64_t base, uint32_t pages,
	uint16_t granule_shift, const void *blob, size_t bytes)
{
	const uint64_t end = base + ((uint64_t)pages << granule_shift);
	const uint64_t blob_base = (uintptr_t)blob;
	uint64_t blob_end;

	if (blob_base > UINT64_MAX - bytes)
		return true;
	blob_end = blob_base + bytes;
	return base < blob_end && blob_base < end;
}

enum cb_err dma_handoff_validate(const void *buffer, size_t bytes)
{
	const struct dma_handoff_header *header = buffer;
	const struct dma_handoff_requester *requesters;
	size_t expected;

	if (!buffer || bytes < sizeof(*header))
		return CB_ERR;
	if (header->revision != DMA_HANDOFF_REVISION ||
	    header->header_size != sizeof(*header) ||
	    header->requester_size != sizeof(*requesters) ||
	    header->granule_shift != DMA_HANDOFF_GRANULE_SHIFT ||
	    header->flags != DMA_HANDOFF_REQUIRED_FLAGS || !header->generation ||
	    header->reserved[0] || header->reserved[1] ||
	    !header->requester_count ||
	    header->requester_count > DMA_HANDOFF_MAX_REQUESTERS)
		return CB_ERR;
	expected = handoff_size(header->requester_count);
	if (expected > UINT32_MAX || header->size != expected || bytes != expected)
		return CB_ERR;
	if (handoff_crc32(header) != header->crc32)
		return CB_ERR;
	requesters = handoff_requesters(header);
	for (size_t slot = 0; slot < header->requester_count; slot++) {
		const struct dma_handoff_requester *requester = &requesters[slot];

		if (!requester->protection_domain ||
		    requester->flags != DMA_HANDOFF_REQUESTER_FLAGS ||
		    requester->arena_flags != DMA_HANDOFF_ARENA_FLAGS ||
		    !range_valid(requester->arena_cpu_base, requester->arena_pages,
			header->granule_shift) ||
		    !range_valid(requester->arena_device_base, requester->arena_pages,
			header->granule_shift) ||
		    range_overlaps_blob(requester->arena_cpu_base,
			requester->arena_pages, header->granule_shift, buffer, bytes))
			return CB_ERR;
		for (size_t other = 0; other < slot; other++) {
			if ((requesters[other].segment == requester->segment &&
			     requesters[other].bdf == requester->bdf) ||
			    requesters[other].protection_domain ==
				requester->protection_domain)
				return CB_ERR;
			if (ranges_overlap(requesters[other].arena_cpu_base,
				requesters[other].arena_pages, requester->arena_cpu_base,
				requester->arena_pages, header->granule_shift))
				return CB_ERR;
		}
	}
	return CB_SUCCESS;
}

enum cb_err dma_handoff_build(void *buffer, size_t capacity,
	uint64_t generation,
	const struct dma_handoff_requester *requesters, size_t requester_count,
	size_t *written)
{
	struct dma_handoff_header *header = buffer;
	struct dma_handoff_requester *output_requesters;
	size_t bytes;

	if (!buffer || !requesters || !written || !generation ||
	    !requester_count || requester_count > DMA_HANDOFF_MAX_REQUESTERS)
		return CB_ERR;
	bytes = handoff_size(requester_count);
	if (capacity < bytes || bytes > UINT32_MAX)
		return CB_ERR;
	memset(buffer, 0, bytes);
	header->revision = DMA_HANDOFF_REVISION;
	header->size = (uint32_t)bytes;
	header->header_size = sizeof(*header);
	header->requester_size = sizeof(*requesters);
	header->flags = DMA_HANDOFF_REQUIRED_FLAGS;
	header->granule_shift = DMA_HANDOFF_GRANULE_SHIFT;
	header->requester_count = (uint32_t)requester_count;
	header->generation = generation;
	output_requesters = (void *)((uint8_t *)buffer + sizeof(*header));
	memcpy(output_requesters, requesters, requester_count * sizeof(*requesters));
	header->crc32 = handoff_crc32(header);
	if (dma_handoff_validate(buffer, bytes) != CB_SUCCESS) {
		memset(buffer, 0, bytes);
		return CB_ERR;
	}
	*written = bytes;
	return CB_SUCCESS;
}

__weak bool payload_dma_handoff_blob(uintptr_t *address, size_t *bytes)
{
	(void)address;
	(void)bytes;
	return false;
}

__weak bool payload_resource_revision4_published(void)
{
	return false;
}

__weak uint64_t payload_resource_revision4_generation(void)
{
	return 0;
}

__weak size_t payload_resource_revision4_boot_count(void)
{
	return 0;
}

__weak bool payload_resource_revision4_boot_requester(uint16_t segment, uint16_t bdf)
{
	(void)segment;
	(void)bdf;
	return false;
}

enum cb_err lb_add_dma_handoff(struct lb_header *header)
{
	struct lb_dma_handoff *reference;
	uintptr_t address;
	size_t bytes;

	if (!header || !payload_resource_revision4_published() ||
	    !payload_dma_handoff_blob(&address, &bytes) || !address ||
	    bytes > UINT32_MAX ||
	    dma_handoff_validate((const void *)address, bytes) != CB_SUCCESS)
		return CB_ERR;
	if (((const struct dma_handoff_header *)address)->generation !=
	    payload_resource_revision4_generation())
		return CB_ERR;
	{
		const struct dma_handoff_header *blob = (const void *)address;
		const struct dma_handoff_requester *requesters = handoff_requesters(blob);
		const size_t boot_count = payload_resource_revision4_boot_count();

		if (!boot_count || blob->requester_count > boot_count)
			return CB_ERR;
		for (size_t index = 0; index < blob->requester_count; index++)
			if (!payload_resource_revision4_boot_requester(
				requesters[index].segment, requesters[index].bdf))
				return CB_ERR;
	}
	reference = (void *)lb_new_record(header);
	reference->tag = LB_TAG_DMA_HANDOFF;
	reference->size = sizeof(*reference);
	reference->address = address;
	reference->bytes = (uint32_t)bytes;
	reference->revision = DMA_HANDOFF_REVISION;
	reference->reserved = 0;
	return CB_SUCCESS;
}
