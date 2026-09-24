/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdarg.h>
#include <spi_flash.h>
#include <stdint.h>

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)

static int media_write(const struct spi_flash *active_flash, uint32_t offset,
	size_t size, const void *data);

static const struct spi_flash_ops flash_ops = {
	.write = media_write,
};
static const struct spi_flash flash = {
	.ops = &flash_ops,
};
static unsigned int begin_calls;
static unsigned int end_calls;
static unsigned int write_calls;
static unsigned int assertion_failures;
static int begin_result;
static int end_result;

uint32_t spi_flash_volatile_group_test_exchange_count(uint32_t count);

int printk(int message_level, const char *format, ...)
{
	(void)message_level;
	(void)format;
	return 0;
}

int spi_claim_bus(const struct spi_slave *slave)
{
	(void)slave;
	return 0;
}

void spi_release_bus(const struct spi_slave *slave)
{
	(void)slave;
}

int spi_xfer_vector(const struct spi_slave *slave,
	struct spi_op *vectors, size_t count)
{
	(void)slave;
	(void)vectors;
	(void)count;
	return 0;
}

unsigned int spi_crop_chunk(const struct spi_slave *slave,
	unsigned int command_length, unsigned int requested_length)
{
	(void)slave;
	(void)command_length;
	return requested_length;
}

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		assertion_failures++;
}

int chipset_volatile_group_begin(const struct spi_flash *active_flash)
{
	CHECK(active_flash == &flash);
	begin_calls++;
	return begin_result;
}

int chipset_volatile_group_end(const struct spi_flash *active_flash)
{
	CHECK(active_flash == &flash);
	end_calls++;
	return end_result;
}

static int media_write(const struct spi_flash *active_flash, uint32_t offset,
	size_t size, const void *data)
{
	CHECK(active_flash == &flash && offset == 4 && size == 1 && data);
	write_calls++;
	return 0;
}

static void reset_state(void)
{
	(void)spi_flash_volatile_group_test_exchange_count(0);
	begin_calls = 0;
	end_calls = 0;
	write_calls = 0;
	assertion_failures = 0;
	begin_result = 0;
	end_result = 0;
}

static uint32_t group_count(void)
{
	uint32_t count = spi_flash_volatile_group_test_exchange_count(0);

	(void)spi_flash_volatile_group_test_exchange_count(count);
	return count;
}

static void success_and_nesting(void)
{
	reset_state();
	CHECK(spi_flash_volatile_group_begin(&flash) == 0);
	CHECK(group_count() == 1 && begin_calls == 1 && end_calls == 0);
	CHECK(spi_flash_volatile_group_begin(&flash) == 0);
	CHECK(group_count() == 2 && begin_calls == 1 && end_calls == 0);
	CHECK(spi_flash_volatile_group_end(&flash) == 0);
	CHECK(group_count() == 1 && begin_calls == 1 && end_calls == 0);
	CHECK(spi_flash_volatile_group_end(&flash) == 0);
	CHECK(group_count() == 0 && begin_calls == 1 && end_calls == 1);
	CHECK(assertion_failures == 0);
}

static void failed_begin(void)
{
	reset_state();
	begin_result = -7;
	CHECK(spi_flash_volatile_group_begin(&flash) == -7);
	CHECK(group_count() == 0 && begin_calls == 1 && end_calls == 0);
	begin_result = 0;
	CHECK(spi_flash_volatile_group_begin(&flash) == 0);
	CHECK(group_count() == 1 && begin_calls == 2 && end_calls == 0);
	CHECK(spi_flash_volatile_group_end(&flash) == 0);
	CHECK(group_count() == 0 && end_calls == 1);
}

static void underflow(void)
{
	reset_state();
	CHECK(spi_flash_volatile_group_end(&flash) == -1);
	CHECK(group_count() == 0 && begin_calls == 0 && end_calls == 0);
	CHECK(assertion_failures == 1);
}

static void overflow(void)
{
	reset_state();
	CHECK(spi_flash_volatile_group_test_exchange_count(UINT32_MAX) == 0);
	CHECK(spi_flash_volatile_group_begin(&flash) == -1);
	CHECK(group_count() == UINT32_MAX && begin_calls == 0 && end_calls == 0);
	(void)spi_flash_volatile_group_test_exchange_count(0);
}

static void failed_end_closes(void)
{
	reset_state();
	CHECK(spi_flash_volatile_group_begin(&flash) == 0);
	end_result = -9;
	CHECK(spi_flash_volatile_group_end(&flash) == -9);
	CHECK(group_count() == 0 && begin_calls == 1 && end_calls == 1);
	end_result = 0;
	CHECK(spi_flash_volatile_group_begin(&flash) == 0);
	CHECK(spi_flash_volatile_group_end(&flash) == 0);
	CHECK(group_count() == 0 && begin_calls == 2 && end_calls == 2);
	CHECK(assertion_failures == 0);
}

static void write_wrapper_boundaries(void)
{
	uint8_t data = 0xa5;

	reset_state();
	CHECK(spi_flash_volatile_group_begin(&flash) == 0);
	CHECK(spi_flash_write(&flash, 4, sizeof(data), &data) == 0);
	CHECK(group_count() == 1 && begin_calls == 1 && end_calls == 0 &&
		write_calls == 1);
	CHECK(spi_flash_volatile_group_end(&flash) == 0);
	CHECK(group_count() == 0 && end_calls == 1);

	reset_state();
	begin_result = -7;
	CHECK(spi_flash_write(&flash, 4, sizeof(data), &data) == -1);
	CHECK(group_count() == 0 && begin_calls == 1 && end_calls == 0 &&
		write_calls == 0);

	reset_state();
	end_result = -9;
	CHECK(spi_flash_write(&flash, 4, sizeof(data), &data) == -1);
	CHECK(group_count() == 0 && begin_calls == 1 && end_calls == 1 &&
		write_calls == 1);
	end_result = 0;
	CHECK(spi_flash_write(&flash, 4, sizeof(data), &data) == 0);
	CHECK(group_count() == 0 && begin_calls == 2 && end_calls == 2 &&
		write_calls == 2);
}

static void disabled_noop(void)
{
	reset_state();
	CHECK(spi_flash_volatile_group_test_exchange_count(UINT32_MAX) == 0);
	CHECK(spi_flash_volatile_group_begin(&flash) == 0);
	CHECK(spi_flash_volatile_group_end(&flash) == 0);
	CHECK(group_count() == UINT32_MAX && begin_calls == 0 && end_calls == 0);
	CHECK(assertion_failures == 0);
	(void)spi_flash_volatile_group_test_exchange_count(0);
}

int main(void)
{
	if (!CONFIG(SPI_FLASH_HAS_VOLATILE_GROUP)) {
		disabled_noop();
		return 0;
	}
	success_and_nesting();
	failed_begin();
	underflow();
	overflow();
	failed_end_closes();
	write_wrapper_boundaries();
	return 0;
}
