/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>

static uint8_t apob[0x1000] __aligned(64);
#undef CONFIG_PSP_APOB_DRAM_ADDRESS
#define CONFIG_PSP_APOB_DRAM_ADDRESS ((uintptr_t)apob)
#include <vendorcode/amd/opensil/phoenix_poc/memmap.c>

static APOB_SYSTEM_MEMORY_MAP_TYPE_STRUCT *record;
static MEMORY_HOLE_DESCRIPTOR *holes;
static void *map_override;
static uint32_t reported_count;
static uint64_t reported_top;
static SIL_STATUS reported_status;
static bool missing_map;
static unsigned int queries;

void *cbmem_find(uint32_t id)
{
	assert_int_equal(id, CBMEM_ID_AMD_OPENSIL);
	return (void *)0x1000;
}

void SIL_STATUS_report(const char *function, const int status) { }

SIL_STATUS xPrfGetSystemMemoryMap(SIL_CONTEXT *context, uint32_t *count,
				uint64_t *top, void **map)
{
	assert_int_equal(context->SilMemBaseAddress, 0x1000);
	queries++;
	*count = reported_count;
	*top = reported_top;
	*map = missing_map ? NULL : map_override ? map_override : holes;
	return reported_status;
}

static int setup(void **unused)
{
	memset(apob, 0, sizeof(apob));
	record = (void *)(apob + 0x100);
	record->ApobTypeHeader.TypeSize = sizeof(*record);
	holes = record->ApobSystemMap.HoleInfo;
	map_override = NULL;
	holes[0] = (MEMORY_HOLE_DESCRIPTOR) { .Base = 2ULL * GiB, .Size = MiB, .Type = MMIO };
	reported_count = 1;
	reported_top = 4ULL * GiB;
	reported_status = SilPass;
	missing_map = false;
	queries = 0;
	return 0;
}

static void valid_low_memory(void **unused)
{
	assert_true(opensil_memory_region_is_ram(MiB, 255 * MiB));
	assert_true(opensil_memory_region_is_ram(holes[0].Base - MiB, MiB));
	assert_true(opensil_memory_region_is_ram(holes[0].Base + MiB, MiB));
}

static void reject_reserved_holes(void **unused)
{
	assert_false(opensil_memory_region_is_ram(holes[0].Base - MiB, MiB + 1));
	holes[0].Type = (MEMORY_HOLE_TYPES)0xffff;
	assert_false(opensil_memory_region_is_ram(holes[0].Base, MiB));
}

static void reject_invalid_bounds(void **unused)
{
	assert_false(opensil_memory_region_is_ram(MiB, 0));
	assert_false(opensil_memory_region_is_ram(UINT64_MAX - MiB, 2 * MiB));
	assert_int_equal(queries, 0);
	assert_false(opensil_memory_region_is_ram(reported_top, MiB));
	assert_false(opensil_memory_region_is_ram(reported_top - MiB, MiB + 1));
	holes[0].Base = UINT64_MAX - MiB;
	holes[0].Size = 2 * MiB;
	assert_false(opensil_memory_region_is_ram(MiB, MiB));
}

static void reject_unavailable_map(void **unused)
{
	missing_map = true;
	assert_false(opensil_memory_region_is_ram(MiB, MiB));
	missing_map = false;
	reported_status = SilInvalidParameter;
	assert_false(opensil_memory_region_is_ram(MiB, MiB));
	reported_status = SilPass;
	reported_count = 0;
	assert_false(opensil_memory_region_is_ram(MiB, MiB));
	reported_count = 19;
	assert_false(opensil_memory_region_is_ram(MiB, MiB));
}

static void reject_short_record(void **unused)
{
	const size_t prefix = offsetof(APOB_SYSTEM_MEMORY_MAP_TYPE_STRUCT, ApobSystemMap.HoleInfo);

	record->ApobTypeHeader.TypeSize = prefix + sizeof(*holes) - 1;
	assert_false(opensil_memory_region_is_ram(MiB, MiB));
	record->ApobTypeHeader.TypeSize = sizeof(apob);
	assert_false(opensil_memory_region_is_ram(MiB, MiB));
	record->ApobTypeHeader.TypeSize = sizeof(*record);
	map_override = apob + prefix - 1;
	assert_false(opensil_memory_region_is_ram(MiB, MiB));
	map_override = apob + sizeof(apob) - sizeof(*holes) + 1;
	assert_false(opensil_memory_region_is_ram(MiB, MiB));
	map_override = (uint8_t *)holes + 1;
	assert_false(opensil_memory_region_is_ram(MiB, MiB));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(valid_low_memory, setup),
		cmocka_unit_test_setup(reject_reserved_holes, setup),
		cmocka_unit_test_setup(reject_invalid_bounds, setup),
		cmocka_unit_test_setup(reject_unavailable_map, setup),
		cmocka_unit_test_setup(reject_short_record, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
