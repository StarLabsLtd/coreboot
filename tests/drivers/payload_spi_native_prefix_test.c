/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <commonlib/region.h>
#include <console/payload_spi_console.h>

/* Actual SMM service, parser and flash implementation; only platform media,
 * fixed-loader span getters and diagnostics are HOST models. */
#include "../../src/drivers/spi/flashconsole.c"
#include "../../src/drivers/spi/payload_spi_console.c"
#include "../../src/drivers/spi/payload_spi_console_smi.c"

static uint8_t media[0xb0000], console_storage[0x20000];
static struct {
	struct payload_spi_console_request header;
	uint8_t data[256];
} request;
static bool overlap, erase_failure, erase_lies, write_failure, source_mutation;
static bool media_leased, missing_span;
static bool wrong_geometry, write_lies;
static size_t erases, writes;
static const char old_prefix[] = "coreboot-prior-boot\n";
static const char current_prefix[] = "coreboot-current-boot\nramstage starting\n";
static const char record[] = "CDK2 | HANDOFF_VALIDATE | begin\n";

int printk(int level, const char *format, ...)
{
	(void)level;
	(void)format;
	return 0;
}

static ssize_t read_media(const struct region_device *device, void *data,
	size_t start, size_t length)
{
	(void)device;
	if (start > sizeof(media) || length > sizeof(media) - start)
		return -1;
	memcpy(data, media + start, length);
	return length;
}

static ssize_t write_media(const struct region_device *device, const void *data,
	size_t start, size_t length)
{
	(void)device;
	if (write_failure || media_leased || start < 0x80000 ||
	    start > 0xa0000 || length > 0xa0000 - start)
		return -1;
	writes++;
	if (!write_lies)
		memcpy(media + start, data, length);
	if (source_mutation)
		console_storage[8] ^= 1;
	return length;
}

static ssize_t erase_media(const struct region_device *device, size_t start, size_t length)
{
	(void)device;
	if (media_leased || start != 0x80000 || length != 0x20000)
		return -1;
	erases++;
	if (!erase_lies)
		memset(media + start, 0xff, erase_failure ? length / 2 : length);
	return erase_failure ? (ssize_t)(length / 2) : (ssize_t)length;
}

static const struct region_device_ops media_ops = {
	.readat = read_media, .writeat = write_media, .eraseat = erase_media,
};
static const struct region_device root_media = {
	.ops = &media_ops, .region = { .offset = 0, .size = sizeof(media) },
};
static const struct region_device *admitted_media = &root_media;

int fmap_locate_area_as_rdev_rw(const char *name, struct region_device *device)
{
	if (!strcmp(name, "CONSOLE"))
		return rdev_chain(device, admitted_media, wrong_geometry ? 0x70000 : 0x80000, 0x20000);
	if (!strcmp(name, "SMMSTORE"))
		return rdev_chain(device, admitted_media, overlap ? 0x80000 : 0, 0x10000);
	return -1;
}

void smm_get_payload_spi_console_buffer(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)&request;
	*size = sizeof(request);
}

void smm_get_cbmemc_buffer(void **base, size_t *size)
{
	*base = missing_span ? NULL : console_storage;
	*size = sizeof(console_storage);
}

static void prepare(void)
{
	uint32_t size = sizeof(console_storage) - 8;
	uint32_t cursor = sizeof(current_prefix) - 1 + sizeof(record) - 1;
	memset(console_storage, 0, sizeof(console_storage));
	memcpy(console_storage, &size, sizeof(size));
	memcpy(console_storage + 4, &cursor, sizeof(cursor));
	memcpy(console_storage + 8, current_prefix, sizeof(current_prefix) - 1);
	memcpy(console_storage + 8 + sizeof(current_prefix) - 1, record, sizeof(record) - 1);
	request.header = (struct payload_spi_console_request){
		.signature = PAYLOAD_SPI_CONSOLE_SIGNATURE, .version = PAYLOAD_SPI_CONSOLE_VERSION,
		.header_size = sizeof(request.header), .length = sizeof(record) - 1,
		.status = PAYLOAD_SPI_CONSOLE_PENDING,
	};
	memcpy(request.data, record, sizeof(record) - 1);
}

static void reset_boot(void)
{
	remaining = PAYLOAD_SPI_CONSOLE_BOOT_LIMIT;
	busy = prefix_attempted = prefix_ready = false;
	latest_boot_attempted = write_failed = false;
	rdev_ptr = NULL;
	offset = line_offset = 0;
	overlap = erase_failure = erase_lies = write_failure = source_mutation = false;
	media_leased = missing_span = false;
	wrong_geometry = write_lies = false;
	erases = writes = 0;
	prepare();
}

static void assert_outside_preserved(void)
{
	for (size_t byte = 0; byte < sizeof(media); byte++)
		if (byte < 0x80000 || byte >= 0xa0000)
			assert(media[byte] == 0xa5);
}

int main(void)
{
	memset(media, 0xa5, sizeof(media));
	memcpy(media + 0x80000, old_prefix, sizeof(old_prefix) - 1);
	reset_boot();
	payload_spi_console_smi();
#if defined(EXPECT_ERASE_OMISSION)
	if (request.header.status == PAYLOAD_SPI_CONSOLE_IO_ERROR &&
	    erases == 0 && writes == 0 &&
	    !memcmp(media + 0x80000, old_prefix, sizeof(old_prefix) - 1)) {
		fputs("EXPECTED: omitted reclaim erase rejects stale prior-boot prefix before writes\n", stderr);
		return 1;
	}
	return 2;
#elif defined(EXPECT_PREFIX_OMISSION)
	if (request.header.status == PAYLOAD_SPI_CONSOLE_SUCCESS && erases == 1 &&
	    memcmp(media + 0x80000, current_prefix, sizeof(current_prefix) - 1)) {
		fputs("EXPECTED: omitted native prefix loses complete current coreboot stream\n", stderr);
		return 1;
	}
	return 2;
#endif
	assert(request.header.status == PAYLOAD_SPI_CONSOLE_SUCCESS && erases == 1);
	assert(!memcmp(media + 0x80000, current_prefix, sizeof(current_prefix) - 1));
	assert(!memcmp(media + 0x80000 + sizeof(current_prefix) - 1, record, sizeof(record) - 1));
	assert(media[0x80000 + sizeof(current_prefix) + sizeof(record) - 2] == 0xff);
	assert(remaining == PAYLOAD_SPI_CONSOLE_BOOT_LIMIT - sizeof(record) + 1);
	assert_outside_preserved();
	/* Same boot appends without erasing; next boot removes a seeded old tail. */
	request.header.status = PAYLOAD_SPI_CONSOLE_PENDING;
	payload_spi_console_smi();
	assert(request.header.status == PAYLOAD_SPI_CONSOLE_SUCCESS && erases == 1);
	media[0x90000] = 0x11;
	reset_boot();
	payload_spi_console_smi();
	assert(request.header.status == PAYLOAD_SPI_CONSOLE_SUCCESS && erases == 1);
	assert(media[0x90000] == 0xff);
	assert_outside_preserved();
	puts("PASS exact one reclaim, complete prefix/first-record no duplication, second-boot stale-tail removal");
	for (unsigned int mode = 0; mode < 13; mode++) {
		reset_boot();
		memset(media + 0x80000, 0x11, 0x20000);
		if (mode == 0) overlap = true;
		if (mode == 1) erase_failure = true;
		if (mode == 2) erase_lies = true;
		if (mode == 3) write_failure = true;
		if (mode == 4) source_mutation = true;
		if (mode == 5) media_leased = true;
		if (mode == 6) missing_span = true;
		if (mode == 7) console_storage[7] |= 0x80;
		if (mode == 8) {
			uint32_t cursor = sizeof(prefix) + sizeof(record);
			memcpy(console_storage + 4, &cursor, sizeof(cursor));
		}
		if (mode == 9) console_storage[8 + sizeof(current_prefix)] ^= 1;
		if (mode == 10) console_storage[0] ^= 1;
		if (mode == 11) wrong_geometry = true;
		if (mode == 12) write_lies = true;
		payload_spi_console_smi();
		assert(request.header.status == PAYLOAD_SPI_CONSOLE_IO_ERROR);
		size_t failed_erases = erases;
		request.header.status = PAYLOAD_SPI_CONSOLE_PENDING;
		payload_spi_console_smi();
		assert(request.header.status == PAYLOAD_SPI_CONSOLE_IO_ERROR && erases == failed_erases);
		assert(remaining == PAYLOAD_SPI_CONSOLE_BOOT_LIMIT);
		assert_outside_preserved();
		printf("PASS fail-closed initialization mode %u without retry/reclaim\n", mode);
	}
	reset_boot();
	request.header.flags = 1;
	payload_spi_console_smi();
	assert(request.header.status == PAYLOAD_SPI_CONSOLE_INVALID && erases == 0 && writes == 0);
	reset_boot();
	busy = true;
	payload_spi_console_smi();
	assert(request.header.status == PAYLOAD_SPI_CONSOLE_BUSY && erases == 0 && writes == 0);
	reset_boot();
	remaining = 1;
	payload_spi_console_smi();
	assert(request.header.status == PAYLOAD_SPI_CONSOLE_LIMIT && erases == 0 && writes == 0);
	puts("PASS malformed/reentry/budget controls have no erase or write");
	reset_boot();
	uint32_t full_cursor = sizeof(prefix) + sizeof(record) - 1;
	memcpy(console_storage + 4, &full_cursor, sizeof(full_cursor));
	memset(console_storage + 8, 'P', sizeof(prefix));
	memcpy(console_storage + 8 + sizeof(prefix), record, sizeof(record) - 1);
	payload_spi_console_smi();
	assert(request.header.status == PAYLOAD_SPI_CONSOLE_SUCCESS);
	for (size_t byte = 0; byte < sizeof(prefix); byte++)
		assert(media[0x80000 + byte] == 'P');
	memset(request.data, 'D', sizeof(request.data));
	while (remaining) {
		request.header.length = remaining < sizeof(request.data) ? remaining : sizeof(request.data);
		request.header.status = PAYLOAD_SPI_CONSOLE_PENDING;
		payload_spi_console_smi();
		assert(request.header.status == PAYLOAD_SPI_CONSOLE_SUCCESS);
	}
	assert(offset == 0x20000 && erases == 1);
	size_t full_writes = writes;
	assert(!flashconsole_append((const uint8_t *)"x", 1));
	request.header.length = 1;
	request.header.status = PAYLOAD_SPI_CONSOLE_PENDING;
	payload_spi_console_smi();
	assert(request.header.status == PAYLOAD_SPI_CONSOLE_LIMIT && writes == full_writes && erases == 1);
	assert_outside_preserved();
	puts("PASS exact maximum full prefix and native quota fill, exhausted media refuses without erase/write");
	return 0;
}
