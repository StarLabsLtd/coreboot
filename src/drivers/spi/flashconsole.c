/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <commonlib/region.h>
#include <fmap.h>
#include <console/console.h>
#include <console/flash.h>
#include <string.h>
#include <types.h>

#define LINE_BUFFER_SIZE 128
#define READ_BUFFER_SIZE 0x100

static const struct region_device *rdev_ptr;
static struct region_device rdev;
static uint8_t line_buffer[LINE_BUFFER_SIZE];
static size_t offset;
static size_t line_offset;
static bool busy;
static bool write_failed;

void flashconsole_init(void)
{
	uint8_t buffer[READ_BUFFER_SIZE];
	size_t size;
	size_t initial_offset = 0;
	size_t len = READ_BUFFER_SIZE;
	size_t i;

	if (write_failed || busy)
		return;

	if (fmap_locate_area_as_rdev_rw("CONSOLE", &rdev)) {
		printk(BIOS_INFO, "Can't find 'CONSOLE' area in FMAP\n");
		return;
	}
	size = region_device_sz(&rdev);
#if CONFIG(PAYLOAD_SPI_FLASH_CONSOLE)
	/* The SMM writer must not follow a redirected FMAP entry. */
	if (region_device_offset(&rdev) != FMAP_SECTION_CONSOLE_START ||
	    size != FMAP_SECTION_CONSOLE_SIZE)
		return;
#endif

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
		if (++i == len) {
			initial_offset += len;
			i = 0;
		}
	}
	// Make sure there is still space left on the console
	if (initial_offset >= size) {
		printk(BIOS_INFO, "No space left on 'console' region in SPI flash\n");
		return;
	}

	offset = initial_offset;
	rdev_ptr = &rdev;
}

void flashconsole_tx_byte(unsigned char c)
{
	if (!rdev_ptr || busy || write_failed)
		return;

	size_t region_size = region_device_sz(rdev_ptr);

	if (offset >= region_size || line_offset >= region_size - offset)
		return;
	line_buffer[line_offset++] = c;

	if (line_offset >= LINE_BUFFER_SIZE ||
	    line_offset >= region_size - offset || c == '\n') {
		flashconsole_tx_flush();
	}
}

static bool flashconsole_flush(void)
{
	size_t len = line_offset;
	size_t region_size;

	/* SPI error logging must not recurse or alter the in-flight buffer. */
	if (busy || write_failed || !rdev_ptr)
		return false;
	if (len == 0)
		return true;

	busy = true;
	region_size = region_device_sz(rdev_ptr);
	if (offset > region_size || len > region_size - offset ||
	    rdev_writeat(&rdev, line_buffer, offset, len) != len)
		goto failed;
	if (ENV_SMM && CONFIG(PAYLOAD_SPI_FLASH_CONSOLE)) {
		uint8_t written[LINE_BUFFER_SIZE];

		if (rdev_readat(&rdev, written, offset, len) != len ||
		    memcmp(written, line_buffer, len))
			goto failed;
	}

	offset += len;
	line_offset = 0;
	busy = false;
	return true;

failed:
	/* A partial write cannot be retried as a fresh append. */
	write_failed = true;
	line_offset = 0;
	busy = false;
	return false;
}

void flashconsole_tx_flush(void)
{
	(void)flashconsole_flush();
}

bool flashconsole_append(const uint8_t *data, size_t length)
{
	size_t region_size;

	if (!data || !length || write_failed || busy)
		return false;

	/* SMM has its own console state; recover the persisted cursor on first use. */
	if (!rdev_ptr)
		flashconsole_init();
	if (!rdev_ptr)
		return false;
	region_size = region_device_sz(rdev_ptr);
	if (offset > region_size || line_offset > region_size - offset ||
	    length > region_size - offset - line_offset)
		return false;

	while (length) {
		size_t chunk = MIN(length, LINE_BUFFER_SIZE - line_offset);

		memcpy(line_buffer + line_offset, data, chunk);
		line_offset += chunk;
		data += chunk;
		length -= chunk;
		if (line_offset == LINE_BUFFER_SIZE && !flashconsole_flush())
			return false;
	}
	return flashconsole_flush();
}
