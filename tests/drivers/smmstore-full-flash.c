/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <commonlib/region.h>
#include <cpu/x86/smm.h>
#include <fmap.h>
#include <fmap_config.h>
#include <smmstore.h>
#include <string.h>
#include <tests/test.h>

static uint8_t com_buffer[SMM_BLOCK_SIZE];
static size_t read_offset;
static unsigned int mapped_reads, command_reads;
static bool missing_backend;

static ssize_t mapped_read(const struct region_device *rd, void *buffer,
			   size_t offset, size_t size)
{
	mapped_reads++;
	read_offset = offset;
	memset(buffer, 0x11, size);
	return size;
}

static ssize_t command_read(const struct region_device *rd, void *buffer,
			    size_t offset, size_t size)
{
	command_reads++;
	read_offset = offset;
	memset(buffer, 0x22, size);
	return size;
}

static const struct region_device_ops mapped_ops = { .readat = mapped_read };
static const struct region_device_ops command_ops = { .readat = command_read };
static const struct region_device mapped = REGION_DEV_INIT(&mapped_ops, 0, 0x1000000);
static const struct region_device command =
	REGION_DEV_INIT(&command_ops, 0, FMAP_SECTION_FLASH_SIZE);

const struct region_device *boot_device_ro(void)
{
	return &mapped;
}

const struct region_device *boot_device_rw(void)
{
	return missing_backend ? NULL : &command;
}

int fmap_locate_area(const char *name, struct region *region)
{
	assert_string_equal(name, "SMMSTORE");
	region->offset = FMAP_SECTION_SMMSTORE_START;
	region->size = FMAP_SECTION_SMMSTORE_SIZE;
	return 0;
}

bool smm_region_overlaps_handler(const struct region *region)
{
	return false;
}

static void full_flash_read(void **state)
{
	uint8_t cmd = SMMSTORE_CMD_USE_FULL_FLASH | SMMSTORE_CMD_RAW_READ;

	assert_int_equal(smmstore_init(com_buffer, sizeof(com_buffer)), 0);
	/* A full-flash modifier without the boot latch is not accepted. */
	assert_int_equal(smmstore_preprocess_cmd(&cmd, NULL), 0);
	assert_int_equal(cmd, SMMSTORE_CMD_USE_FULL_FLASH | SMMSTORE_CMD_RAW_READ);
	assert_int_equal(smmstore_rawread_region(128, 0, 16), -1);
	assert_int_equal(command_reads, 0);

	cmd = SMMSTORE_CMD_RAW_READ;
	smmstore_preprocess_cmd(&cmd, NULL);
	assert_int_equal(smmstore_rawread_region(0, 0, 16), 0);
	assert_int_equal(read_offset, 0x760000);
	assert_int_equal(com_buffer[0], 0x11);
	assert_int_equal(mapped_reads, 1);

	cmd = SMMSTORE_CMD_USE_FULL_FLASH;
	assert_int_equal(smmstore_preprocess_cmd(&cmd, (void *)1), 1);
	cmd = SMMSTORE_CMD_USE_FULL_FLASH | SMMSTORE_CMD_RAW_READ;
	smmstore_preprocess_cmd(&cmd, NULL);
	assert_int_equal(cmd, SMMSTORE_CMD_RAW_READ);
	/* Even reads below 16 MiB must not chain the full flash through the mapping. */
	assert_int_equal(smmstore_rawread_region(128, 0, 16), 0);
	assert_int_equal(read_offset, 0x800000);
	assert_int_equal(com_buffer[0], 0x22);
	/* The command backend also covers the upper half of the chip. */
	assert_int_equal(smmstore_rawread_region(256, 0x10, 16), 0);
	assert_int_equal(read_offset, 0x1000010);
	assert_int_equal(command_reads, 2);
	assert_int_equal(mapped_reads, 1);
	assert_int_equal(smmstore_rawread_region(512, 0, 16), -1);
	assert_int_equal(smmstore_rawread_region(128, 0xffff, 2), -1);
	missing_backend = true;
	assert_int_equal(smmstore_rawread_region(128, 0, 16), -1);
	missing_backend = false;

	cmd = SMMSTORE_CMD_RAW_READ;
	smmstore_preprocess_cmd(&cmd, NULL);
	assert_int_equal(smmstore_rawread_region(0, 0, 16), 0);
	assert_int_equal(com_buffer[0], 0x11);
	assert_int_equal(mapped_reads, 2);
	assert_int_equal(command_reads, 2);
}

int main(void)
{
	const struct CMUnitTest tests[] = { cmocka_unit_test(full_flash_read) };
	return cb_run_group_tests(tests, NULL, NULL);
}
