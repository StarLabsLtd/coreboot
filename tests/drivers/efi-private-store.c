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

#define SECTOR_SIZE (4 * KiB)
#define TRANSFER_SIZE (64 * KiB)
#define SECTOR_FLASH_SIZE (3 * TRANSFER_SIZE)
#define COREBOOT_OFFSET (TRANSFER_SIZE + SECTOR_SIZE)
#define COREBOOT_SIZE (TRANSFER_SIZE - SECTOR_SIZE)

static uint8_t sector_flash[SECTOR_FLASH_SIZE];
static uint8_t original_flash[SECTOR_FLASH_SIZE];
static uint8_t signed_transfer[2 * TRANSFER_SIZE];
static uint8_t original_transfer[TRANSFER_SIZE];
static uint8_t sector_readback[TRANSFER_SIZE];
static struct region_device sector_device;
static uint64_t read_sectors, write_sectors, erase_sectors;
static unsigned int sector_erases;
static size_t interrupted_sector;
static bool corrupt_sector_write;

static const struct efi_capsule_range sector_regions[] = {
	{ COREBOOT_OFFSET, COREBOOT_SIZE },
	{ 0, SECTOR_SIZE },
};

static const struct efi_capsule_sector_policy sector_policy = {
	.erase_size = SECTOR_SIZE,
	.regions = sector_regions,
	.region_count = ARRAY_SIZE(sector_regions),
};

static uint64_t sector_bit(size_t offset, size_t size)
{
	assert_int_equal(size, SECTOR_SIZE);
	assert_int_equal(offset % SECTOR_SIZE, 0);
	assert_true(offset < SECTOR_FLASH_SIZE);
	return 1ULL << (offset / SECTOR_SIZE);
}

static ssize_t sector_read(const struct region_device *rdev, void *buf, size_t offset, size_t size)
{
	read_sectors |= sector_bit(offset, size);
	memcpy(buf, sector_flash + offset, size);
	return size;
}

static ssize_t sector_write(const struct region_device *rdev, const void *buf,
			    size_t offset, size_t size)
{
	write_sectors |= sector_bit(offset, size);
	for (size_t i = 0; i < size; i++)
		sector_flash[offset + i] &= ((const uint8_t *)buf)[i];
	if (corrupt_sector_write)
		sector_flash[offset] ^= 1;
	return size;
}

static ssize_t sector_erase(const struct region_device *rdev, size_t offset, size_t size)
{
	erase_sectors |= sector_bit(offset, size);
	sector_erases++;
	if (offset == interrupted_sector) {
		memset(sector_flash + offset, 0xff, size / 2);
		return -1;
	}
	memset(sector_flash + offset, 0xff, size);
	return size;
}

static int setup_capsule_sectors(void **state)
{
	static const struct region_device_ops sector_ops = {
		.readat = sector_read,
		.writeat = sector_write,
		.eraseat = sector_erase,
	};

	region_device_init(&sector_device, &sector_ops, 0, sizeof(sector_flash));
	for (size_t i = 0; i < sizeof(sector_flash); i += SECTOR_SIZE)
		memset(sector_flash + i, 0x20 + i / SECTOR_SIZE, SECTOR_SIZE);
	memcpy(original_flash, sector_flash, sizeof(sector_flash));
	for (size_t i = 0; i < TRANSFER_SIZE; i += SECTOR_SIZE)
		memset(signed_transfer + i, 0x80 + i / SECTOR_SIZE, SECTOR_SIZE);
	memcpy(original_transfer, signed_transfer, sizeof(original_transfer));
	read_sectors = write_sectors = erase_sectors = 0;
	sector_erases = 0;
	interrupted_sector = SIZE_MAX;
	corrupt_sector_write = false;
	return 0;
}

static void expect_no_sector_io(void)
{
	assert_int_equal(read_sectors, 0);
	assert_int_equal(write_sectors, 0);
	assert_int_equal(erase_sectors, 0);
	assert_memory_equal(sector_flash, original_flash, sizeof(sector_flash));
	assert_memory_equal(signed_transfer, original_transfer, TRANSFER_SIZE);
}

static void capsule_sector_programming(void **state)
{
	uint64_t selected_sectors = 0;
	const uint8_t *image = signed_transfer;
	uint8_t *readback = signed_transfer + TRANSFER_SIZE;

	assert_int_equal(efi_capsule_program_sectors(&sector_device, TRANSFER_SIZE, TRANSFER_SIZE,
			 sector_regions, ARRAY_SIZE(sector_regions), image, readback, &sector_policy, true),
			 0);
	expect_no_sector_io();

	assert_int_equal(efi_capsule_program_sectors(&sector_device, TRANSFER_SIZE, TRANSFER_SIZE,
			 sector_regions, ARRAY_SIZE(sector_regions), image, readback, &sector_policy, false),
			 0);
	for (size_t offset = COREBOOT_OFFSET; offset < 2 * TRANSFER_SIZE; offset += SECTOR_SIZE)
		selected_sectors |= sector_bit(offset, SECTOR_SIZE);
	assert_int_equal(erase_sectors, selected_sectors);
	assert_int_equal(write_sectors, selected_sectors);
	assert_int_equal(read_sectors, selected_sectors);
	assert_int_equal(sector_erases, COREBOOT_SIZE / SECTOR_SIZE);
	/* The FMAP sector is in the same signed transfer as COREBOOT. */
	assert_memory_equal(sector_flash, original_flash, COREBOOT_OFFSET);
	assert_memory_equal(sector_flash + COREBOOT_OFFSET, original_transfer + SECTOR_SIZE,
			    COREBOOT_SIZE);
	assert_memory_equal(sector_flash + 2 * TRANSFER_SIZE, original_flash + 2 * TRANSFER_SIZE,
			    TRANSFER_SIZE);
	assert_memory_equal(image, original_transfer, TRANSFER_SIZE);
}

static void capsule_programs_partial_sectors(void **state)
{
	const struct efi_capsule_range regions[] = {
		{ COREBOOT_OFFSET + 64, SECTOR_SIZE - 128 },
	};
	const struct efi_capsule_sector_policy policy = {
		.erase_size = SECTOR_SIZE, .regions = regions, .region_count = ARRAY_SIZE(regions),
		.allow_partial_sectors = true,
	};

	assert_int_equal(efi_capsule_program_sectors(&sector_device, TRANSFER_SIZE, TRANSFER_SIZE,
			 regions, ARRAY_SIZE(regions), signed_transfer, sector_readback,
			 &policy, false), 0);
	assert_int_equal(sector_erases, 1);
	assert_memory_equal(sector_flash, original_flash, regions[0].offset);
	assert_memory_equal(sector_flash + regions[0].offset,
			    original_transfer + regions[0].offset - TRANSFER_SIZE, regions[0].size);
	const size_t end = regions[0].offset + regions[0].size;

	assert_memory_equal(sector_flash + end, original_flash + end, sizeof(sector_flash) - end);
}

static void capsule_interrupted_sector_erase(void **state)
{
	const size_t failed = COREBOOT_OFFSET + SECTOR_SIZE;

	interrupted_sector = failed;
	assert_int_equal(efi_capsule_program_sectors(&sector_device, TRANSFER_SIZE, TRANSFER_SIZE,
			 sector_regions, 1, signed_transfer, sector_readback, &sector_policy, false), -1);
	assert_int_equal(erase_sectors, sector_bit(COREBOOT_OFFSET, SECTOR_SIZE) |
				      sector_bit(failed, SECTOR_SIZE));
	assert_int_equal(write_sectors, sector_bit(COREBOOT_OFFSET, SECTOR_SIZE));
	assert_int_equal(sector_erases, 5);
	assert_int_equal(sector_flash[failed], 0xff);
	assert_int_equal(sector_flash[failed + SECTOR_SIZE / 2], original_flash[failed]);
	assert_memory_equal(sector_flash, original_flash, COREBOOT_OFFSET);
	assert_memory_equal(sector_flash + failed + SECTOR_SIZE,
			    original_flash + failed + SECTOR_SIZE,
			    sizeof(sector_flash) - failed - SECTOR_SIZE);
	assert_memory_equal(signed_transfer, original_transfer, TRANSFER_SIZE);
}

static void capsule_failed_readback_keeps_image(void **state)
{
	corrupt_sector_write = true;
	assert_int_equal(efi_capsule_program_sectors(&sector_device, TRANSFER_SIZE, TRANSFER_SIZE,
			 sector_regions, 1, signed_transfer, sector_readback, &sector_policy, false), -1);
	assert_int_equal(sector_erases, 4);
	assert_int_equal(erase_sectors, sector_bit(COREBOOT_OFFSET, SECTOR_SIZE));
	assert_memory_equal(sector_flash, original_flash, COREBOOT_OFFSET);
	assert_memory_equal(signed_transfer, original_transfer, TRANSFER_SIZE);
}

static void capsule_sector_rejects_unapproved_plan(void **state)
{
	const struct efi_capsule_range invalid[] = {
		{ 0, SECTOR_FLASH_SIZE },
		{ TRANSFER_SIZE, 2 * TRANSFER_SIZE },
		{ COREBOOT_OFFSET, COREBOOT_SIZE - SECTOR_SIZE },
		{ 2 * TRANSFER_SIZE, SECTOR_SIZE },
		{ SIZE_MAX, SECTOR_SIZE },
	};
	struct efi_capsule_range plan[] = { sector_regions[0], sector_regions[1] };

	/* Even a bad last range outside this transfer must invalidate the entire plan. */
	for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
		plan[1] = invalid[i];
		for (int check = 0; check <= 1; check++)
			assert_int_equal(efi_capsule_program_sectors(&sector_device, TRANSFER_SIZE,
				 TRANSFER_SIZE, plan, ARRAY_SIZE(plan), signed_transfer,
				 sector_readback, &sector_policy, check), -1);
	}
	expect_no_sector_io();
}

static void capsule_sector_rejects_invalid_geometry(void **state)
{
	struct efi_capsule_range approved[] = { sector_regions[0], sector_regions[1] };
	struct efi_capsule_sector_policy policy = sector_policy;
	struct region_device unaligned;
	const struct efi_capsule_range invalid[] = {
		{ 1, SECTOR_SIZE }, { 0, SECTOR_SIZE - 1 }, { 0, SECTOR_FLASH_SIZE },
		{ SECTOR_FLASH_SIZE, SECTOR_SIZE }, { SECTOR_SIZE, SIZE_MAX },
	};

	policy.regions = approved;
	for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
		approved[1] = invalid[i];
		assert_int_equal(efi_capsule_program_sectors(&sector_device, TRANSFER_SIZE,
			 TRANSFER_SIZE, sector_regions, 1, signed_transfer, sector_readback,
			 &policy, true), -1);
	}
	assert_int_equal(rdev_chain(&unaligned, &sector_device, 1, 2 * TRANSFER_SIZE), 0);
	assert_int_equal(efi_capsule_program_sectors(&unaligned, TRANSFER_SIZE, TRANSFER_SIZE,
			 sector_regions, 1, signed_transfer, sector_readback, &sector_policy, true), -1);
	assert_int_equal(efi_capsule_program_sectors(&sector_device, SIZE_MAX, TRANSFER_SIZE,
			 sector_regions, 1, signed_transfer, sector_readback, &sector_policy, true), -1);
	assert_int_equal(efi_capsule_program_sectors(&sector_device, 2 * TRANSFER_SIZE, TRANSFER_SIZE,
			 sector_regions, 1, signed_transfer, sector_readback, &sector_policy, true), -1);
	assert_int_equal(efi_capsule_program_sectors(&sector_device, COREBOOT_OFFSET, SECTOR_SIZE,
			 sector_regions, 1, signed_transfer, sector_readback, &sector_policy, true), -1);
	expect_no_sector_io();
}

static void capsule_sector_rejects_partial_tail(void **state)
{
	const struct efi_capsule_range approved[] = {
		sector_regions[0], { 2 * TRANSFER_SIZE, SECTOR_SIZE },
	};
	const struct efi_capsule_sector_policy policy = {
		.erase_size = SECTOR_SIZE, .regions = approved, .region_count = ARRAY_SIZE(approved),
	};
	struct region_device partial;

	assert_int_equal(rdev_chain(&partial, &sector_device, 0,
			 2 * TRANSFER_SIZE + SECTOR_SIZE), 0);
	for (int check = 0; check <= 1; check++)
		assert_int_equal(efi_capsule_program_sectors(&partial, TRANSFER_SIZE, TRANSFER_SIZE,
			 approved, ARRAY_SIZE(approved), signed_transfer, sector_readback,
			 &policy, check), -1);
	expect_no_sector_io();
}

static void capsule_sector_rejects_bad_buffers(void **state)
{
	/* Overlap lies beyond the two-sector scratch area actually used. */
	assert_int_equal(efi_capsule_program_sectors(&sector_device, TRANSFER_SIZE, TRANSFER_SIZE,
			 sector_regions, 1, signed_transfer, signed_transfer + TRANSFER_SIZE - 1,
			 &sector_policy, true), -1);
	assert_int_equal(efi_capsule_program_sectors(&sector_device, TRANSFER_SIZE, TRANSFER_SIZE,
			 sector_regions, 1, (const void *)(UINTPTR_MAX - 7), sector_readback,
			 &sector_policy, false), -1);
	assert_int_equal(efi_capsule_program_sectors(&sector_device, TRANSFER_SIZE, TRANSFER_SIZE,
			 sector_regions, 1, signed_transfer, (void *)(UINTPTR_MAX - 7),
			 &sector_policy, false), -1);
	assert_int_equal(efi_capsule_program_sectors(&sector_device, TRANSFER_SIZE, TRANSFER_SIZE,
			 (const void *)sector_readback, 1, signed_transfer, sector_readback,
			 &sector_policy, false), -1);
	expect_no_sector_io();
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
		cmocka_unit_test_setup(capsule_sector_programming, setup_capsule_sectors),
		cmocka_unit_test_setup(capsule_programs_partial_sectors, setup_capsule_sectors),
		cmocka_unit_test_setup(capsule_interrupted_sector_erase, setup_capsule_sectors),
		cmocka_unit_test_setup(capsule_failed_readback_keeps_image, setup_capsule_sectors),
		cmocka_unit_test_setup(capsule_sector_rejects_unapproved_plan, setup_capsule_sectors),
		cmocka_unit_test_setup(capsule_sector_rejects_invalid_geometry, setup_capsule_sectors),
		cmocka_unit_test_setup(capsule_sector_rejects_bad_buffers, setup_capsule_sectors),
		cmocka_unit_test_setup(capsule_sector_rejects_partial_tail, setup_capsule_sectors),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
