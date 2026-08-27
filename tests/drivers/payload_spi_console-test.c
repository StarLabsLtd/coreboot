/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/payload_spi_console.h>
#include <string.h>
#include <tests/test.h>

struct fixture {
	struct payload_spi_console_request request;
	uint8_t data[PAYLOAD_SPI_CONSOLE_MAX_CHUNK];
};

struct capture {
	struct fixture *source;
	uint8_t data[PAYLOAD_SPI_CONSOLE_MAX_CHUNK];
	size_t length;
};

static bool capture_sink(const uint8_t *data, size_t length, void *context)
{
	struct capture *capture = context;

	memset(capture->source->data, 0xee, sizeof(capture->source->data));
	memcpy(capture->data, data, length);
	capture->length = length;
	return true;
}

static void prepare(struct fixture *fixture, const void *data, size_t length)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->request.signature = PAYLOAD_SPI_CONSOLE_SIGNATURE;
	fixture->request.version = PAYLOAD_SPI_CONSOLE_VERSION;
	fixture->request.header_size = sizeof(fixture->request);
	fixture->request.length = length;
	fixture->request.status = PAYLOAD_SPI_CONSOLE_PENDING;
	memcpy(fixture->data, data, length);
}

static bool reject_sink(const uint8_t *data, size_t length, void *context)
{
	return false;
}

static void test_sink_failure_preserves_quota(void **state)
{
	const uint8_t message[] = "write failure";
	struct fixture fixture;
	size_t remaining = sizeof(message);
	bool busy = false;

	prepare(&fixture, message, sizeof(message));
	assert_int_equal(payload_spi_console_process(&fixture, sizeof(fixture),
		&remaining, &busy, reject_sink, NULL), PAYLOAD_SPI_CONSOLE_IO_ERROR);
	assert_int_equal(remaining, sizeof(message));
	assert_false(busy);
}

static enum payload_spi_console_status process(struct fixture *fixture,
	size_t buffer_size, size_t *remaining, bool *busy, struct capture *capture)
{
	return payload_spi_console_process(fixture, buffer_size, remaining, busy,
		capture_sink, capture);
}

static void test_valid_request_is_copied_before_sink(void **state)
{
	const uint8_t message[] = "payload diagnostic";
	struct fixture fixture;
	struct capture capture = { .source = &fixture };
	size_t remaining = sizeof(message);
	bool busy = false;

	prepare(&fixture, message, sizeof(message));
	assert_int_equal(process(&fixture, sizeof(fixture), &remaining, &busy,
		&capture), PAYLOAD_SPI_CONSOLE_SUCCESS);
	assert_int_equal(fixture.request.status, PAYLOAD_SPI_CONSOLE_SUCCESS);
	assert_int_equal(capture.length, sizeof(message));
	assert_memory_equal(capture.data, message, sizeof(message));
	assert_int_equal(remaining, 0);
	assert_false(busy);
}

static void test_rejects_malformed_headers(void **state)
{
	const uint8_t message[] = "x";
	struct fixture fixture;
	struct capture capture = { .source = &fixture };
	size_t remaining = PAYLOAD_SPI_CONSOLE_BOOT_LIMIT;
	bool busy = false;

	prepare(&fixture, message, sizeof(message));
	fixture.request.signature = 0;
	assert_int_equal(process(&fixture, sizeof(fixture), &remaining, &busy,
		&capture), PAYLOAD_SPI_CONSOLE_INVALID);
	prepare(&fixture, message, sizeof(message));
	fixture.request.version++;
	assert_int_equal(process(&fixture, sizeof(fixture), &remaining, &busy,
		&capture), PAYLOAD_SPI_CONSOLE_INVALID);
	prepare(&fixture, message, sizeof(message));
	fixture.request.header_size--;
	assert_int_equal(process(&fixture, sizeof(fixture), &remaining, &busy,
		&capture), PAYLOAD_SPI_CONSOLE_INVALID);
	prepare(&fixture, message, sizeof(message));
	fixture.request.flags = 1;
	assert_int_equal(process(&fixture, sizeof(fixture), &remaining, &busy,
		&capture), PAYLOAD_SPI_CONSOLE_INVALID);
	prepare(&fixture, message, sizeof(message));
	fixture.request.status = PAYLOAD_SPI_CONSOLE_SUCCESS;
	assert_int_equal(process(&fixture, sizeof(fixture), &remaining, &busy,
		&capture), PAYLOAD_SPI_CONSOLE_INVALID);
	assert_int_equal(capture.length, 0);
}

static void test_rejects_lengths_outside_fixed_buffer(void **state)
{
	const uint8_t message[] = "x";
	struct fixture fixture;
	struct capture capture = { .source = &fixture };
	size_t remaining = PAYLOAD_SPI_CONSOLE_BOOT_LIMIT;
	bool busy = false;

	prepare(&fixture, message, sizeof(message));
	fixture.request.length = 0;
	assert_int_equal(process(&fixture, sizeof(fixture), &remaining, &busy,
		&capture), PAYLOAD_SPI_CONSOLE_INVALID);
	prepare(&fixture, message, sizeof(message));
	fixture.request.length = PAYLOAD_SPI_CONSOLE_MAX_CHUNK + 1;
	assert_int_equal(process(&fixture, sizeof(fixture), &remaining, &busy,
		&capture), PAYLOAD_SPI_CONSOLE_INVALID);
	prepare(&fixture, message, sizeof(message));
	assert_int_equal(process(&fixture, sizeof(fixture.request), &remaining,
		&busy, &capture), PAYLOAD_SPI_CONSOLE_INVALID);
	assert_int_equal(capture.length, 0);
}

static void test_busy_and_boot_limit_fail_closed(void **state)
{
	const uint8_t message[] = "limit";
	struct fixture fixture;
	struct capture capture = { .source = &fixture };
	size_t remaining = sizeof(message) - 1;
	bool busy = false;

	prepare(&fixture, message, sizeof(message));
	assert_int_equal(process(&fixture, sizeof(fixture), &remaining, &busy,
		&capture), PAYLOAD_SPI_CONSOLE_LIMIT);
	assert_int_equal(remaining, sizeof(message) - 1);
	prepare(&fixture, message, sizeof(message));
	remaining = PAYLOAD_SPI_CONSOLE_BOOT_LIMIT;
	busy = true;
	assert_int_equal(process(&fixture, sizeof(fixture), &remaining, &busy,
		&capture), PAYLOAD_SPI_CONSOLE_BUSY);
	assert_true(busy);
	assert_int_equal(capture.length, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_valid_request_is_copied_before_sink),
		cmocka_unit_test(test_rejects_malformed_headers),
		cmocka_unit_test(test_rejects_lengths_outside_fixed_buffer),
		cmocka_unit_test(test_busy_and_boot_limit_fail_closed),
		cmocka_unit_test(test_sink_failure_preserves_quota),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
