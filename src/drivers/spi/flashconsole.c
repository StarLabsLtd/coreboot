/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <commonlib/region.h>
#include <fmap.h>
#include <console/console.h>
#include <console/flash.h>
#include <types.h>

#define LINE_BUFFER_SIZE 128
#define READ_BUFFER_SIZE 0x100

static const struct region_device *rdev_ptr;
static struct region_device rdev;
static uint8_t line_buffer[LINE_BUFFER_SIZE];
static size_t offset;
static size_t line_offset;
static bool initialized;
static bool disabled;
static bool failed;

static int flashconsole_flush_buffer(void);

void flashconsole_init(void)
{
	uint8_t buffer[READ_BUFFER_SIZE];
	size_t size;
	size_t initial_offset = 0;
	size_t len = READ_BUFFER_SIZE;
	size_t i;

	if (initialized || disabled)
		return;

	if (fmap_locate_area_as_rdev_rw("CONSOLE", &rdev)) {
		printk(BIOS_INFO, "Can't find 'CONSOLE' area in FMAP\n");
		return;
	}
	size = region_device_sz(&rdev);

	/*
	 * We need to check the region until we find a 0xff indicating
	 * the end of a previous log write.
	 * We can't erase the region because one stage would erase the
	 * data from the previous stage. Also, it looks like doing an
	 * erase could completely freeze the SPI controller and then
	 * we can't write anything anymore (apparently might happen if
	 * the sector is already erased, so we would need to read
	 * anyways to check if it's all 0xff).
	 */
	for (i = 0; i < len && initial_offset < size;) {
		// Fill the buffer on first iteration
		if (i == 0) {
			len = MIN(READ_BUFFER_SIZE, size - initial_offset);
			if (rdev_readat(&rdev, buffer, initial_offset, len) != len)
				return;
		}
		if (buffer[i] == 0xff) {
			initial_offset += i;
			break;
		}
		// If we're done, repeat the process for the next sector
		if (++i == READ_BUFFER_SIZE) {
			initial_offset += len;
			i = 0;
		}
	}
	// Make sure there is still space left on the console
	if (initial_offset >= size) {
		printk(BIOS_INFO, "No space left on 'console' region in SPI flash\n");
		disabled = true;
		return;
	}

	offset = initial_offset;
	rdev_ptr = &rdev;
	initialized = true;
}

void flashconsole_tx_byte(unsigned char c)
{
	if (!rdev_ptr)
		return;

	size_t region_size = region_device_sz(rdev_ptr);

	if (line_offset < LINE_BUFFER_SIZE)
		line_buffer[line_offset++] = c;

	if (line_offset >= LINE_BUFFER_SIZE ||
	    offset + line_offset >= region_size || c == '\n') {
		flashconsole_tx_flush();
	}
}

void flashconsole_tx_flush(void)
{
	flashconsole_flush_buffer();
}

static int flashconsole_flush_buffer(void)
{
	size_t len = line_offset;
	size_t region_size;

	/* Prevent any recursive loops in case the spi flash driver
	 * calls printk (in case of transaction timeout or
	 * any other error while writing) */
	static bool busy;

	/* In addition to being an obvious runtime optimization, checking for
	 * len = 0 also prevents false disabling of the driver, as rdev_writeat
	 * seems to return -1 if len is zero even though this should be a
	 * recoverable condition. */
	if (busy || !rdev_ptr || len == 0)
		return len == 0 ? 0 : -1;

	busy = true;
	region_size = region_device_sz(rdev_ptr);
	if (offset + len >= region_size)
		len = region_size - offset;

	if (rdev_writeat(&rdev, line_buffer, offset, len) != len) {
		rdev_ptr = NULL;
		line_offset = 0;
		disabled = true;
		failed = true;
		busy = false;
		return -1;
	}

	offset += len;
	line_offset = 0;
	// If the region is full, stop future write attempts.
	if (offset >= region_size) {
		rdev_ptr = NULL;
		disabled = true;
	}

	busy = false;
	return 0;
}

int flashconsole_append(const void *buffer, size_t size)
{
	const uint8_t *data = buffer;

	if (!rdev_ptr)
		flashconsole_init();
	if (!rdev_ptr)
		return -1;

	for (size_t i = 0; i < size; i++) {
		flashconsole_tx_byte(data[i]);
		if (!rdev_ptr)
			return !failed && i + 1 == size ? 0 : -1;
	}

	return flashconsole_flush_buffer();
}
