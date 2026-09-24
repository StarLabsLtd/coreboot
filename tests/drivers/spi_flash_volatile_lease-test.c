/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <spi_flash.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)

static int media_read(const struct spi_flash *flash, uint32_t offset,
	size_t size, void *data);
static int media_write(const struct spi_flash *flash, uint32_t offset,
	size_t size, const void *data);
static int media_erase(const struct spi_flash *flash, uint32_t offset,
	size_t size);
static int media_status(const struct spi_flash *flash, uint8_t *status);

static struct spi_flash_ops flash_ops = {
	.read = media_read,
	.write = media_write,
	.erase = media_erase,
	.status = media_status,
};
static const struct spi_flash_ops alternate_ops = {
	.read = media_read,
	.write = media_write,
	.erase = media_erase,
	.status = media_status,
};
static struct spi_flash flash = {
	.ops = &flash_ops,
	.size = 16,
};
static struct spi_flash_volatile_lease *mutated_lease;
static unsigned int begin_calls;
static unsigned int end_calls;
static unsigned int read_calls;
static unsigned int write_calls;
static unsigned int erase_calls;
static unsigned int status_calls;
static int begin_result;
static int end_result;
static int operation_result;
static bool mutate_begin_owner;
static bool mutate_begin_ops;
static bool mutate_write_owner;
static bool mutate_write_ops;
static bool acquire_during_read;
static bool acquire_during_write;
static int nested_acquire_result;
static struct spi_flash_volatile_lease *nested_lease;
static uint32_t callback_offset;
static size_t callback_size;

int printk(int level, const char *format, ...)
{
	(void)level;
	(void)format;
	return 0;
}

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	CHECK(result);
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

int spi_xfer_vector(const struct spi_slave *slave, struct spi_op *vectors,
	size_t count)
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

int chipset_volatile_group_begin(const struct spi_flash *active_flash)
{
	CHECK(active_flash == &flash);
	begin_calls++;
	if (mutate_begin_owner)
		mutated_lease->private_data[0] = 1;
	if (mutate_begin_ops)
		flash.ops = &alternate_ops;
	return begin_result;
}

int chipset_volatile_group_end(const struct spi_flash *active_flash)
{
	CHECK(active_flash == &flash);
	end_calls++;
	return end_result;
}

static int media_read(const struct spi_flash *active_flash, uint32_t offset,
	size_t size, void *data)
{
	CHECK(active_flash == &flash && data);
	read_calls++;
	callback_offset = offset;
	callback_size = size;
	if (acquire_during_read)
		nested_acquire_result = spi_flash_volatile_lease_begin(&flash,
			nested_lease);
	*(uint8_t *)data = 0xa5;
	return operation_result;
}

static int media_write(const struct spi_flash *active_flash, uint32_t offset,
	size_t size, const void *data)
{
	CHECK(active_flash == &flash && data);
	write_calls++;
	callback_offset = offset;
	callback_size = size;
	if (acquire_during_write)
		nested_acquire_result = spi_flash_volatile_lease_begin(&flash,
			nested_lease);
	if (mutate_write_owner)
		mutated_lease->private_data[0] ^= 1;
	if (mutate_write_ops)
		flash.ops = &alternate_ops;
	return operation_result;
}

static int media_erase(const struct spi_flash *active_flash, uint32_t offset,
	size_t size)
{
	CHECK(active_flash == &flash);
	erase_calls++;
	callback_offset = offset;
	callback_size = size;
	return operation_result;
}

static int media_status(const struct spi_flash *active_flash, uint8_t *status)
{
	CHECK(active_flash == &flash && status);
	status_calls++;
	*status = 0;
	return operation_result;
}

static void reset_fixture(void)
{
	flash.ops = &flash_ops;
	mutated_lease = NULL;
	begin_calls = 0;
	end_calls = 0;
	read_calls = 0;
	write_calls = 0;
	erase_calls = 0;
	status_calls = 0;
	begin_result = 0;
	end_result = 0;
	operation_result = 0;
	mutate_begin_owner = false;
	mutate_begin_ops = false;
	mutate_write_owner = false;
	mutate_write_ops = false;
	acquire_during_read = false;
	acquire_during_write = false;
	nested_acquire_result = 0;
	nested_lease = NULL;
	callback_offset = 0;
	callback_size = 0;
}

static void operations_and_peer_exclusion(void)
{
	struct spi_flash_volatile_lease lease = { 0 };
	struct spi_flash_volatile_lease peer = { 0 };
	uint8_t byte = 0;

	reset_fixture();
	CHECK(!spi_flash_volatile_lease_begin(&flash, &lease));
	CHECK(spi_flash_volatile_lease_begin(&flash, &peer) < 0);
	peer = lease;
	CHECK(spi_flash_volatile_lease_read(&flash, &peer, 4, 1, &byte) < 0);
	CHECK(spi_flash_volatile_lease_end(&peer) < 0);
	CHECK(spi_flash_read(&flash, 4, 1, &byte) < 0);
	CHECK(spi_flash_write(&flash, 4, 1, &byte) < 0);
	CHECK(spi_flash_erase(&flash, 4, 1) < 0);
	CHECK(!spi_flash_volatile_lease_read(&flash, &lease, 4, 1, &byte));
	CHECK(byte == 0xa5);
	CHECK(!spi_flash_volatile_lease_write(&flash, &lease, 4, 1, &byte));
	CHECK(!spi_flash_volatile_lease_erase(&flash, &lease, 4, 1));
	CHECK(!spi_flash_volatile_lease_sync(&flash, &lease));
	CHECK(!spi_flash_volatile_lease_end(&lease));
	CHECK(!memcmp(&lease, &(struct spi_flash_volatile_lease){ 0 },
		sizeof(lease)));
	CHECK(begin_calls == !!CONFIG(SPI_FLASH_HAS_VOLATILE_GROUP));
	CHECK(end_calls == !!CONFIG(SPI_FLASH_HAS_VOLATILE_GROUP));
	CHECK(read_calls == 1 && write_calls == 1 && erase_calls == 1 &&
		status_calls == 1);
	CHECK(!spi_flash_read(&flash, 4, 1, &byte));
}

static void ordinary_activity_excludes_lease(void)
{
	struct spi_flash_volatile_lease lease = { 0 };
	uint8_t byte = 0;

	reset_fixture();
	nested_lease = &lease;
	acquire_during_read = true;
	CHECK(!spi_flash_read(&flash, 4, 1, &byte));
	CHECK(nested_acquire_result < 0);
	CHECK(!memcmp(&lease, &(struct spi_flash_volatile_lease){ 0 },
		sizeof(lease)));

	acquire_during_read = false;
	acquire_during_write = true;
	nested_acquire_result = 0;
	CHECK(!spi_flash_write(&flash, 4, 1, &byte));
	CHECK(nested_acquire_result < 0);
	CHECK(!memcmp(&lease, &(struct spi_flash_volatile_lease){ 0 },
		sizeof(lease)));
}

static void spans_are_bounded(void)
{
	struct spi_flash_volatile_lease lease = { 0 };
	uint8_t byte = 0;
	const size_t overflow = SIZE_MAX - 7;

	reset_fixture();
	CHECK(!spi_flash_volatile_lease_begin(&flash, &lease));

	CHECK(!spi_flash_volatile_lease_read(&flash, &lease, 15, 1, &byte));
	CHECK(read_calls == 1 && callback_offset == 15 && callback_size == 1);
	CHECK(spi_flash_volatile_lease_read(&flash, &lease, 16, 1, &byte) < 0);
	CHECK(spi_flash_volatile_lease_read(&flash, &lease, 17, 1, &byte) < 0);
	CHECK(spi_flash_volatile_lease_read(&flash, &lease, 15, 2, &byte) < 0);
	CHECK(spi_flash_volatile_lease_read(&flash, &lease, 8, overflow,
		&byte) < 0);
	CHECK(read_calls == 1);

	CHECK(!spi_flash_volatile_lease_write(&flash, &lease, 15, 1, &byte));
	CHECK(write_calls == 1 && callback_offset == 15 && callback_size == 1);
	CHECK(spi_flash_volatile_lease_write(&flash, &lease, 16, 1, &byte) < 0);
	CHECK(spi_flash_volatile_lease_write(&flash, &lease, 17, 1, &byte) < 0);
	CHECK(spi_flash_volatile_lease_write(&flash, &lease, 15, 2, &byte) < 0);
	CHECK(spi_flash_volatile_lease_write(&flash, &lease, 8, overflow,
		&byte) < 0);
	CHECK(write_calls == 1);

	CHECK(!spi_flash_volatile_lease_erase(&flash, &lease, 15, 1));
	CHECK(erase_calls == 1 && callback_offset == 15 && callback_size == 1);
	CHECK(spi_flash_volatile_lease_erase(&flash, &lease, 16, 1) < 0);
	CHECK(spi_flash_volatile_lease_erase(&flash, &lease, 17, 1) < 0);
	CHECK(spi_flash_volatile_lease_erase(&flash, &lease, 15, 2) < 0);
	CHECK(spi_flash_volatile_lease_erase(&flash, &lease, 8, overflow) < 0);
	CHECK(erase_calls == 1);

	CHECK(!spi_flash_volatile_lease_end(&lease));
}

static void failures_unwind(void)
{
	struct spi_flash_volatile_lease lease = { 0 };
	uint8_t byte = 0;

	reset_fixture();
	if (CONFIG(SPI_FLASH_HAS_VOLATILE_GROUP)) {
		begin_result = -7;
		CHECK(spi_flash_volatile_lease_begin(&flash, &lease) < 0);
		CHECK(!memcmp(&lease, &(struct spi_flash_volatile_lease){ 0 },
			sizeof(lease)));
		begin_result = 0;
	}
	CHECK(!spi_flash_volatile_lease_begin(&flash, &lease));
	operation_result = -9;
	CHECK(spi_flash_volatile_lease_write(&flash, &lease, 4, 1, &byte) == -9);
	operation_result = 0;
	end_result = CONFIG(SPI_FLASH_HAS_VOLATILE_GROUP) ? -3 : 0;
	CHECK(spi_flash_volatile_lease_end(&lease) ==
		(CONFIG(SPI_FLASH_HAS_VOLATILE_GROUP) ? -1 : 0));
	end_result = 0;
	CHECK(!spi_flash_write(&flash, 4, 1, &byte));
}

static void owner_mutation_fails_closed(void)
{
	struct spi_flash_volatile_lease lease = { 0 };
	uint8_t byte = 0;

	reset_fixture();
	mutated_lease = &lease;
	mutate_write_owner = true;
	CHECK(!spi_flash_volatile_lease_begin(&flash, &lease));
	CHECK(spi_flash_volatile_lease_write(&flash, &lease, 4, 1, &byte) < 0);
	CHECK(spi_flash_volatile_lease_read(&flash, &lease, 4, 1, &byte) < 0);
	CHECK(spi_flash_volatile_lease_end(&lease) < 0);
	CHECK(!spi_flash_write(&flash, 4, 1, &byte));
}

static void callback_mutation_fails_closed(void)
{
	struct spi_flash_volatile_lease lease = { 0 };
	uint8_t byte = 0;

	reset_fixture();
	mutate_write_ops = true;
	CHECK(!spi_flash_volatile_lease_begin(&flash, &lease));
	CHECK(spi_flash_volatile_lease_write(&flash, &lease, 4, 1, &byte) < 0);
	CHECK(spi_flash_volatile_lease_end(&lease) < 0);
	flash.ops = &flash_ops;
	CHECK(!spi_flash_write(&flash, 4, 1, &byte));

	if (CONFIG(SPI_FLASH_HAS_VOLATILE_GROUP)) {
		reset_fixture();
		mutated_lease = &lease;
		mutate_begin_owner = true;
		CHECK(spi_flash_volatile_lease_begin(&flash, &lease) < 0);
		CHECK(end_calls == 1);
		memset(&lease, 0, sizeof(lease));
		mutate_begin_owner = false;
		CHECK(!spi_flash_volatile_lease_begin(&flash, &lease));
		CHECK(!spi_flash_volatile_lease_end(&lease));

		reset_fixture();
		mutate_begin_ops = true;
		CHECK(spi_flash_volatile_lease_begin(&flash, &lease) < 0);
		CHECK(end_calls == 1);
		flash.ops = &flash_ops;
	}
}

struct contender {
	struct spi_flash_volatile_lease lease;
	uint32_t *ready;
	uint32_t *go;
	uint32_t *release;
	uint32_t won;
};

static void *contend(void *argument)
{
	struct contender *contender = argument;

	__atomic_add_fetch(contender->ready, 1, __ATOMIC_RELEASE);
	while (!__atomic_load_n(contender->go, __ATOMIC_ACQUIRE))
		sched_yield();
	if (!spi_flash_volatile_lease_begin(&flash, &contender->lease)) {
		__atomic_store_n(&contender->won, 1, __ATOMIC_RELEASE);
		while (!__atomic_load_n(contender->release, __ATOMIC_ACQUIRE))
			sched_yield();
		CHECK(!spi_flash_volatile_lease_end(&contender->lease));
	}
	return NULL;
}

static void concurrent_acquisition(void)
{
	struct contender contenders[2] = { 0 };
	pthread_t threads[2];
	uint32_t ready = 0;
	uint32_t go = 0;
	uint32_t release = 0;

	reset_fixture();
	for (size_t i = 0; i < 2; i++) {
		contenders[i].ready = &ready;
		contenders[i].go = &go;
		contenders[i].release = &release;
		CHECK(!pthread_create(&threads[i], NULL, contend, &contenders[i]));
	}
	while (__atomic_load_n(&ready, __ATOMIC_ACQUIRE) != 2)
		sched_yield();
	__atomic_store_n(&go, 1, __ATOMIC_RELEASE);
	while (__atomic_load_n(&contenders[0].won, __ATOMIC_ACQUIRE) +
	       __atomic_load_n(&contenders[1].won, __ATOMIC_ACQUIRE) == 0)
		sched_yield();
	for (unsigned int i = 0; i < 1000; i++)
		sched_yield();
	CHECK(__atomic_load_n(&contenders[0].won, __ATOMIC_ACQUIRE) +
		__atomic_load_n(&contenders[1].won, __ATOMIC_ACQUIRE) == 1);
	__atomic_store_n(&release, 1, __ATOMIC_RELEASE);
	for (size_t i = 0; i < 2; i++)
		CHECK(!pthread_join(threads[i], NULL));
}

int main(void)
{
	operations_and_peer_exclusion();
	ordinary_activity_excludes_lease();
	spans_are_bounded();
	failures_unwind();
	owner_mutation_fails_closed();
	callback_mutation_fails_closed();
	concurrent_acquisition();
	return 0;
}
