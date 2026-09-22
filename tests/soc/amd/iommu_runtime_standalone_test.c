/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/iommu_runtime.h>
#include <string.h>

#define ENABLED		1ULL
#define SEGMENTS_SHIFT	34
#define SEGMENTS_SUPPORTED_SHIFT 38
#define SEGMENTS_SUPPORTED(count) ((uint64_t)(count) << SEGMENTS_SUPPORTED_SHIFT)
#define TABLE(base, pages) ((base) | ((pages) - 1))

static int failures;

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

int main(void)
{
	test_unsegmented_table();
	test_segmented_table();
	test_middle_segment_capability();
	test_sparse_segment_rejects_missing_entry();
	test_invalid_registers_fail_closed();
	test_bad_arguments();
	return failures != 0;
}
