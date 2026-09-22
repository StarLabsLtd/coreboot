/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/iommu_runtime.h>
#include <string.h>

#define ENABLED		1ULL
#define SEGMENTS_SHIFT	34
#define SEGMENTS_SUPPORTED_SHIFT 38
#define SEGMENTS_SUPPORTED(count) ((uint64_t)(count) << SEGMENTS_SUPPORTED_SHIFT)
#define TABLE(base, pages) ((base) | ((pages) - 1))

static int failures;

static uint8_t dma_device_table[AMD_IOMMU_PAGE_SIZE]
	__aligned(AMD_IOMMU_PAGE_SIZE);
static uint8_t dma_page_tables[2 * AMD_IOMMU_PAGE_SIZE]
	__aligned(AMD_IOMMU_PAGE_SIZE);
static uint8_t dma_arenas[4 * AMD_IOMMU_PAGE_SIZE]
	__aligned(AMD_IOMMU_PAGE_SIZE);

#define CHECK(condition) do { if (!(condition)) failures++; } while (0)

static void test_unsegmented_table(void)
{
	struct amd_iommu_device_table table;
	uint64_t registers[AMD_IOMMU_DEVICE_TABLE_SEGMENTS_MAX] = {
		TABLE(0x100000, 512),
	};
	uint64_t address;

	CHECK(amd_iommu_decode_device_table(ENABLED, 0, registers, &table) ==
	       CB_SUCCESS);
	CHECK(table.segment_count == 1);
	CHECK(table.segment[0].base == 0x100000);
	CHECK(table.segment[0].entries == 65536);
	CHECK(amd_iommu_device_table_entry_address(&table, 0xffff, &address) ==
	       CB_SUCCESS);
	CHECK(address == 0x2fffe0);
}

static void test_segmented_table(void)
{
	struct amd_iommu_device_table table;
	uint64_t registers[AMD_IOMMU_DEVICE_TABLE_SEGMENTS_MAX] = {
		TABLE(0x100000, 64), TABLE(0x200000, 64),
		TABLE(0x300000, 64), TABLE(0x400000, 64),
		TABLE(0x500000, 64), TABLE(0x600000, 64),
		TABLE(0x700000, 64), TABLE(0x800000, 64),
	};
	uint64_t address;

	CHECK(amd_iommu_decode_device_table(ENABLED | (3ULL << SEGMENTS_SHIFT),
					    SEGMENTS_SUPPORTED(3), registers, &table) ==
	      CB_SUCCESS);
	CHECK(table.segment_count == 8);
	CHECK(table.segment[7].entries == 8192);
	CHECK(amd_iommu_device_table_entry_address(&table, 0xe000, &address) ==
	       CB_SUCCESS);
	CHECK(address == 0x800000);
	CHECK(amd_iommu_device_table_entry_address(&table, 0xffff, &address) ==
	       CB_SUCCESS);
	CHECK(address == 0x83ffe0);
}

static void test_middle_segment_capability(void)
{
	struct amd_iommu_device_table table;
	uint64_t registers[AMD_IOMMU_DEVICE_TABLE_SEGMENTS_MAX] = {
		TABLE(0x100000, 128), TABLE(0x200000, 128),
		TABLE(0x300000, 128), TABLE(0x400000, 128),
	};

	CHECK(amd_iommu_decode_device_table(ENABLED | (2ULL << SEGMENTS_SHIFT),
					    SEGMENTS_SUPPORTED(2), registers, &table) ==
	      CB_SUCCESS);
	CHECK(table.segment_count == 4);
	CHECK(amd_iommu_decode_device_table(ENABLED | (2ULL << SEGMENTS_SHIFT),
					    SEGMENTS_SUPPORTED(1), registers, &table) ==
	      CB_ERR_ARG);
	CHECK(table.segment_count == 0);
}

static void test_sparse_segment_rejects_missing_entry(void)
{
	struct amd_iommu_device_table table;
	uint64_t registers[AMD_IOMMU_DEVICE_TABLE_SEGMENTS_MAX] = {
		TABLE(0x100000, 1), TABLE(0x200000, 1),
	};
	uint64_t address = 0xdeadbeef;

	CHECK(amd_iommu_decode_device_table(ENABLED | (1ULL << SEGMENTS_SHIFT),
					    SEGMENTS_SUPPORTED(1), registers, &table) ==
	      CB_SUCCESS);
	CHECK(amd_iommu_device_table_entry_address(&table, 127, &address) ==
	       CB_SUCCESS);
	CHECK(address == 0x100fe0);
	CHECK(amd_iommu_device_table_entry_address(&table, 128, &address) ==
	       CB_ERR_ARG);
	CHECK(address == 0x100fe0);
	CHECK(amd_iommu_device_table_entry_address(&table, 0x8000, &address) ==
	       CB_SUCCESS);
	CHECK(address == 0x200000);
}

static void test_invalid_registers_fail_closed(void)
{
	struct amd_iommu_device_table table;
	uint64_t registers[AMD_IOMMU_DEVICE_TABLE_SEGMENTS_MAX] = {
		TABLE(0x100000, 1), TABLE(0x200000, 1),
	};

	memset(&table, 0xa5, sizeof(table));
	CHECK(amd_iommu_decode_device_table(0, 0, registers, &table) == CB_ERR_ARG);
	CHECK(table.segment_count == 0);

	registers[0] = TABLE(0, 1);
	CHECK(amd_iommu_decode_device_table(ENABLED, 0, registers, &table) ==
	       CB_ERR_ARG);
	CHECK(table.segment_count == 0);

	registers[0] = TABLE(0x100000, 1) | (1ULL << 52);
	CHECK(amd_iommu_decode_device_table(ENABLED, 0, registers, &table) ==
	       CB_ERR_ARG);
	CHECK(table.segment_count == 0);

	registers[0] = TABLE(0x100000, 2);
	registers[1] = TABLE(0x101000, 1);
	CHECK(amd_iommu_decode_device_table(ENABLED | (1ULL << SEGMENTS_SHIFT),
					    SEGMENTS_SUPPORTED(1), registers, &table) ==
	      CB_ERR_ARG);
	CHECK(table.segment_count == 0);

	registers[0] = TABLE(0x100000, 257);
	registers[1] = TABLE(0x300000, 1);
	CHECK(amd_iommu_decode_device_table(ENABLED | (1ULL << SEGMENTS_SHIFT),
					    SEGMENTS_SUPPORTED(1), registers, &table) ==
	      CB_ERR_ARG);
	CHECK(table.segment_count == 0);

	registers[0] = TABLE(0x100000, 1);
	CHECK(amd_iommu_decode_device_table(ENABLED | (4ULL << SEGMENTS_SHIFT),
					    SEGMENTS_SUPPORTED(3), registers, &table) ==
	      CB_ERR_ARG);
	CHECK(table.segment_count == 0);

	registers[0] = TABLE(0x100000, 1);
	registers[1] = TABLE(0x200000, 1);
	CHECK(amd_iommu_decode_device_table(ENABLED | (1ULL << SEGMENTS_SHIFT),
					    0, registers, &table) == CB_ERR_ARG);
	CHECK(table.segment_count == 0);
	CHECK(amd_iommu_decode_device_table(ENABLED | (3ULL << SEGMENTS_SHIFT),
					    SEGMENTS_SUPPORTED(2), registers, &table) ==
	      CB_ERR_ARG);
	CHECK(table.segment_count == 0);
}

static void test_bad_arguments(void)
{
	struct amd_iommu_device_table table = { 0 };
	uint64_t registers[AMD_IOMMU_DEVICE_TABLE_SEGMENTS_MAX] = { 0 };
	uint64_t address;

	CHECK(amd_iommu_decode_device_table(ENABLED, 0, NULL, &table) == CB_ERR_ARG);
	CHECK(amd_iommu_decode_device_table(ENABLED, 0, registers, NULL) == CB_ERR_ARG);
	CHECK(amd_iommu_device_table_entry_address(NULL, 0, &address) == CB_ERR_ARG);
	CHECK(amd_iommu_device_table_entry_address(&table, 0, NULL) == CB_ERR_ARG);
	table.segment_count = 3;
	CHECK(amd_iommu_device_table_entry_address(&table, 0, &address) ==
	       CB_ERR_ARG);
	table.segment_count = 1;
	table.segment[0].base = 0x100001;
	table.segment[0].entries = 1;
	CHECK(amd_iommu_device_table_entry_address(&table, 0, &address) ==
	       CB_ERR_ARG);
	table.segment[0].base = 0x100000;
	table.segment[0].entries = 65537;
	CHECK(amd_iommu_device_table_entry_address(&table, 0, &address) ==
	       CB_ERR_ARG);
	table.segment_count = 2;
	table.segment[0].entries = 256;
	table.segment[1].base = 0x101000;
	table.segment[1].entries = 128;
	CHECK(amd_iommu_device_table_entry_address(&table, 0, &address) ==
	       CB_ERR_ARG);
}

static void test_owned_dma_state(void)
{
	const struct amd_iommu_dma_requester requesters[] = {
		{
			.device_id = 0x18,
			.protection_domain = 1,
			.arena_cpu_base = (uintptr_t)&dma_arenas[0],
			.arena_device_base = 0x1000,
			.arena_pages = 1,
		},
		{
			.device_id = 0x20,
			.protection_domain = 2,
			.arena_cpu_base = (uintptr_t)&dma_arenas[AMD_IOMMU_PAGE_SIZE],
			.arena_device_base = 0x1000,
			.arena_pages = 3,
		},
	};
	uint64_t *device_words = (void *)dma_device_table;
	uint64_t *page_words = (void *)dma_page_tables;

	memset(dma_device_table, 0xa5, sizeof(dma_device_table));
	memset(dma_page_tables, 0xa5, sizeof(dma_page_tables));
	CHECK(amd_iommu_device_table_bytes(0) == AMD_IOMMU_PAGE_SIZE);
	CHECK(amd_iommu_device_table_bytes(127) == AMD_IOMMU_PAGE_SIZE);
	CHECK(amd_iommu_device_table_bytes(128) == 2 * AMD_IOMMU_PAGE_SIZE);
	CHECK(amd_iommu_device_table_bytes(UINT16_MAX) == 512 * AMD_IOMMU_PAGE_SIZE);
	CHECK(amd_iommu_build_dma_state(dma_device_table, sizeof(dma_device_table),
		dma_page_tables, sizeof(dma_page_tables), requesters,
		ARRAY_SIZE(requesters)) == CB_SUCCESS);
	CHECK(amd_iommu_dma_state_matches(dma_device_table,
		sizeof(dma_device_table), dma_page_tables, sizeof(dma_page_tables),
		requesters, ARRAY_SIZE(requesters)));
	CHECK(device_words[0] == 0);
	CHECK((device_words[0x18 * 4] & 3U) == 3U);
	CHECK(device_words[0x18 * 4 + 1] == 1);
	CHECK(device_words[0x20 * 4 + 1] == 2);
	CHECK(page_words[0] == 0);
	CHECK((page_words[1] & 1U) == 1U);
	CHECK((page_words[512 + 1] & 1U) == 1U);
	CHECK((page_words[512 + 4] & 1U) == 0);

	page_words[512 + 4] = 1;
	CHECK(!amd_iommu_dma_state_matches(dma_device_table,
		sizeof(dma_device_table), dma_page_tables, sizeof(dma_page_tables),
		requesters, ARRAY_SIZE(requesters)));
	page_words[512 + 4] = 0;
	device_words[3] = 1;
	CHECK(!amd_iommu_dma_state_matches(dma_device_table,
		sizeof(dma_device_table), dma_page_tables, sizeof(dma_page_tables),
		requesters, ARRAY_SIZE(requesters)));
	device_words[3] = 0;
	device_words[0x18 * 4] ^= 1ULL << 61;
	CHECK(!amd_iommu_dma_state_matches(dma_device_table,
		sizeof(dma_device_table), dma_page_tables, sizeof(dma_page_tables),
		requesters, ARRAY_SIZE(requesters)));
	device_words[0x18 * 4] ^= 1ULL << 61;
	page_words[1] ^= 1ULL << 62;
	CHECK(!amd_iommu_dma_state_matches(dma_device_table,
		sizeof(dma_device_table), dma_page_tables, sizeof(dma_page_tables),
		requesters, ARRAY_SIZE(requesters)));
}

static void test_invalid_dma_state(void)
{
	struct amd_iommu_dma_requester requester = {
		.device_id = 0x18,
		.protection_domain = 1,
		.arena_cpu_base = (uintptr_t)dma_arenas,
		.arena_device_base = 0x1000,
		.arena_pages = 1,
	};

#define REJECT(field, value) do { \
	const __typeof__(requester.field) saved = requester.field; \
	requester.field = (value); \
	CHECK(amd_iommu_build_dma_state(dma_device_table, sizeof(dma_device_table), \
		dma_page_tables, AMD_IOMMU_PAGE_SIZE, &requester, 1) == CB_ERR_ARG); \
	requester.field = saved; \
} while (0)

	REJECT(protection_domain, 0);
	REJECT(arena_cpu_base, 0);
	REJECT(arena_cpu_base, (uintptr_t)dma_arenas + 1);
	REJECT(arena_cpu_base, 1ULL << 52);
	REJECT(arena_device_base, 0);
	REJECT(arena_device_base, 1);
	REJECT(arena_device_base, 2 * 1024 * 1024ULL);
	REJECT(arena_pages, 0);
	REJECT(arena_pages, 512);
	REJECT(device_id, 128);
	CHECK(amd_iommu_build_dma_state(dma_device_table + 1,
		sizeof(dma_device_table) - 1, dma_page_tables, AMD_IOMMU_PAGE_SIZE,
		&requester, 1) == CB_ERR_ARG);
	CHECK(amd_iommu_build_dma_state(dma_device_table,
		sizeof(dma_device_table), dma_page_tables + 1,
		AMD_IOMMU_PAGE_SIZE - 1, &requester, 1) == CB_ERR_ARG);
	CHECK(amd_iommu_build_dma_state(dma_device_table,
		sizeof(dma_device_table), dma_page_tables,
		2 * AMD_IOMMU_PAGE_SIZE, &requester, 1) == CB_ERR_ARG);
	requester.arena_pages = 511;
	CHECK(amd_iommu_build_dma_state(dma_device_table,
		sizeof(dma_device_table), dma_page_tables, AMD_IOMMU_PAGE_SIZE,
		&requester, 1) == CB_SUCCESS);
	requester.arena_pages = 1;
	{
		struct amd_iommu_dma_requester duplicate[2] = { requester, requester };

		duplicate[1].device_id++;
		CHECK(amd_iommu_build_dma_state(dma_device_table,
			sizeof(dma_device_table), dma_page_tables,
			2 * AMD_IOMMU_PAGE_SIZE, duplicate, ARRAY_SIZE(duplicate)) ==
			CB_ERR_ARG);
		duplicate[1].protection_domain++;
		CHECK(amd_iommu_build_dma_state(dma_device_table,
			sizeof(dma_device_table), dma_page_tables,
			2 * AMD_IOMMU_PAGE_SIZE, duplicate, ARRAY_SIZE(duplicate)) ==
			CB_ERR_ARG);
		duplicate[1].arena_cpu_base += AMD_IOMMU_PAGE_SIZE;
		CHECK(amd_iommu_build_dma_state(dma_device_table,
			sizeof(dma_device_table), dma_page_tables,
			2 * AMD_IOMMU_PAGE_SIZE, duplicate, ARRAY_SIZE(duplicate)) ==
			CB_SUCCESS);
	}
#undef REJECT
}

int main(void)
{
	test_unsegmented_table();
	test_segmented_table();
	test_middle_segment_capability();
	test_sparse_segment_rejects_missing_entry();
	test_invalid_registers_fail_closed();
	test_bad_arguments();
	test_owned_dma_state();
	test_invalid_dma_state();
	return failures != 0;
}
