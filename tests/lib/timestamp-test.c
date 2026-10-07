/* SPDX-License-Identifier: GPL-2.0-only */

#include "../lib/timestamp.c"
#include <commonlib/bsd/helpers.h>
#include <string.h>
#include <tests/test.h>
#include "stubs/timestamp.h"

/* Timestamp region definition */
#define TIMESTAMP_REGION_SIZE (1 * KiB)
TEST_REGION(timestamp, TIMESTAMP_REGION_SIZE);

/* Model the CBMEM allocation boundary, not the CBMEM allocator internals. */
void *cbmem_add(uint32_t id, uint64_t size)
{
	check_expected(id);
	check_expected(size);
	return mock_ptr_type(void *);
}

#if CONFIG(COLLECT_TIMESTAMPS)
#define TIMESTAMP_ALLOCATION_SIZE (sizeof(struct timestamp_table) + \
	CONFIG_TIMESTAMP_ENTRIES * sizeof(struct timestamp_entry))
static uint8_t timestamp_cbmem[TIMESTAMP_ALLOCATION_SIZE + 8] __aligned(8);

static void test_timestamp_cbmem_allocation(void **state)
{
	const uint8_t canary[8] = { [0 ... 7] = 0xa5 };
	struct timestamp_table *table;

	memset(timestamp_cbmem, 0xa5, sizeof(timestamp_cbmem));
	expect_value(cbmem_add, id, CBMEM_ID_TIMESTAMP);
	expect_value(cbmem_add, size, 16 + 12 * CONFIG_TIMESTAMP_ENTRIES);
	will_return(cbmem_add, timestamp_cbmem);
	table = timestamp_alloc_cbmem_table();

	assert_ptr_equal(table, timestamp_cbmem);
	assert_int_equal(table->base_time, 0);
	assert_int_equal(table->max_entries, CONFIG_TIMESTAMP_ENTRIES);
	assert_int_equal(table->num_entries, 0);
	assert_int_equal(table->tick_freq_mhz, 0xa5a5);
	assert_memory_equal(&table->entries[0], canary, sizeof(canary));
	assert_memory_equal(timestamp_cbmem + TIMESTAMP_ALLOCATION_SIZE,
		canary, sizeof(canary));
}

static void test_timestamp_cbmem_allocation_failure(void **state)
{
	uint8_t before[sizeof(timestamp_cbmem)];
	struct timestamp_table *previous = glob_ts_table;

	memset(timestamp_cbmem, 0xa5, sizeof(timestamp_cbmem));
	memcpy(before, timestamp_cbmem, sizeof(before));
	expect_value(cbmem_add, id, CBMEM_ID_TIMESTAMP);
	expect_value(cbmem_add, size, 16 + 12 * CONFIG_TIMESTAMP_ENTRIES);
	will_return(cbmem_add, (void *)NULL);

	assert_null(timestamp_alloc_cbmem_table());
	assert_ptr_equal(glob_ts_table, previous);
	assert_memory_equal(timestamp_cbmem, before, sizeof(before));
}

static void test_timestamp_cbmem_full(void **state)
{
	uint8_t before[sizeof(timestamp_cbmem)];
	struct timestamp_table *table;
	struct timestamp_entry *last;

	memset(timestamp_cbmem, 0xa5, sizeof(timestamp_cbmem));
	expect_value(cbmem_add, id, CBMEM_ID_TIMESTAMP);
	expect_value(cbmem_add, size, 16 + 12 * CONFIG_TIMESTAMP_ENTRIES);
	will_return(cbmem_add, timestamp_cbmem);
	table = timestamp_alloc_cbmem_table();
	table->num_entries = CONFIG_TIMESTAMP_ENTRIES - 1;
	memcpy(before, timestamp_cbmem, sizeof(before));
	last = &table->entries[CONFIG_TIMESTAMP_ENTRIES - 1];

	timestamp_add_table_entry(table, TS_ROMSTAGE_END, 12345);
	assert_int_equal(table->num_entries, CONFIG_TIMESTAMP_ENTRIES);
	assert_int_equal(last->entry_id, TS_ROMSTAGE_END);
	assert_int_equal(last->entry_stamp, 12345);
	assert_memory_equal(table->entries,
		before + sizeof(struct timestamp_table),
		(CONFIG_TIMESTAMP_ENTRIES - 1) * sizeof(struct timestamp_entry));
	assert_memory_equal(timestamp_cbmem + TIMESTAMP_ALLOCATION_SIZE,
		before + TIMESTAMP_ALLOCATION_SIZE, 8);

	memcpy(before, timestamp_cbmem, sizeof(before));
	timestamp_add_table_entry(table, TS_SELFBOOT_JUMP, 54321);
	assert_memory_equal(timestamp_cbmem, before, sizeof(before));
	table->num_entries = CONFIG_TIMESTAMP_ENTRIES + 1;
	memcpy(before, timestamp_cbmem, sizeof(before));
	timestamp_add_table_entry(table, TS_SELFBOOT_JUMP, 54321);
	assert_memory_equal(timestamp_cbmem, before, sizeof(before));
	table->num_entries = UINT32_MAX;
	memcpy(before, timestamp_cbmem, sizeof(before));
	timestamp_add_table_entry(table, TS_SELFBOOT_JUMP, 54321);
	assert_memory_equal(timestamp_cbmem, before, sizeof(before));
}
#endif

void test_timestamp_init(void **state)
{
	timestamp_init(1000);

	assert_non_null(glob_ts_table);
}

void test_timestamp_add(void **state)
{
	const int base_multipler = 2000;
	const int timestamp_base = 1000;
	struct timestamp_entry *entry;
	int i;

	timestamp_init(timestamp_base);

	timestamp_add(TS_ROMSTAGE_START, base_multipler);

	assert_int_equal(1, glob_ts_table->num_entries);

	entry = &glob_ts_table->entries[0];
	assert_int_equal(1, entry->entry_id);
	assert_int_equal(base_multipler - timestamp_base, /* Added timestamp reduced by base */
			 entry->entry_stamp);

	/* Add few timestamps to check if all of them will be added properly */
	for (i = 1; i < 10; ++i)
		timestamp_add(i + 1, base_multipler * (i + 1));

	assert_int_equal(10, glob_ts_table->num_entries);

	for (i = 0; i < 10; ++i) {
		entry = &glob_ts_table->entries[i];
		assert_int_equal(i + 1, entry->entry_id);
		assert_int_equal(base_multipler * (i + 1) - timestamp_base, entry->entry_stamp);
	}
}

void test_timestamp_add_now(void **state)
{
	const int base_multipler = 2000;
	const int timestamp_base = 1000;
	struct timestamp_entry *entry;

	/* Initialize with base timestamp of 1000.
	 * This value will be subtracted from each timestamp
	 * when adding it.
	 */
	timestamp_init(timestamp_base);

	dummy_timestamp_set(base_multipler);

	timestamp_add_now(TS_ROMSTAGE_START);

	assert_int_equal(1, glob_ts_table->num_entries);

	entry = &glob_ts_table->entries[0];

	assert_int_equal(1, entry->entry_id);
	assert_int_equal(base_multipler - timestamp_base, /* Added timestamp reduced by base */
			 entry->entry_stamp);
}

void test_timestamp_rescale_table(void **state)
{
	const int base_multipler = 1000;
	int i;

	timestamp_init(0);

	/* Add few timestamps to check if all of them will be rescaled properly */
	for (i = 1; i <= 10; ++i)
		timestamp_add(i, base_multipler * i);

	/* Check if all entries were added to table */
	assert_int_equal(10, glob_ts_table->num_entries);

	timestamp_rescale_table(2, 4);

	/* Check if there is the same number of entries */
	assert_int_equal(10, glob_ts_table->num_entries);

	for (i = 0; i < glob_ts_table->num_entries; ++i)
		assert_int_equal(base_multipler * (i + 1) / 4 * 2,
				 glob_ts_table->entries[i].entry_stamp);
}

void test_get_us_since_boot(void **state)
{
	const int base_multipler = 10000;
	const int timestamp_base = 1000;
	const int freq_base = 100;

	timestamp_init(timestamp_base);
	dummy_timestamp_set(base_multipler);
	dummy_timestamp_tick_freq_mhz_set(freq_base);
	/* There is a need to update this field manually, because cbmem hooks are not used. */
	glob_ts_table->tick_freq_mhz = freq_base;

	assert_int_equal((base_multipler - timestamp_base) / freq_base, get_us_since_boot());
}

int setup_timestamp_and_freq(void **state)
{
	dummy_timestamp_set(0);
	dummy_timestamp_tick_freq_mhz_set(1);

	return 0;
}

int main(void)
{
	const struct CMUnitTest tests[] = {
#if CONFIG(COLLECT_TIMESTAMPS)
		cmocka_unit_test_setup(test_timestamp_cbmem_allocation, setup_timestamp_and_freq),
		cmocka_unit_test_setup(test_timestamp_cbmem_allocation_failure,
			setup_timestamp_and_freq),
		cmocka_unit_test_setup(test_timestamp_cbmem_full, setup_timestamp_and_freq),
#endif
		cmocka_unit_test_setup(test_timestamp_init, setup_timestamp_and_freq),
		cmocka_unit_test_setup(test_timestamp_add, setup_timestamp_and_freq),
		cmocka_unit_test_setup(test_timestamp_add_now, setup_timestamp_and_freq),
		cmocka_unit_test_setup(test_timestamp_rescale_table, setup_timestamp_and_freq),
		cmocka_unit_test_setup(test_get_us_since_boot, setup_timestamp_and_freq),
	};

#if CONFIG(COLLECT_TIMESTAMPS)
	return cb_run_group_tests(tests, NULL, NULL);
#else
	return 0;
#endif
}
