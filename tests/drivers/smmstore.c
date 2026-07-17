/* SPDX-License-Identifier: GPL-2.0-only */

#include "../../src/drivers/smmstore/store.c"

#include <string.h>
#include <tests/test.h>

#define MOCK_FLASH_SIZE (5 * MiB)

static struct region mock_ec_region;
static int mock_fmap_result;
static uint8_t mock_flash[MOCK_FLASH_SIZE];
static struct region_device mock_boot_device;

int fmap_locate_area(const char *name, struct region *region)
{
	assert_string_equal(name, "EC");
	*region = mock_ec_region;
	return mock_fmap_result;
}

const struct region_device *boot_device_rw(void)
{
	return &mock_boot_device;
}

int boot_device_ro_subregion(const struct region *sub, struct region_device *subrd)
{
	return rdev_chain(subrd, &mock_boot_device, region_offset(sub), region_sz(sub));
}

int boot_device_rw_subregion(const struct region *sub, struct region_device *subrd)
{
	return rdev_chain(subrd, &mock_boot_device, region_offset(sub), region_sz(sub));
}

static int setup(void **state)
{
	mock_ec_region = region_create(0x400000, 128 * KiB);
	mock_fmap_result = 0;
	memset(mock_flash, 0xff, sizeof(mock_flash));
	rdev_chain_mem_rw(&mock_boot_device, mock_flash, sizeof(mock_flash));
	smmstore_region_mode = SMMSTORE_REGION_DEFAULT;
	has_capsules = -1;
	return 0;
}

static void test_capsule_region_command(void **state)
{
	uint8_t command = SMMSTORE_CMD_USE_FULL_FLASH;

	assert_int_equal(smmstore_preprocess_cmd(&command, (void *)1), 1);
	assert_int_equal(command, SMMSTORE_CMD_USE_FULL_FLASH);

	command = SMMSTORE_CMD_USE_EC_REGION | SMMSTORE_CMD_RAW_WRITE;
	assert_int_equal(smmstore_preprocess_cmd(&command, NULL), 0);
	assert_int_equal(command, SMMSTORE_CMD_RAW_WRITE);
	assert_int_equal(smmstore_region_mode, SMMSTORE_REGION_EC);
}

static void test_combined_region_modifiers_rejected(void **state)
{
	uint8_t command = SMMSTORE_CMD_USE_FULL_FLASH;

	assert_int_equal(smmstore_preprocess_cmd(&command, (void *)1), 1);
	command = SMMSTORE_CMD_REGION_MASK | SMMSTORE_CMD_RAW_CLEAR;
	assert_int_equal(smmstore_preprocess_cmd(&command, NULL), 0);
	assert_int_equal(command, SMMSTORE_CMD_REGION_MASK | SMMSTORE_CMD_RAW_CLEAR);
	assert_int_equal(smmstore_region_mode, SMMSTORE_REGION_DEFAULT);
}

static void test_disabled_capsules_ignore_region_modifier(void **state)
{
	uint8_t command = SMMSTORE_CMD_USE_FULL_FLASH;

	assert_int_equal(smmstore_preprocess_cmd(&command, NULL), 0);
	command = SMMSTORE_CMD_USE_EC_REGION | SMMSTORE_CMD_RAW_READ;
	assert_int_equal(smmstore_preprocess_cmd(&command, NULL), 0);
	assert_int_equal(command, SMMSTORE_CMD_USE_EC_REGION | SMMSTORE_CMD_RAW_READ);
	assert_int_equal(smmstore_region_mode, SMMSTORE_REGION_DEFAULT);
}

static void test_ec_region_is_limited_to_update_window(void **state)
{
	struct region region;

	assert_int_equal(lookup_store_region(&region, SMMSTORE_REGION_EC), CB_SUCCESS);
	assert_int_equal(region_offset(&region), 0x400000);
	assert_int_equal(region_sz(&region), 64 * KiB);
}

static void test_ec_region_rejects_bad_geometry(void **state)
{
	struct region region;

	mock_ec_region = region_create(0x400001, 128 * KiB);
	assert_int_equal(lookup_store_region(&region, SMMSTORE_REGION_EC), CB_ERR);

	mock_ec_region = region_create(0x400000, (64 * KiB) - 1);
	assert_int_equal(lookup_store_region(&region, SMMSTORE_REGION_EC), CB_ERR);

	mock_fmap_result = -1;
	assert_int_equal(lookup_store_region(&region, SMMSTORE_REGION_EC), CB_ERR);
}

static void test_raw_lookup_tracks_fmap_relocation(void **state)
{
	struct region_device store;
	uint8_t first = 0x11;
	uint8_t second = 0x22;

	assert_int_equal(lookup_store(&store, SMMSTORE_REGION_EC), 0);
	assert_int_equal(rdev_writeat(&store, &first, 0, sizeof(first)), sizeof(first));

	mock_ec_region = region_create(0x440000, 128 * KiB);
	assert_int_equal(lookup_store(&store, SMMSTORE_REGION_EC), 0);
	assert_int_equal(rdev_writeat(&store, &second, 0, sizeof(second)), sizeof(second));

	assert_int_equal(mock_flash[0x400000], first);
	assert_int_equal(mock_flash[0x440000], second);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_capsule_region_command, setup),
		cmocka_unit_test_setup(test_combined_region_modifiers_rejected, setup),
		cmocka_unit_test_setup(test_disabled_capsules_ignore_region_modifier, setup),
		cmocka_unit_test_setup(test_ec_region_is_limited_to_update_window, setup),
		cmocka_unit_test_setup(test_ec_region_rejects_bad_geometry, setup),
		cmocka_unit_test_setup(test_raw_lookup_tracks_fmap_relocation, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
