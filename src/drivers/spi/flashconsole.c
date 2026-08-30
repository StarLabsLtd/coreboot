/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <commonlib/region.h>
#include <fmap.h>
#include <console/console.h>
#include <console/flash.h>
#include <types.h>
#include <string.h>

#define LINE_BUFFER_SIZE 128
#define READ_BUFFER_SIZE 0x100

static const struct region_device *rdev_ptr;
static struct region_device rdev;
static uint8_t line_buffer[LINE_BUFFER_SIZE];
static size_t offset;
static size_t line_offset;
static bool write_failed;
static bool latest_boot_attempted;

static bool latest_boot_owner(void)
{
	/* Q35 intentionally instantiates this console only in ramstage. */
	if (CONFIG(BOARD_EMULATION_QEMU_X86_Q35))
		return ENV_RAMSTAGE;
	return ENV_BOOTBLOCK;
}

void flashconsole_init(void)
{
	uint8_t buffer[READ_BUFFER_SIZE];
	struct region_device smmstore;
	size_t size;
	size_t initial_offset = 0;
	size_t len = READ_BUFFER_SIZE;
	size_t i;
	bool latest_owner = CONFIG(CONSOLE_SPI_FLASH_LATEST_BOOT) &&
		latest_boot_owner();

	if (latest_owner && latest_boot_attempted)
		return;
	if (latest_owner) {
		latest_boot_attempted = true;
		rdev_ptr = NULL;
		offset = 0;
		line_offset = 0;
		write_failed = false;
	}

	if (fmap_locate_area_as_rdev_rw("CONSOLE", &rdev)) {
		if (latest_owner)
			write_failed = true;
		printk(BIOS_INFO, "Can't find 'CONSOLE' area in FMAP\n");
		return;
	}
	size = region_device_sz(&rdev);
	if (latest_owner) {
		if (CONFIG(SMMSTORE) &&
		    (fmap_locate_area_as_rdev_rw("SMMSTORE", &smmstore) ||
		     region_overlap(region_device_region(&rdev),
			region_device_region(&smmstore)))) {
			write_failed = true;
			printk(BIOS_ERR, "Invalid CONSOLE/SMMSTORE FMAP ownership\n");
			return;
		}
		if (rdev_eraseat(&rdev, 0, size) != size) {
			write_failed = true;
			printk(BIOS_ERR, "Can't reclaim 'CONSOLE' area in SPI flash\n");
			return;
		}
		offset = 0;
		line_offset = 0;
		rdev_ptr = &rdev;
		return;
	}

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

static bool flashconsole_flush(void)
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
	if (busy || !rdev_ptr)
		return false;
	if (len == 0)
		return true;

	busy = true;
	region_size = region_device_sz(rdev_ptr);
	if (len == 0 || offset > region_size || len > region_size - offset ||
	    rdev_writeat(&rdev, line_buffer, offset, len) != len) {
		write_failed = true;
		line_offset = 0;
		busy = false;
		return false;
	}

	offset += len;
	line_offset = 0;
	busy = false;
	return true;
}

void flashconsole_tx_flush(void)
{
	(void)flashconsole_flush();
}

bool flashconsole_append(const uint8_t *data, size_t length)
{
	size_t region_size;

	if (!data || write_failed)
		return false;

	/* The SMM image has its own static state and does not execute the
	 * ramstage console initialization path.  Recover the persisted append
	 * cursor from the authoritative FMAP CONSOLE region on first use. */
	if (!rdev_ptr)
		flashconsole_init();
	if (!rdev_ptr)
		return false;
	region_size = region_device_sz(rdev_ptr);
	if (offset > region_size || line_offset > region_size - offset ||
	    length > region_size - offset - line_offset)
		return false;

	while (length) {
		size_t available = LINE_BUFFER_SIZE - line_offset;
		size_t chunk = MIN(length, available);

		memcpy(line_buffer + line_offset, data, chunk);
		line_offset += chunk;
		data += chunk;
		length -= chunk;
		if (line_offset == LINE_BUFFER_SIZE && !flashconsole_flush())
			return false;
	}

	return flashconsole_flush();
}
