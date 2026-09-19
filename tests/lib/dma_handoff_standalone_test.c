/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/coreboot_tables.h>
#include <boot/dma_handoff.h>
#include <string.h>
#include <unistd.h>

#define GENERATION 0x1122334455667788ULL

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static const struct dma_handoff_requester requesters[] = {
	{ 0, 0x0018, 1, DMA_HANDOFF_REQUESTER_FLAGS, 0, 1, 3, 0, GENERATION },
	{ 0, 0x0100, 2, DMA_HANDOFF_REQUESTER_FLAGS, 0, 2, 4, 0, GENERATION },
};

static const struct dma_handoff_table tables[] = {
	{ 0x100000, 1, DMA_HANDOFF_TABLE_GLOBAL, 0, 0, DMA_HANDOFF_TABLE_FLAGS, 0 },
	{ 0x101000, 1, DMA_HANDOFF_TABLE_BUS, 0, 0, DMA_HANDOFF_TABLE_FLAGS, 0 },
	{ 0x102000, 1, DMA_HANDOFF_TABLE_BUS, 0, 1, DMA_HANDOFF_TABLE_FLAGS, 0 },
	{ 0x103000, 4, DMA_HANDOFF_TABLE_REQUESTER, 0, 0x18,
	  DMA_HANDOFF_TABLE_FLAGS, 0 },
	{ 0x107000, 4, DMA_HANDOFF_TABLE_REQUESTER, 0, 0x100,
	  DMA_HANDOFF_TABLE_FLAGS, 0 },
};

static const struct dma_handoff_requester q35_requester = {
	0, 0x0018, 1, DMA_HANDOFF_REQUESTER_FLAGS, 0, 1, 2, 0, GENERATION
};

static const struct dma_handoff_table q35_tables[] = {
	{ 0x100000, 1, DMA_HANDOFF_TABLE_GLOBAL, 0, 0, DMA_HANDOFF_TABLE_FLAGS, 0 },
	{ 0x101000, 1, DMA_HANDOFF_TABLE_BUS, 0, 0, DMA_HANDOFF_TABLE_FLAGS, 0 },
	{ 0x102000, 4, DMA_HANDOFF_TABLE_REQUESTER, 0, 0x18,
	  DMA_HANDOFF_TABLE_FLAGS, 0 },
};

static bool revision4_published;
static uint64_t revision4_generation;
static bool platform_blob_present;
static uintptr_t platform_blob_address;
static size_t platform_blob_bytes;
static struct lb_dma_handoff output_record;

bool payload_resource_revision4_published(void)
{
	return revision4_published;
}

uint64_t payload_resource_revision4_generation(void)
{
	return revision4_generation;
}

bool payload_dma_handoff_blob(uintptr_t *address, size_t *bytes)
{
	if (!platform_blob_present)
		return false;
	*address = platform_blob_address;
	*bytes = platform_blob_bytes;
	return true;
}

struct lb_record *lb_new_record(struct lb_header *header)
{
	(void)header;
	memset(&output_record, 0, sizeof(output_record));
	return (void *)&output_record;
}

static size_t make_blob(uint8_t *blob)
{
	size_t written = 0;

	assert(dma_handoff_build(blob, 256, GENERATION, requesters,
		ARRAY_SIZE(requesters), tables, ARRAY_SIZE(tables), &written) == CB_SUCCESS);
	assert(written == 244);
	return written;
}

static size_t make_q35_blob(uint8_t *blob)
{
	size_t written = 0;

	assert(dma_handoff_build(blob, 256, GENERATION, &q35_requester, 1,
		q35_tables, ARRAY_SIZE(q35_tables), &written) == CB_SUCCESS);
	assert(written == 156);
	return written;
}

static void mutations(void)
{
	uint8_t blob[256];
	struct dma_handoff_header *header = (void *)blob;
	struct dma_handoff_requester *output_requesters;
	struct dma_handoff_table *output_tables;
	size_t bytes = make_blob(blob);

#define REJECT(member, value) do { \
	__typeof__(header->member) saved = header->member; \
	header->member = (value); \
	assert(dma_handoff_validate(blob, bytes) == CB_ERR); \
	header->member = saved; \
} while (0)
	REJECT(revision, 2);
	REJECT(size, bytes - 1);
	REJECT(header_size, sizeof(*header) - 1);
	REJECT(requester_size, sizeof(struct dma_handoff_requester) - 1);
	REJECT(table_size, sizeof(struct dma_handoff_table) - 1);
	REJECT(flags, header->flags | (1U << 15));
	REJECT(requester_count, DMA_HANDOFF_MAX_REQUESTERS + 1);
	REJECT(table_count, DMA_HANDOFF_MAX_TABLES + 1);
	REJECT(generation, 0);
	REJECT(reserved[0], 1);
#undef REJECT
	assert(dma_handoff_validate(blob, bytes - 1) == CB_ERR);
	assert(dma_handoff_validate(blob, bytes + 1) == CB_ERR);

	output_requesters = (void *)(blob + sizeof(*header));
	output_tables = (void *)(output_requesters + header->requester_count);
#define REJECT_REQUESTER(index, member, value) do { \
	__typeof__(output_requesters[index].member) saved = output_requesters[index].member; \
	output_requesters[index].member = (value); \
	assert(dma_handoff_validate(blob, bytes) == CB_ERR); \
	output_requesters[index].member = saved; \
} while (0)
	REJECT_REQUESTER(1, bdf, output_requesters[0].bdf);
	REJECT_REQUESTER(1, domain, output_requesters[0].domain);
	REJECT_REQUESTER(0, flags, 0);
	REJECT_REQUESTER(0, generation, GENERATION + 1);
	REJECT_REQUESTER(0, root_table, header->table_count);
	REJECT_REQUESTER(0, hierarchy_table, output_requesters[0].context_table);
	REJECT_REQUESTER(0, reserved, 1);
#undef REJECT_REQUESTER

#define REJECT_TABLE(index, member, value) do { \
	__typeof__(output_tables[index].member) saved = output_tables[index].member; \
	output_tables[index].member = (value); \
	assert(dma_handoff_validate(blob, bytes) == CB_ERR); \
	output_tables[index].member = saved; \
} while (0)
	REJECT_TABLE(0, base, output_tables[0].base + 1);
	REJECT_TABLE(0, pages, 0);
	REJECT_TABLE(0, flags, output_tables[0].flags | 4);
	REJECT_TABLE(0, owner_type, DMA_HANDOFF_TABLE_BUS);
	REJECT_TABLE(1, owner_id, 1);
	REJECT_TABLE(3, base, output_tables[2].base);
	REJECT_TABLE(4, reserved1, 1);
#undef REJECT_TABLE
}

static void publisher(void)
{
	uint8_t blob[256];
	size_t bytes = make_blob(blob);
	struct lb_header *header = (void *)blob;

	platform_blob_present = true;
	platform_blob_address = (uintptr_t)blob;
	platform_blob_bytes = bytes;
	revision4_generation = GENERATION;
	revision4_published = false;
	assert(lb_add_dma_handoff(header) == CB_ERR);
	revision4_published = true;
	platform_blob_present = false;
	assert(lb_add_dma_handoff(header) == CB_ERR);
	platform_blob_present = true;
	assert(lb_add_dma_handoff(header) == CB_SUCCESS);
	assert(output_record.tag == LB_TAG_DMA_HANDOFF);
	assert(output_record.size == sizeof(output_record));
	assert(output_record.address == (uintptr_t)blob);
	assert(output_record.bytes == bytes);
	assert(output_record.revision == DMA_HANDOFF_REVISION);
	assert(output_record.reserved == 0);
	revision4_generation++;
	assert(lb_add_dma_handoff(header) == CB_ERR);
	revision4_generation--;
	((struct dma_handoff_header *)blob)->flags ^= 1U;
	assert(lb_add_dma_handoff(header) == CB_ERR);
}

int main(int argc, char **argv)
{
	uint8_t blob[256];
	size_t bytes = make_blob(blob);

	if (argc == 2 && !strcmp(argv[1], "--fixture"))
		return write(STDOUT_FILENO, blob, bytes) == (ssize_t)bytes ? 0 : 1;
	if (argc == 2 && !strcmp(argv[1], "--q35-fixture")) {
		bytes = make_q35_blob(blob);
		return write(STDOUT_FILENO, blob, bytes) == (ssize_t)bytes ? 0 : 1;
	}
	assert(argc == 1);
	assert(dma_handoff_validate(NULL, 0) == CB_ERR);
	mutations();
	publisher();
	return 0;
}
