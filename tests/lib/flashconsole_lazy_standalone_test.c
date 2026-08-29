/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/region.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

static uint8_t flash[300];
static size_t flash_size;
static int locate_failure;
static int read_failure;
static int write_failure;
static int fail_on_write;
static int write_calls;

int printk(int level, const char *format, ...)
{
	(void)level;
	(void)format;
	return 0;
}

int fmap_locate_area_as_rdev_rw(const char *name, struct region_device *device)
{
	if (locate_failure || strcmp(name, "CONSOLE"))
		return -1;
	memset(device, 0, sizeof(*device));
	device->region.size = flash_size;
	return 0;
}

ssize_t rdev_readat(const struct region_device *device, void *buffer,
	size_t position, size_t length)
{
	(void)device;
	if (read_failure || position > flash_size || length > flash_size - position)
		return -1;
	memcpy(buffer, flash + position, length);
	return length;
}

ssize_t rdev_writeat(const struct region_device *device, const void *buffer,
	size_t position, size_t length)
{
	(void)device;
	write_calls++;
	if (write_failure || (fail_on_write && write_calls == fail_on_write) ||
	    position > flash_size || length > flash_size - position)
		return -1;
	memcpy(flash + position, buffer, length);
	return length;
}

#include "../../src/drivers/spi/flashconsole.c"

static void reset_fixture(size_t size, size_t used)
{
	memset(flash, 0xff, sizeof(flash));
	memset(flash, 'x', used);
	flash_size = size;
	locate_failure = read_failure = write_failure = 0;
	fail_on_write = write_calls = 0;
	rdev_ptr = NULL;
	offset = line_offset = 0;
	write_failed = false;
}

static int bytes_equal(size_t offset_bytes, const char *value, size_t length)
{
	return !memcmp(flash + offset_bytes, value, length);
}

int main(void)
{
	uint8_t snapshot[sizeof(flash)];

	reset_fixture(sizeof(flash), 0);
	if (!flashconsole_append((const uint8_t *)"abc", 3) ||
	    !bytes_equal(0, "abc", 3))
		return 1;
	if (!flashconsole_append((const uint8_t *)"def", 3) ||
	    !bytes_equal(0, "abcdef", 6))
		return 2;

	reset_fixture(sizeof(flash), 257);
	if (!flashconsole_append((const uint8_t *)"AB", 2) ||
	    !bytes_equal(257, "AB", 2))
		return 3;

	reset_fixture(20, 10);
	if (!flashconsole_append((const uint8_t *)"0123456789", 10))
		return 4;
	memcpy(snapshot, flash, sizeof(flash));
	if (flashconsole_append((const uint8_t *)"+", 1) ||
	    memcmp(snapshot, flash, sizeof(flash)))
		return 5;

	reset_fixture(20, 10);
	memcpy(snapshot, flash, sizeof(flash));
	if (flashconsole_append((const uint8_t *)"01234567890", 11) ||
	    memcmp(snapshot, flash, sizeof(flash)))
		return 6;

	reset_fixture(sizeof(flash), 0);
	locate_failure = 1;
	if (flashconsole_append((const uint8_t *)"x", 1))
		return 7;
	locate_failure = 0;
	if (!flashconsole_append((const uint8_t *)"x", 1))
		return 8;

	reset_fixture(sizeof(flash), 0);
	read_failure = 1;
	if (flashconsole_append((const uint8_t *)"x", 1))
		return 9;
	read_failure = 0;
	if (!flashconsole_append((const uint8_t *)"x", 1))
		return 10;

	reset_fixture(sizeof(flash), 0);
	write_failure = 1;
	if (flashconsole_append((const uint8_t *)"x", 1))
		return 11;
	write_failure = 0;
	if (flashconsole_append((const uint8_t *)"x", 1) || flash[0] != 0xff ||
	    flash[1] != 0xff)
		return 12;

	reset_fixture(sizeof(flash), 0);
	uint8_t large[200];
	memset(large, 'q', sizeof(large));
	fail_on_write = 2;
	if (flashconsole_append(large, sizeof(large)) || write_calls != 2 ||
	    memcmp(flash, large, LINE_BUFFER_SIZE) ||
	    flash[LINE_BUFFER_SIZE] != 0xff)
		return 13;
	if (flashconsole_append((const uint8_t *)"z", 1) || write_calls != 2 ||
	    flash[LINE_BUFFER_SIZE] != 0xff)
		return 14;

	reset_fixture(7, 7);
	if (flashconsole_append((const uint8_t *)"x", 1))
		return 15;
	return 0;
}
