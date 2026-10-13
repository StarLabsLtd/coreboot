/* SPDX-License-Identifier: GPL-2.0-only */

#include <drivers/efi_variable_service/private_store.h>
#include <drivers/efi_variable_service/capsule_store.h>
#include <string.h>
#include <tests/test.h>

#define BLOCK_SIZE 64

static uint8_t flash[BLOCK_SIZE * 4];
static uint8_t private_buffer[BLOCK_SIZE * 2];
static struct region_device device;
static struct efi_private_store store;
static unsigned int reads, writes, erases;
static bool short_io;
static bool failed_io;
static bool failed_write, corrupt_write;
static unsigned int transient_writes;

static ssize_t flash_read(const struct region_device *rdev, void *buffer, size_t offset,
			  size_t size)
{
	reads++;
	if (failed_io)
		return -1;
	if (short_io)
		size--;
	memcpy(buffer, flash + offset, size);
	return size;
}

static ssize_t flash_write(const struct region_device *rdev, const void *buffer, size_t offset,
			   size_t size)
{
	writes++;
	if (transient_writes) {
		transient_writes--;
		return -1;
	}
	if (failed_io || failed_write)
		return -1;
	if (short_io)
		size--;
	/* NOR programming cannot restore cleared bits without an erase. */
	for (size_t i = 0; i < size; i++)
		flash[offset + i] &= ((const uint8_t *)buffer)[i];
	if (corrupt_write)
		flash[offset] ^= 1;
	return size;
}

static ssize_t flash_erase(const struct region_device *rdev, size_t offset, size_t size)
{
	erases++;
	if (failed_io)
		return -1;
	if (short_io)
		size--;
	memset(flash + offset, 0xff, size);
	return size;
}

static const struct region_device_ops ops = {
	.readat = flash_read,
	.writeat = flash_write,
	.eraseat = flash_erase,
};

static int setup(void **state)
{
	region_device_init(&device, &ops, 0, sizeof(flash));
	memset(flash, 0x33, sizeof(flash));
	memset(private_buffer, 0x77, sizeof(private_buffer));
	reads = writes = erases = 0;
	short_io = false;
	failed_io = false;
	failed_write = corrupt_write = false;
	transient_writes = 0;
	return efi_private_store_init(&store, &device, BLOCK_SIZE,
				      (uintptr_t)private_buffer, sizeof(private_buffer));
}

static void valid_io(void **state)
{
	assert_int_equal(efi_private_store_read(&store, 5, private_buffer, 4), 0);
	assert_memory_equal(private_buffer, flash + 5, 4);
	private_buffer[0] = 0x42;
	flash[BLOCK_SIZE + 1] = 0xff;
	assert_int_equal(efi_private_store_write(&store, BLOCK_SIZE + 1, private_buffer, 1), 0);
	assert_int_equal(flash[BLOCK_SIZE + 1], 0x42);
	private_buffer[0] = 0xff;
	assert_int_equal(efi_private_store_write(&store, BLOCK_SIZE + 1, private_buffer, 1), 0);
	assert_int_equal(flash[BLOCK_SIZE + 1], 0x42);
	assert_int_equal(efi_private_store_erase(&store, 2 * BLOCK_SIZE, BLOCK_SIZE), 0);
	assert_int_equal(flash[2 * BLOCK_SIZE], 0xff);
	assert_int_equal(flash[3 * BLOCK_SIZE - 1], 0xff);
	assert_int_equal(flash[3 * BLOCK_SIZE], 0x33);
}

static void invalid_io(void **state)
{
	uint8_t outside[4];

	assert_int_not_equal(efi_private_store_read(&store, SIZE_MAX, private_buffer, 1), 0);
	assert_int_not_equal(efi_private_store_read(&store, 0, outside, 1), 0);
	assert_int_not_equal(efi_private_store_read(&store, 0, private_buffer + sizeof(private_buffer) - 1, 2), 0);
	assert_int_not_equal(efi_private_store_read(&store, BLOCK_SIZE - 1, private_buffer, 2), 0);
	assert_int_not_equal(efi_private_store_write(&store, sizeof(flash), private_buffer, 1), 0);
	assert_int_not_equal(efi_private_store_write(&store, 0, private_buffer, 0), 0);
	assert_int_not_equal(efi_private_store_erase(&store, 1, BLOCK_SIZE), 0);
	assert_int_not_equal(efi_private_store_erase(&store, 0, 2 * BLOCK_SIZE), 0);
	assert_int_not_equal(efi_private_store_erase(&store, sizeof(flash), BLOCK_SIZE), 0);
	assert_int_equal(reads, 0);
	assert_int_equal(writes, 0);
	assert_int_equal(erases, 0);
}

static void short_transfers(void **state)
{
	short_io = true;
	assert_int_not_equal(efi_private_store_read(&store, 0, private_buffer, 4), 0);
	assert_int_not_equal(efi_private_store_write(&store, 0, private_buffer, 4), 0);
	assert_int_not_equal(efi_private_store_erase(&store, 0, BLOCK_SIZE), 0);
}

static void bad_initialization(void **state)
{
	assert_int_not_equal(efi_private_store_init(&store, &device, 0,
						  (uintptr_t)private_buffer, sizeof(private_buffer)), 0);
	assert_int_not_equal(efi_private_store_write(&store, 0, private_buffer, 1), 0);
	assert_int_not_equal(efi_private_store_init(&store, &device, BLOCK_SIZE,
						  UINTPTR_MAX - 1, 4), 0);
	assert_int_not_equal(efi_private_store_init(&store, &device, 3,
						  (uintptr_t)private_buffer, sizeof(private_buffer)), 0);
}

static void failed_transfers(void **state)
{
	uint8_t original[sizeof(flash)];
	uint8_t original_buffer[sizeof(private_buffer)];

	memcpy(original, flash, sizeof(original));
	memcpy(original_buffer, private_buffer, sizeof(original_buffer));
	failed_io = true;
	assert_int_not_equal(efi_private_store_read(&store, 0, private_buffer, 4), 0);
	assert_memory_equal(private_buffer, original_buffer, sizeof(private_buffer));
	assert_int_not_equal(efi_private_store_write(&store, 0, private_buffer, 4), 0);
	assert_int_not_equal(efi_private_store_erase(&store, 0, BLOCK_SIZE), 0);
	assert_memory_equal(flash, original, sizeof(flash));
}

static int program_selected_range(const struct region_device *flash, size_t offset,
				  size_t block_size, size_t selected, size_t size,
				  uint8_t *image, uint8_t *readback, bool allow_erase)
{
	const struct efi_capsule_range range = { selected, size };
	return efi_capsule_program_block(flash, offset, block_size, &range, 1,
					image, readback, allow_erase);
}

static void capsule_shared_block(void **state)
{
	uint8_t image[BLOCK_SIZE], readback[BLOCK_SIZE];

	memset(image, 0x55, sizeof(image));
	assert_int_equal(program_selected_range(&device, BLOCK_SIZE, BLOCK_SIZE,
				 BLOCK_SIZE + 4, BLOCK_SIZE - 8, image, readback, true), 0);
	for (size_t i = 0; i < sizeof(flash); i++)
		assert_int_equal(flash[i], i >= BLOCK_SIZE + 4 && i < 2 * BLOCK_SIZE - 4 ?
				 0x55 : 0x33);
	assert_int_equal(erases, 1);
	assert_int_equal(writes, 1);
	assert_int_equal(program_selected_range(&device, BLOCK_SIZE, BLOCK_SIZE,
				 BLOCK_SIZE + 4, BLOCK_SIZE - 8, image, readback, true), 0);
	assert_int_equal(erases, 1);
	assert_int_equal(writes, 1);
}

static void capsule_invalid_ranges(void **state)
{
	uint8_t image[BLOCK_SIZE], readback[BLOCK_SIZE];

	assert_int_not_equal(program_selected_range(&device, 0, BLOCK_SIZE,
				 0, BLOCK_SIZE, image, image, true), 0);
	assert_int_not_equal(program_selected_range(&device, 1, BLOCK_SIZE,
				 0, BLOCK_SIZE, image, readback, true), 0);
	assert_int_not_equal(program_selected_range(&device, 0, BLOCK_SIZE,
				 BLOCK_SIZE, BLOCK_SIZE, image, readback, true), 0);
	assert_int_not_equal(program_selected_range(&device, 0, BLOCK_SIZE,
				 SIZE_MAX, BLOCK_SIZE, image, readback, true), 0);
	assert_int_equal(reads, 0);
	assert_int_equal(erases, 0);
	assert_int_equal(writes, 0);
}

static void capsule_write_errors(void **state)
{
	uint8_t image[BLOCK_SIZE], readback[BLOCK_SIZE];

	memset(image, 0x55, sizeof(image));
	failed_io = true;
	assert_int_not_equal(program_selected_range(&device, 0, BLOCK_SIZE,
				 0, BLOCK_SIZE, image, readback, true), 0);
	assert_int_equal(erases, 0);
	assert_int_equal(writes, 0);
	failed_io = false;
	failed_write = true;
	assert_int_not_equal(program_selected_range(&device, 0, BLOCK_SIZE,
				 0, BLOCK_SIZE, image, readback, true), 0);
	failed_write = false;
	corrupt_write = true;
	assert_int_not_equal(program_selected_range(&device, 0, BLOCK_SIZE,
				 0, BLOCK_SIZE, image, readback, true), 0);
}

static void capsule_region_union(void **state)
{
	uint8_t image[BLOCK_SIZE], readback[BLOCK_SIZE];
	struct efi_capsule_range ranges[] = { { 8, 8 }, { 32, 8 } };

	memset(image, 0x55, sizeof(image));
	assert_int_equal(efi_capsule_program_block(&device, 0, BLOCK_SIZE,
				 ranges, ARRAY_SIZE(ranges), image, readback, true), 0);
	for (size_t i = 0; i < sizeof(flash); i++)
		assert_int_equal(flash[i], (i >= 8 && i < 16) || (i >= 32 && i < 40) ?
				 0x55 : 0x33);
	assert_int_equal(erases, 1);
	assert_int_equal(writes, 1);
	assert_int_equal(reads, 2);
	ranges[1].offset = SIZE_MAX;
	assert_int_not_equal(efi_capsule_program_block(&device, 0, BLOCK_SIZE,
				 ranges, ARRAY_SIZE(ranges), image, readback, true), 0);
	assert_int_equal(reads, 2);
	assert_int_equal(erases, 1);
	assert_int_equal(writes, 1);
}

static void capsule_retry_preserves_neighbours(void **state)
{
	uint8_t image[BLOCK_SIZE], readback[BLOCK_SIZE];
	const struct efi_capsule_range range = { 4, BLOCK_SIZE - 8 };

	memset(image, 0x55, sizeof(image));
	transient_writes = 1;
	assert_int_equal(efi_capsule_program_block(&device, 0, BLOCK_SIZE, &range, 1,
				 image, readback, true), 0);
	assert_int_equal(erases, 2);
	assert_int_equal(writes, 2);
	for (size_t i = 0; i < sizeof(flash); i++)
		assert_int_equal(flash[i], i >= 4 && i < BLOCK_SIZE - 4 ? 0x55 : 0x33);
}

static void capsule_immutable_preflight(void **state)
{
	uint8_t image[BLOCK_SIZE], readback[BLOCK_SIZE];
	const struct efi_capsule_range range = { 0, BLOCK_SIZE };

	memset(image, 0x55, sizeof(image));
	assert_int_not_equal(efi_capsule_program_block(&device, 0, BLOCK_SIZE, &range, 1,
				 image, readback, false), 0);
	assert_int_equal(erases, 0);
	assert_int_equal(writes, 0);
	for (size_t i = 0; i < sizeof(flash); i++)
		assert_int_equal(flash[i], 0x33);
	memset(image, 0x33, sizeof(image));
	assert_int_equal(efi_capsule_program_block(&device, 0, BLOCK_SIZE, &range, 1,
				 image, readback, false), 0);
	assert_int_equal(erases, 0);
	assert_int_equal(writes, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(valid_io, setup),
		cmocka_unit_test_setup(invalid_io, setup),
		cmocka_unit_test_setup(short_transfers, setup),
		cmocka_unit_test_setup(failed_transfers, setup),
		cmocka_unit_test_setup(bad_initialization, setup),
		cmocka_unit_test_setup(capsule_shared_block, setup),
		cmocka_unit_test_setup(capsule_invalid_ranges, setup),
		cmocka_unit_test_setup(capsule_write_errors, setup),
		cmocka_unit_test_setup(capsule_region_union, setup),
		cmocka_unit_test_setup(capsule_retry_preserves_neighbours, setup),
		cmocka_unit_test_setup(capsule_immutable_preflight, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
