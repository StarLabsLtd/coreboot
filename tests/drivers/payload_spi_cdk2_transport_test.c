/* SPDX-License-Identifier: GPL-2.0-only */
#define main prefix_model_main
#include "payload_spi_native_prefix_test.c"
#undef main
#undef ARRAY_SIZE
#include <cdk2/diagnostic.h>

static size_t submits;
static bool fail_submit;

/* HOST equivalent of the sole outb submission instruction, not a fabricated
 * response: it executes the genuine producer SMM service on the fixed port. */
void cdk2_spi_host_submit(void *opaque)
{
	assert(opaque == &request);
	submits++;
	if (fail_submit)
		request.header.status = PAYLOAD_SPI_CONSOLE_IO_ERROR;
	else
		payload_spi_console_smi();
}

static void configure_transport(size_t chunk)
{
	uint32_t cursor = sizeof(current_prefix) - 1;
	reset_boot();
	memset(media, 0xa5, sizeof(media));
	memcpy(console_storage + 4, &cursor, sizeof(cursor));
	submits = 0;
	fail_submit = false;
	cdk2_diag_configure(&(const struct cdk2_diag_config){
		.cbmem_console = (void *)console_storage,
		.cbmem_console_bytes = sizeof(console_storage),
		.spi_console_buffer = &request, .spi_console_buffer_bytes = sizeof(request),
		.spi_console_max_chunk = chunk, .spi_console_boot_limit = PAYLOAD_SPI_CONSOLE_BOOT_LIMIT,
		.spi_console_apm_cmd = PAYLOAD_SPI_CONSOLE_APM_CMD,
	});
}

static void same_stream(void)
{
	uint32_t cursor;
	memcpy(&cursor, console_storage + 4, sizeof(cursor));
	assert(cursor < 0x20000 && offset == cursor);
	assert(!memcmp(media + 0x80000, console_storage + 8, cursor));
	assert(media[0x80000 + cursor] == 0xff);
	assert(erases == 1);
	assert_outside_preserved();
}

int main(void)
{
	const size_t chunks[] = {1, 8, 32, 256};
	char message[601];
	memset(message, 'M', sizeof(message) - 1);
	message[sizeof(message) - 1] = 0;
	for (size_t test = 0; test < sizeof(chunks) / sizeof(chunks[0]); test++) {
		configure_transport(chunks[test]);
		cdk2_diag_value(CDK2_DIAG_BDS, message, 1);
#if defined(EXPECT_OLD_SEQUENCE)
		uint32_t cursor;
		memcpy(&cursor, console_storage + 4, sizeof(cursor));
		if (test == 0 && submits == 1 &&
		    request.header.status == PAYLOAD_SPI_CONSOLE_IO_ERROR &&
		    erases == 0 && writes == 0 && cursor > 600) {
			fputs("EXPECTED: old write-before-flush fails exact pending suffix at first chunk; CBMEM retained\n", stderr);
			return 1;
		}
		fputs("UNEXPECTED: old-order inverse did not reject the first suffix\n", stderr);
		return 2;
#endif
		assert(submits > 1 && request.header.status == PAYLOAD_SPI_CONSOLE_SUCCESS);
		same_stream();
		size_t first_submits = submits;
		cdk2_diag_value(CDK2_DIAG_BDS, message, 2);
		assert(submits > first_submits && request.header.status == PAYLOAD_SPI_CONSOLE_SUCCESS);
		same_stream();
		printf("PASS actual CDK2 first/later 600-byte records, chunk %zu, exact CBMEM/SPI stream\n", chunks[test]);
	}
	configure_transport(8);
	fail_submit = true;
	cdk2_diag_value(CDK2_DIAG_BDS, message, 3);
	uint32_t failed_cursor;
	memcpy(&failed_cursor, console_storage + 4, sizeof(failed_cursor));
	assert(failed_cursor > 600 && submits == 1 && erases == 0 && writes == 0);
	struct cdk2_diag_config saved;
	cdk2_diag_test_snapshot_config(&saved);
	assert(saved.spi_console_buffer == NULL && saved.cbmem_console == (void *)console_storage);
	puts("PASS failed chunk disables only SPI while incoming/rest-of-record bytes remain in CBMEM");
	configure_transport(8);
	console_storage[0] ^= 1;
	cdk2_diag_value(CDK2_DIAG_BDS, message, 4);
	assert(submits == 0 && erases == 0 && writes == 0);
	puts("PASS failed CBMEM ownership has no SPI submission or flash write");
	return 0;
}
