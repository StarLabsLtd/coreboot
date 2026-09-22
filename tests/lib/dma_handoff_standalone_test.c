/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/coreboot_tables.h>
#include <boot/dma_handoff.h>
#include <crc_byte.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#define GENERATION 0x1122334455667788ULL
#define MAX_BLOB_BYTES (sizeof(struct dma_handoff_header) + \
	DMA_HANDOFF_MAX_REQUESTERS * sizeof(struct dma_handoff_requester))

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
	{ 0, 0x0018, 1, DMA_HANDOFF_REQUESTER_FLAGS,
	  0x200000, 0x800000, 1, DMA_HANDOFF_ARENA_FLAGS },
	{ 0, 0x0100, 2, DMA_HANDOFF_REQUESTER_FLAGS,
	  0x204000, 0x800000, 2, DMA_HANDOFF_ARENA_FLAGS },
};

static const struct dma_handoff_requester q35_requesters[] = {
	{ 0, 0x0018, 1, DMA_HANDOFF_REQUESTER_FLAGS,
	  0x200000, 0x80000000, 32, DMA_HANDOFF_ARENA_FLAGS },
	{ 0, 0x0020, 2, DMA_HANDOFF_REQUESTER_FLAGS,
	  0x220000, 0x90000000, 128, DMA_HANDOFF_ARENA_FLAGS },
};

static bool revision4_published;
static uint64_t revision4_generation;
static bool platform_blob_present;
static uintptr_t platform_blob_address;
static size_t platform_blob_bytes;
static size_t boot_count;
static uint16_t boot_bdfs[DMA_HANDOFF_MAX_REQUESTERS];
static struct lb_dma_handoff output_record;

bool payload_resource_revision4_published(void) { return revision4_published; }
uint64_t payload_resource_revision4_generation(void) { return revision4_generation; }
size_t payload_resource_revision4_boot_count(void)
{
	return revision4_published ? boot_count : 0;
}
bool payload_resource_revision4_boot_requester(uint16_t segment, uint16_t bdf)
{
	if (!revision4_published || segment)
		return false;
	for (size_t index = 0; index < boot_count; index++)
		if (boot_bdfs[index] == bdf)
			return true;
	return false;
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

static uint32_t crc(const struct dma_handoff_header *header)
{
	const uint8_t *bytes = (const void *)header;
	const size_t offset = offsetof(struct dma_handoff_header, crc32);
	uint32_t value = 0;

	for (size_t index = 0; index < header->size; index++)
		value = crc32_byte(value, index >= offset &&
			index < offset + sizeof(header->crc32) ? 0 : bytes[index]);
	return value;
}

static void refresh_crc(struct dma_handoff_header *header)
{
	header->crc32 = crc(header);
}

static size_t make_blob(uint8_t *blob)
{
	size_t written = 0;

	assert(dma_handoff_build(blob, 512, GENERATION, requesters,
		ARRAY_SIZE(requesters), &written) == CB_SUCCESS);
	assert(written == 104);
	return written;
}

static size_t make_q35_blob(uint8_t *blob)
{
	size_t written = 0;

	assert(dma_handoff_build(blob, 512, GENERATION, q35_requesters,
		ARRAY_SIZE(q35_requesters), &written) == CB_SUCCESS);
	assert(written == 104);
	return written;
}

static void count_coverage(void)
{
	static struct dma_handoff_requester many[DMA_HANDOFF_MAX_REQUESTERS];
	static uint8_t blob[MAX_BLOB_BYTES];
	const size_t counts[] = { 1, 2, 3, DMA_HANDOFF_MAX_REQUESTERS };

	for (size_t index = 0; index < ARRAY_SIZE(many); index++)
		many[index] = (struct dma_handoff_requester) {
			.segment = 0, .bdf = index, .protection_domain = index + 1,
			.flags = DMA_HANDOFF_REQUESTER_FLAGS,
			.arena_cpu_base = 0x100000ULL + index * 0x1000ULL,
			.arena_device_base = 0x80000000ULL,
			.arena_pages = 1, .arena_flags = DMA_HANDOFF_ARENA_FLAGS,
		};
	for (size_t index = 0; index < ARRAY_SIZE(counts); index++) {
		size_t written = 0;

		assert(dma_handoff_build(blob, sizeof(blob), GENERATION, many,
			counts[index], &written) == CB_SUCCESS);
		assert(written == sizeof(struct dma_handoff_header) +
			counts[index] * sizeof(struct dma_handoff_requester));
		assert(dma_handoff_validate(blob, written) == CB_SUCCESS);
	}
	assert(dma_handoff_build(blob, sizeof(blob), GENERATION, many,
		DMA_HANDOFF_MAX_REQUESTERS + 1U, &(size_t){0}) == CB_ERR);
}

static void mutations(void)
{
	uint8_t blob[512];
	struct dma_handoff_header *header = (void *)blob;
	struct dma_handoff_requester *output_requesters;
	size_t bytes = make_blob(blob);

#define REJECT(member, value) do { \
	__typeof__(header->member) saved = header->member; \
	header->member = (value); refresh_crc(header); \
	assert(dma_handoff_validate(blob, bytes) == CB_ERR); \
	header->member = saved; refresh_crc(header); \
} while (0)
	REJECT(revision, 1);
	REJECT(size, bytes - 1);
	REJECT(header_size, sizeof(*header) - 1);
	REJECT(requester_size, sizeof(struct dma_handoff_requester) - 1);
	REJECT(flags, header->flags | (1U << 15));
	REJECT(granule_shift, DMA_HANDOFF_GRANULE_SHIFT + 1);
	REJECT(requester_count, DMA_HANDOFF_MAX_REQUESTERS + 1);
	REJECT(generation, 0);
	REJECT(reserved[0], 1);
#undef REJECT
	header->crc32 ^= 1U;
	assert(dma_handoff_validate(blob, bytes) == CB_ERR);
	header->crc32 ^= 1U;
	assert(crc(header) == header->crc32);
	{
		const uint32_t saved_crc = header->crc32;

		header->crc32 = 0;
		assert(crc(header) == saved_crc);
		header->crc32 = UINT32_MAX;
		assert(crc(header) == saved_crc);
		header->crc32 = saved_crc;
	}
	assert(dma_handoff_validate(blob, bytes - 1) == CB_ERR);
	assert(dma_handoff_validate(blob, bytes + 1) == CB_ERR);

	output_requesters = (void *)(blob + sizeof(*header));
#define REJECT_REQUESTER(index, member, value) do { \
	__typeof__(output_requesters[index].member) saved = output_requesters[index].member; \
	output_requesters[index].member = (value); refresh_crc(header); \
	assert(dma_handoff_validate(blob, bytes) == CB_ERR); \
	output_requesters[index].member = saved; refresh_crc(header); \
} while (0)
	REJECT_REQUESTER(1, bdf, output_requesters[0].bdf);
	REJECT_REQUESTER(1, protection_domain,
		output_requesters[0].protection_domain);
	REJECT_REQUESTER(0, flags, 0);
	REJECT_REQUESTER(0, arena_cpu_base, 0);
	REJECT_REQUESTER(0, arena_cpu_base, 0x200001);
	REJECT_REQUESTER(0, arena_cpu_base, UINT64_MAX - 0xfffU);
	REJECT_REQUESTER(0, arena_cpu_base,
		(uintptr_t)blob & ~(uint64_t)((1U << DMA_HANDOFF_GRANULE_SHIFT) - 1U));
	REJECT_REQUESTER(0, arena_device_base, 0);
	REJECT_REQUESTER(0, arena_device_base, 0x800001);
	REJECT_REQUESTER(0, arena_device_base, UINT64_MAX - 0xfffU);
	REJECT_REQUESTER(0, arena_pages, 0);
	REJECT_REQUESTER(0, arena_flags, 0);
	REJECT_REQUESTER(0, arena_flags, DMA_HANDOFF_ARENA_FLAGS | (1U << 15));
	REJECT_REQUESTER(1, arena_cpu_base, output_requesters[0].arena_cpu_base);
#undef REJECT_REQUESTER
	output_requesters[1].arena_device_base = output_requesters[0].arena_device_base;
	refresh_crc(header);
	assert(dma_handoff_validate(blob, bytes) == CB_SUCCESS);
}

static void publisher(void)
{
	uint8_t blob[512];
	size_t bytes = make_blob(blob);
	struct lb_header *header = (void *)blob;

	platform_blob_present = true;
	platform_blob_address = (uintptr_t)blob;
	platform_blob_bytes = bytes;
	revision4_generation = GENERATION;
	boot_count = 2;
	boot_bdfs[0] = requesters[0].bdf;
	boot_bdfs[1] = requesters[1].bdf;
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
	boot_count = 1;
	assert(lb_add_dma_handoff(header) == CB_ERR);
	boot_count = 2;
	boot_bdfs[1]++;
	assert(lb_add_dma_handoff(header) == CB_ERR);
	boot_bdfs[1]--;
	((struct dma_handoff_header *)blob)->flags ^= 1U;
	assert(lb_add_dma_handoff(header) == CB_ERR);
}

static void publisher_count_coverage(void)
{
	static struct dma_handoff_requester many[DMA_HANDOFF_MAX_REQUESTERS];
	static uint8_t blob[MAX_BLOB_BYTES];
	const size_t counts[] = { 3, DMA_HANDOFF_MAX_REQUESTERS };

	for (size_t index = 0; index < ARRAY_SIZE(many); index++) {
		many[index] = (struct dma_handoff_requester) {
			.bdf = index, .protection_domain = index + 1,
			.flags = DMA_HANDOFF_REQUESTER_FLAGS,
			.arena_cpu_base = 0x100000ULL + index * 0x1000ULL,
			.arena_device_base = 0x80000000ULL,
			.arena_pages = 1, .arena_flags = DMA_HANDOFF_ARENA_FLAGS,
		};
		boot_bdfs[index] = index;
	}
	for (size_t index = 0; index < ARRAY_SIZE(counts); index++) {
		size_t written = 0;

		assert(dma_handoff_build(blob, sizeof(blob), GENERATION, many,
			counts[index], &written) == CB_SUCCESS);
		platform_blob_present = true;
		platform_blob_address = (uintptr_t)blob;
		platform_blob_bytes = written;
		revision4_generation = GENERATION;
		revision4_published = true;
		boot_count = counts[index];
		assert(lb_add_dma_handoff((void *)blob) == CB_SUCCESS);
	}
}

int main(int argc, char **argv)
{
	uint8_t blob[512];
	size_t bytes = make_blob(blob);

	if (argc == 2 && !strcmp(argv[1], "--fixture"))
		return write(STDOUT_FILENO, blob, bytes) == (ssize_t)bytes ? 0 : 1;
	if (argc == 2 && !strcmp(argv[1], "--q35-fixture")) {
		bytes = make_q35_blob(blob);
		return write(STDOUT_FILENO, blob, bytes) == (ssize_t)bytes ? 0 : 1;
	}
	assert(argc == 1);
	assert(dma_handoff_validate(NULL, 0) == CB_ERR);
	count_coverage();
	mutations();
	publisher();
	publisher_count_coverage();
	return 0;
}
