/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <commonlib/region.h>
#include <payload_mm_interface.h>
#include <tests/test.h>

static uint8_t extended_backing[16];
static uint8_t fixed_backing[16];
static struct mem_region_device extended;
static struct mem_region_device fixed;
static struct xlate_window windows[2];
static struct xlate_region_device boot_device;

#define BIOS_FLASH_BASE 0x60

const struct region_device *boot_device_ro(void)
{
	return &boot_device.rdev;
}

static int setup(void **state)
{
	mem_region_device_ro_init(&extended, extended_backing, sizeof(extended_backing));
	mem_region_device_ro_init(&fixed, fixed_backing, sizeof(fixed_backing));
	xlate_window_init(&windows[0], &extended.rdev, BIOS_FLASH_BASE,
			  sizeof(extended_backing));
	xlate_window_init(&windows[1], &fixed.rdev,
			  BIOS_FLASH_BASE + sizeof(extended_backing),
			  sizeof(fixed_backing));
	xlate_region_device_ro_init(&boot_device, ARRAY_SIZE(windows), windows,
				    BIOS_FLASH_BASE + sizeof(extended_backing) +
					    sizeof(fixed_backing));
	return 0;
}

static void maps_extended_window(void **state)
{
	const struct region store = { .offset = BIOS_FLASH_BASE + 4, .size = 8 };
	uintptr_t mapping;

	assert_true(payload_mm_map_flash_region(&store, &mapping));
	assert_ptr_equal((void *)mapping, &extended_backing[4]);
}

static void maps_fixed_window(void **state)
{
	const struct region store = {
		.offset = BIOS_FLASH_BASE + sizeof(extended_backing) + 4,
		.size = 8,
	};
	uintptr_t mapping;

	assert_true(payload_mm_map_flash_region(&store, &mapping));
	assert_ptr_equal((void *)mapping, &fixed_backing[4]);
}

static void rejects_cross_window_region(void **state)
{
	const struct region store = {
		.offset = BIOS_FLASH_BASE + sizeof(extended_backing) - 4,
		.size = 8,
	};
	uintptr_t mapping = 0;

	assert_false(payload_mm_map_flash_region(&store, &mapping));
}

static void rejects_unmapped_region(void **state)
{
	const struct region store = { .offset = BIOS_FLASH_BASE - 1, .size = 1 };
	uintptr_t mapping = 0;

	assert_false(payload_mm_map_flash_region(&store, &mapping));
}

static void validates_flash_geometry(void **state)
{
	struct region store = { .offset = 0x30000, .size = 0x40000 };

	assert_true(payload_mm_flash_region_is_valid(&store, 0x1000000, 0x10000));
	store.offset = 0x31000;
	assert_false(payload_mm_flash_region_is_valid(&store, 0x1000000, 0x10000));
	store.offset = 0x30000;
	store.size = 0x41000;
	assert_false(payload_mm_flash_region_is_valid(&store, 0x1000000, 0x10000));
	store.size = 0x20000;
	assert_false(payload_mm_flash_region_is_valid(&store, 0x1000000, 0x10000));
	store.size = 0x40000;
	store.offset = 0xfd0000;
	assert_false(payload_mm_flash_region_is_valid(&store, 0x1000000, 0x10000));
	assert_false(payload_mm_flash_region_is_valid(&store, 0x1000000, 0));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(maps_extended_window, setup),
		cmocka_unit_test_setup(maps_fixed_window, setup),
		cmocka_unit_test_setup(rejects_cross_window_region, setup),
		cmocka_unit_test_setup(rejects_unmapped_region, setup),
		cmocka_unit_test(validates_flash_geometry),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
