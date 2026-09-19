/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <boot/dma_handoff.h>
#include <commonlib/helpers.h>
#include <string.h>

#define DMA_PAGE_SIZE 4096U

static size_t handoff_size(size_t requester_count, size_t table_count)
{
	return sizeof(struct dma_handoff_header) +
		requester_count * sizeof(struct dma_handoff_requester) +
		table_count * sizeof(struct dma_handoff_table);
}

static const struct dma_handoff_requester *handoff_requesters(
	const struct dma_handoff_header *header)
{
	return (const void *)((const uint8_t *)header + header->header_size);
}

static const struct dma_handoff_table *handoff_tables(
	const struct dma_handoff_header *header)
{
	return (const void *)((const uint8_t *)handoff_requesters(header) +
		header->requester_count * header->requester_size);
}

static bool table_valid(const struct dma_handoff_table *table)
{
	uint64_t bytes;

	if (!table->base || (table->base & (DMA_PAGE_SIZE - 1U)) ||
	    !table->pages ||
	    table->flags != DMA_HANDOFF_TABLE_FLAGS || table->reserved0 ||
	    table->reserved1)
		return false;
	bytes = (uint64_t)table->pages * DMA_PAGE_SIZE;
	return table->base <= UINT64_MAX - bytes;
}

static bool table_owner_matches(const struct dma_handoff_table *table,
	uint16_t owner_type, uint32_t owner_id, uint32_t pages)
{
	return table->owner_type == owner_type && table->owner_id == owner_id &&
		table->pages == pages;
}

enum cb_err dma_handoff_validate(const void *buffer, size_t bytes)
{
	const struct dma_handoff_header *header = buffer;
	const struct dma_handoff_requester *requesters;
	const struct dma_handoff_table *tables;
	bool referenced[DMA_HANDOFF_MAX_TABLES] = { false };
	size_t expected;

	if (!buffer || bytes < sizeof(*header))
		return CB_ERR;
	if (header->revision != DMA_HANDOFF_REVISION ||
	    header->header_size != sizeof(*header) ||
	    header->requester_size != sizeof(*requesters) ||
	    header->table_size != sizeof(*tables) ||
	    header->flags != DMA_HANDOFF_REQUIRED_FLAGS || !header->generation ||
	    header->reserved[0] || header->reserved[1] ||
	    !header->requester_count ||
	    header->requester_count > DMA_HANDOFF_MAX_REQUESTERS ||
	    !header->table_count || header->table_count > DMA_HANDOFF_MAX_TABLES)
		return CB_ERR;
	expected = handoff_size(header->requester_count, header->table_count);
	if (expected > UINT32_MAX || header->size != expected || bytes != expected)
		return CB_ERR;
	requesters = handoff_requesters(header);
	tables = handoff_tables(header);
	for (size_t table = 0; table < header->table_count; table++) {
		uint64_t first_end;

		if (!table_valid(&tables[table]))
			return CB_ERR;
		first_end = tables[table].base +
			(uint64_t)tables[table].pages * DMA_PAGE_SIZE;
		for (size_t other = 0; other < table; other++) {
			uint64_t other_end = tables[other].base +
				(uint64_t)tables[other].pages * DMA_PAGE_SIZE;

			if (tables[table].base < other_end && tables[other].base < first_end)
				return CB_ERR;
		}
	}
	for (size_t slot = 0; slot < header->requester_count; slot++) {
		const struct dma_handoff_requester *requester = &requesters[slot];
		const uint32_t requester_id = ((uint32_t)requester->segment << 16) |
			requester->bdf;
		const uint32_t bus_id = ((uint32_t)requester->segment << 8) |
			(requester->bdf >> 8);

		if (!requester->domain || requester->flags != DMA_HANDOFF_REQUESTER_FLAGS ||
		    requester->reserved || requester->generation != header->generation ||
		    requester->root_table >= header->table_count ||
		    requester->context_table >= header->table_count ||
		    requester->hierarchy_table >= header->table_count ||
		    requester->root_table == requester->context_table ||
		    requester->root_table == requester->hierarchy_table ||
		    requester->context_table == requester->hierarchy_table)
			return CB_ERR;
		for (size_t other = 0; other < slot; other++)
			if ((requesters[other].segment == requester->segment &&
			     requesters[other].bdf == requester->bdf) ||
			    requesters[other].domain == requester->domain)
				return CB_ERR;
		if (!table_owner_matches(&tables[requester->root_table],
			DMA_HANDOFF_TABLE_GLOBAL, 0U, 1U) ||
		    !table_owner_matches(&tables[requester->context_table],
			DMA_HANDOFF_TABLE_BUS, bus_id, 1U) ||
		    !table_owner_matches(&tables[requester->hierarchy_table],
			DMA_HANDOFF_TABLE_REQUESTER, requester_id, 4U))
			return CB_ERR;
		referenced[requester->root_table] = true;
		referenced[requester->context_table] = true;
		referenced[requester->hierarchy_table] = true;
	}
	for (size_t table = 0; table < header->table_count; table++)
		if (!referenced[table])
			return CB_ERR;
	return CB_SUCCESS;
}

enum cb_err dma_handoff_build(void *buffer, size_t capacity,
	uint64_t generation,
	const struct dma_handoff_requester *requesters, size_t requester_count,
	const struct dma_handoff_table *tables, size_t table_count,
	size_t *written)
{
	struct dma_handoff_header *header = buffer;
	struct dma_handoff_requester *output_requesters;
	struct dma_handoff_table *output_tables;
	size_t bytes;

	if (!buffer || !requesters || !tables || !written || !generation ||
	    !requester_count || requester_count > DMA_HANDOFF_MAX_REQUESTERS ||
	    !table_count || table_count > DMA_HANDOFF_MAX_TABLES)
		return CB_ERR;
	bytes = handoff_size(requester_count, table_count);
	if (capacity < bytes || bytes > UINT32_MAX)
		return CB_ERR;
	memset(buffer, 0, bytes);
	header->revision = DMA_HANDOFF_REVISION;
	header->size = (uint32_t)bytes;
	header->header_size = sizeof(*header);
	header->requester_size = sizeof(*requesters);
	header->table_size = sizeof(*tables);
	header->flags = DMA_HANDOFF_REQUIRED_FLAGS;
	header->requester_count = (uint32_t)requester_count;
	header->table_count = (uint32_t)table_count;
	header->generation = generation;
	output_requesters = (void *)((uint8_t *)buffer + sizeof(*header));
	output_tables = (void *)((uint8_t *)output_requesters +
		requester_count * sizeof(*requesters));
	memcpy(output_requesters, requesters, requester_count * sizeof(*requesters));
	memcpy(output_tables, tables, table_count * sizeof(*tables));
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
	reference = (void *)lb_new_record(header);
	reference->tag = LB_TAG_DMA_HANDOFF;
	reference->size = sizeof(*reference);
	reference->address = address;
	reference->bytes = (uint32_t)bytes;
	reference->revision = DMA_HANDOFF_REVISION;
	reference->reserved = 0;
	return CB_SUCCESS;
}
