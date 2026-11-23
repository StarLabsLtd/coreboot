/* SPDX-License-Identifier: GPL-2.0-only */

#include <drivers/efi/efivars.h>
#include <string.h>
#include <tests/test.h>

#define STORE_SIZE 0x80000
#define OLD_VARIABLE_SIZE 0x30000
#define NEW_VARIABLE_SIZE 0x3f000
#define HEADER_SIZE 0x60

static uint8_t flash[STORE_SIZE], original[STORE_SIZE], expected[STORE_SIZE];
static uint8_t committed[STORE_SIZE];
static uint8_t scratch[0x1000];
static struct region_device root, device;
static size_t operations, fail_at, partial;
static bool failed, stop_before_primary;
static const EFI_GUID guid = { 0x11223344, 0x5566, 0x7788, { 1, 2, 3, 4, 5, 6, 7, 8 } };

static ssize_t read_flash(const struct region_device *rdev, void *data, size_t offset, size_t size)
{
	assert_true(offset <= sizeof(flash));
	assert_true(size <= sizeof(flash) - offset);
	if (failed)
		return -1;
	memcpy(data, flash + offset, size);
	return size;
}

static ssize_t write_flash(const struct region_device *rdev, const void *data,
			   size_t offset, size_t size)
{
	assert_true(offset <= sizeof(flash));
	assert_true(size <= sizeof(flash) - offset);
	if (failed)
		return -1;
	operations++;
	size_t done = operations == fail_at ? MIN(size, partial) : size;
	for (size_t i = 0; i < done; i++) {
		assert_int_equal(flash[offset + i] & ((const uint8_t *)data)[i],
				 ((const uint8_t *)data)[i]);
		uint8_t value = ((const uint8_t *)data)[i];
		if (operations == fail_at && partial > 0x1000)
			value |= partial & 0xff;
		flash[offset + i] &= value;
	}
	if (operations == fail_at) {
		failed = true;
		return -1;
	}
	return size;
}

static ssize_t erase_flash(const struct region_device *rdev, size_t offset, size_t size)
{
	assert_true(offset <= sizeof(flash));
	assert_true(size <= sizeof(flash) - offset);
	assert_int_equal(size, 0x1000);
	assert_int_equal(offset % 0x1000, 0);
	if (failed)
		return -1;
	operations++;
	if (stop_before_primary && !offset) {
		failed = true;
		return -1;
	}
	if (operations == fail_at && partial > 0x1000) {
		for (size_t i = 0; i < size; i++)
			flash[offset + i] |= partial & 0xff;
	} else {
		memset(flash + offset, 0xff, operations == fail_at ? MIN(size, partial) : size);
	}
	if (operations == fail_at) {
		failed = true;
		return -1;
	}
	return size;
}

static const struct region_device_ops ops = {
	.readat = read_flash,
	.writeat = write_flash,
	.eraseat = erase_flash,
};

static void reset_fault(void)
{
	operations = 0;
	fail_at = 0;
	failed = false;
	stop_before_primary = false;
}

static int setup(void **unused)
{
	region_device_init(&root, &ops, 0, sizeof(flash));
	assert_int_equal(rdev_chain(&device, &root, 0, sizeof(flash)), 0);
	reset_fault();
	memset(flash, 0xff, sizeof(flash));
	assert_int_equal(efi_fv_initialize(&device, 0x10000), CB_SUCCESS);
	assert_int_equal(efi_fv_set_option(&device, &guid, "migration", "retained",
					 sizeof("retained")), CB_SUCCESS);
	/* Preserve all bytes, including old variable authentication metadata. */
	memcpy(original, flash, sizeof(flash));
	reset_fault();
	return 0;
}

static void check_migrated(void)
{
	bool authenticated;
	char value[16];
	uint32_t size = sizeof(value);
	size_t block_size;

	assert_int_equal(efi_fv_validate_store(&device, 0x1000, &authenticated), CB_SUCCESS);
	assert_true(authenticated);
	assert_int_equal(efi_fv_store_block_size(&device, 0x1000, &block_size), CB_SUCCESS);
	assert_int_equal(block_size, 0x1000);
	assert_int_equal(efi_fv_get_option(&device, &guid, "migration", value, &size), CB_SUCCESS);
	assert_string_equal(value, "retained");
	assert_memory_equal(flash + HEADER_SIZE, original + HEADER_SIZE,
			    OLD_VARIABLE_SIZE - HEADER_SIZE);
	for (size_t i = OLD_VARIABLE_SIZE; i < sizeof(flash); i++)
		assert_int_equal(flash[i], 0xff);
	reset_fault();
	assert_int_equal(efi_fv_migration_recover(&device, scratch, sizeof(scratch)), 0);
	assert_int_equal(operations, 0);
}

static void migrate_valid_store(void **unused)
{
	size_t block_size;

	assert_int_equal(efi_fv_store_block_size(&device, 0x1000, &block_size), CB_SUCCESS);
	assert_int_equal(block_size, 0x10000);
	assert_int_equal(efi_fv_migrate_4k(&device, scratch, sizeof(scratch)), 1);
	check_migrated();
}

static void recover_every_interruption(void **unused)
{
	assert_int_equal(efi_fv_migrate_4k(&device, scratch, sizeof(scratch)), 1);
	const size_t total = operations;
	memcpy(expected, flash, sizeof(flash));
	const size_t cuts[] = { 0, 1, 13, 23, 0x800, 0x1000, 0x2001, 0x2055, 0x20aa, 0x2080 };

	print_message("Checking %zu interrupted conversion cases\n", total * ARRAY_SIZE(cuts));
	for (size_t point = 1; point <= total; point++) {
		for (size_t c = 0; c < ARRAY_SIZE(cuts); c++) {
			memcpy(flash, original, sizeof(flash));
			reset_fault();
			fail_at = point;
			partial = cuts[c];
			assert_int_equal(efi_fv_migrate_4k(&device, scratch, sizeof(scratch)), -1);
			reset_fault();
			int recovered = efi_fv_migration_recover(&device, scratch, sizeof(scratch));
			assert_true(recovered >= 0);
			bool authenticated;
			if (!recovered && efi_fv_validate_store(&device, 0x1000,
							   &authenticated) != CB_SUCCESS) {
				/* Before commit the complete old primary must survive. */
				assert_true(!memcmp(flash, original, OLD_VARIABLE_SIZE));
				assert_int_equal(efi_fv_migrate_4k(&device, scratch, sizeof(scratch)), 1);
			}
			/* A cut after the last erase may already leave the final image. */
			assert_true(!memcmp(flash, expected, sizeof(flash)));
		}
	}
}

static void stage_committed_copy(void)
{
	stop_before_primary = true;
	assert_int_equal(efi_fv_migrate_4k(&device, scratch, sizeof(scratch)), -1);
	reset_fault();
	assert_true(!memcmp(flash, original, OLD_VARIABLE_SIZE));
	memcpy(committed, flash, sizeof(flash));
}

static void recover_interrupted_recovery(void **unused)
{
	stage_committed_copy();
	assert_int_equal(efi_fv_migration_recover(&device, scratch, sizeof(scratch)), 1);
	const size_t total = operations;
	memcpy(expected, flash, sizeof(flash));
	const size_t cuts[] = { 0, 13, 0x1000, 0x2001, 0x2055, 0x20aa, 0x2080 };

	print_message("Checking %zu interrupted recovery cases\n", total * ARRAY_SIZE(cuts));
	for (size_t point = 1; point <= total; point++) {
		for (size_t c = 0; c < ARRAY_SIZE(cuts); c++) {
			memcpy(flash, committed, sizeof(flash));
			reset_fault();
			fail_at = point;
			partial = cuts[c];
			assert_int_equal(efi_fv_migration_recover(&device, scratch, sizeof(scratch)), -1);
			reset_fault();
			assert_true(efi_fv_migration_recover(&device, scratch, sizeof(scratch)) >= 0);
			assert_true(!memcmp(flash, expected, sizeof(flash)));
		}
	}
}

static void reject_corrupt_snapshot(void **unused)
{
	stage_committed_copy();
	/* A CRC mismatch must be refused before any destination write or erase. */
	flash[STORE_SIZE / 2 + 0x100] ^= 1;
	assert_int_equal(efi_fv_migration_recover(&device, scratch, sizeof(scratch)), -1);
	assert_int_equal(operations, 0);
	assert_true(!memcmp(flash, original, OLD_VARIABLE_SIZE));
	memcpy(flash, committed, sizeof(flash));
	/* The record CRC follows the magic, store size and data CRC. */
	flash[STORE_SIZE - 0x1000 + 16] ^= 1;
	assert_int_equal(efi_fv_migration_recover(&device, scratch, sizeof(scratch)), -1);
	assert_int_equal(operations, 0);
	assert_true(!memcmp(flash, original, OLD_VARIABLE_SIZE));
}

static void select_geometry_from_recovery_copy(void **unused)
{
	size_t block_size;

	memcpy(flash + STORE_SIZE / 2, flash, OLD_VARIABLE_SIZE);
	memset(flash, 0xff, 0x1000);
	assert_int_equal(efi_fv_store_block_size(&device, 0x1000, &block_size), CB_SUCCESS);
	assert_int_equal(block_size, 0x10000);
	memcpy(flash, original, sizeof(flash));
	assert_int_equal(efi_fv_migrate_4k(&device, scratch, sizeof(scratch)), 1);
	memcpy(flash + STORE_SIZE / 2, flash, NEW_VARIABLE_SIZE);
	memset(flash, 0xff, 0x1000);
	assert_int_equal(efi_fv_store_block_size(&device, 0x1000, &block_size), CB_SUCCESS);
	assert_int_equal(block_size, 0x1000);
}

static void root_memory_backing_is_preserved(void **unused)
{
	struct mem_region_device mapped;
	bool authenticated;
	size_t block_size;

	mem_region_device_ro_init(&mapped, flash, sizeof(flash));
	assert_int_equal(efi_fv_validate_store(&mapped.rdev, 0x10000, &authenticated), CB_SUCCESS);
	assert_true(authenticated);
	assert_int_equal(efi_fv_store_block_size(&mapped.rdev, 0x1000, &block_size), CB_SUCCESS);
	assert_int_equal(block_size, 0x10000);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(migrate_valid_store, setup),
		cmocka_unit_test_setup(recover_every_interruption, setup),
		cmocka_unit_test_setup(recover_interrupted_recovery, setup),
		cmocka_unit_test_setup(reject_corrupt_snapshot, setup),
		cmocka_unit_test_setup(select_geometry_from_recovery_copy, setup),
		cmocka_unit_test_setup(root_memory_backing_is_preserved, setup),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
