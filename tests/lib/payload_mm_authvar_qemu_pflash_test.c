/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar.h>
#include <boot/payload_mm_authvar_media.h>
#include <commonlib/region.h>
#include <emulation/qemu_pflash.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#ifndef BACKEND_SOURCE_INCLUDE
#define BACKEND_SOURCE_INCLUDE "../../src/lib/payload_mm_authvar_qemu_pflash.c"
#endif
#include BACKEND_SOURCE_INCLUDE

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

enum operation {
	OP_BEGIN = 1,
	OP_READ,
	OP_WRITE,
	OP_ERASE,
	OP_SYNC,
	OP_END,
};

static struct payload_mm_authvar_contract contract;
static struct payload_mm_authvar_media_port installed_port;
static struct mem_region_device root_device;
static struct region_device store;
static enum operation trace[32];
static size_t trace_count;
static size_t observed_offset;
static size_t observed_size;
static int lookup_failure;
static int lease_begin_failure;
static int lease_operation_failure;
static int lease_end_failure;
static int mutate_policy;
static int mutate_context;
static int lease_owned;
static pthread_barrier_t transaction_barrier;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static void record(enum operation operation)
{
	CHECK(trace_count < ARRAY_SIZE(trace));
	trace[trace_count++] = operation;
}

bool payload_mm_authvar_buffers_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_address = (uintptr_t)left;
	uintptr_t right_address = (uintptr_t)right;

	if (!left || !right || !left_size || !right_size ||
	    left_address > UINTPTR_MAX - (left_size - 1U) ||
	    right_address > UINTPTR_MAX - (right_size - 1U))
		return true;
	return left_address <= right_address + right_size - 1U &&
		right_address <= left_address + left_size - 1U;
}

bool payload_mm_authvar_smram_buffer(const void *buffer, size_t size)
{
	return buffer && size;
}

bool payload_mm_authvar_authority_snapshot(
	struct payload_mm_authvar_contract *output)
{
	*output = contract;
	return true;
}

enum cb_err payload_mm_authvar_media_install(
	const struct payload_mm_authvar_media_port *port)
{
	installed_port = *port;
	return CB_SUCCESS;
}

int smmstore_lookup_read_region(struct region_device *output)
{
	if (lookup_failure)
		return -1;
	*output = store;
	return 0;
}

const struct region_device *boot_device_rw(void)
{
	return &root_device.rdev;
}

int qemu_pflash_lease_begin(const struct region_device *active_root,
	struct qemu_pflash_lease *lease)
{
	record(OP_BEGIN);
	CHECK(active_root == &root_device.rdev);
	CHECK(!lease_owned);
	if (lease_begin_failure)
		return -1;
	lease_owned = 1;
	lease->private_data[0] = 1;
	if (mutate_policy)
		backend.policy.store_size++;
	return 0;
}

int qemu_pflash_lease_read(const struct region_device *active_root,
	const struct qemu_pflash_lease *lease, size_t offset, void *buffer,
	size_t size)
{
	record(OP_READ);
	CHECK(active_root == &root_device.rdev && lease_owned && lease->private_data[0]);
	observed_offset = offset;
	observed_size = size;
	memset(buffer, 0xa5, size);
	return lease_operation_failure ? -1 : 0;
}

int qemu_pflash_lease_program(const struct region_device *active_root,
	const struct qemu_pflash_lease *lease, size_t offset,
	const void *buffer, size_t size)
{
	record(OP_WRITE);
	CHECK(active_root == &root_device.rdev && lease_owned && lease->private_data[0]);
	CHECK(buffer);
	observed_offset = offset;
	observed_size = size;
	if (mutate_policy)
		backend.policy.store_offset++;
	if (mutate_context)
		backend.context.policy.store_offset++;
	return lease_operation_failure ? -1 : 0;
}

int qemu_pflash_lease_erase(const struct region_device *active_root,
	const struct qemu_pflash_lease *lease, size_t offset, size_t size)
{
	record(OP_ERASE);
	CHECK(active_root == &root_device.rdev && lease_owned && lease->private_data[0]);
	observed_offset = offset;
	observed_size = size;
	return lease_operation_failure ? -1 : 0;
}

int qemu_pflash_lease_sync(const struct region_device *active_root,
	const struct qemu_pflash_lease *lease)
{
	record(OP_SYNC);
	CHECK(active_root == &root_device.rdev && lease_owned && lease->private_data[0]);
	return lease_operation_failure ? -1 : 0;
}

int qemu_pflash_lease_end(struct qemu_pflash_lease *lease)
{
	record(OP_END);
	CHECK(lease_owned);
	lease_owned = 0;
	memset(lease, 0, sizeof(*lease));
	return lease_end_failure ? -1 : 0;
}

static void initialize(void)
{
	memset(&backend, 0, sizeof(backend));
	memset(&installed_port, 0, sizeof(installed_port));
	memset(&root_device, 0, sizeof(root_device));
	memset(&store, 0, sizeof(store));
	memset(trace, 0, sizeof(trace));
	trace_count = 0;
	observed_offset = 0;
	observed_size = 0;
	lookup_failure = 0;
	lease_begin_failure = 0;
	lease_operation_failure = 0;
	lease_end_failure = 0;
	mutate_policy = 0;
	mutate_context = 0;
	lease_owned = 0;
	root_device.rdev.region.size = 0x1000000U;
	store.region.offset = 0x0c00000U;
	store.region.size = 0x40000U;
	contract = (struct payload_mm_authvar_contract) {
		.revision = PAYLOAD_MM_AUTHVAR_REVISION,
		.size = sizeof(contract),
		.generation = 7,
		.boot_media_size = region_device_sz(&root_device.rdev),
		.store_offset = store.region.offset,
		.store_size = store.region.size,
		.block_size = SMM_BLOCK_SIZE,
		.erase_size = 0x1000U,
	};
}

static void install(void)
{
	CHECK(payload_mm_authvar_qemu_pflash_install() == CB_SUCCESS);
	CHECK(installed_port.begin && installed_port.read && installed_port.program &&
		installed_port.erase && installed_port.sync && installed_port.end);
}

static void test_transaction(void)
{
	uint8_t buffer[16] = { 0 };
	uint64_t generation = 0;
	size_t completed = 0;

	install();
	CHECK(installed_port.begin(installed_port.context, &generation) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	CHECK(generation == contract.generation);
	CHECK(installed_port.read(installed_port.context, 3, buffer,
		sizeof(buffer), &completed) == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	CHECK(completed == sizeof(buffer) && buffer[0] == 0xa5);
	CHECK(observed_offset == contract.store_offset + 3U);
	CHECK(installed_port.program(installed_port.context, 7, buffer,
		sizeof(buffer)) == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	CHECK(observed_offset == contract.store_offset + 7U);
	CHECK(installed_port.erase(installed_port.context, 0,
		contract.erase_size) == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	CHECK(observed_offset == contract.store_offset);
	CHECK(observed_size == contract.erase_size);
	CHECK(installed_port.sync(installed_port.context) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	CHECK(installed_port.end(installed_port.context) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	CHECK(!lease_owned);
	CHECK(trace_count == 6 && trace[0] == OP_BEGIN && trace[1] == OP_READ &&
		trace[2] == OP_WRITE && trace[3] == OP_ERASE &&
		trace[4] == OP_SYNC && trace[5] == OP_END);
}

static void test_geometry(const char *mode)
{
	if (!strcmp(mode, "geometry-offset"))
		contract.store_offset++;
	else if (!strcmp(mode, "geometry-size"))
		contract.store_size--;
	else if (!strcmp(mode, "geometry-media"))
		contract.boot_media_size--;
	else if (!strcmp(mode, "geometry-block"))
		contract.block_size /= 2U;
	else if (!strcmp(mode, "geometry-erase"))
		contract.erase_size *= 2U;
	else
		__builtin_trap();
	CHECK(payload_mm_authvar_qemu_pflash_install() == CB_ERR);
}

static void test_contention(void)
{
	uint64_t generation = 1;

	install();
	lease_begin_failure = 1;
	CHECK(installed_port.begin(installed_port.context, &generation) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(!lease_owned && generation == 0);
}

static void test_reentry(void)
{
	uint64_t generation;
	uint64_t second_generation = 1;

	install();
	CHECK(installed_port.begin(installed_port.context, &generation) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	CHECK(installed_port.begin(installed_port.context, &second_generation) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(trace_count == 1 && lease_owned && second_generation == 1);
	CHECK(installed_port.end(installed_port.context) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	CHECK(!lease_owned);
}

static void test_begin_mutation_cleanup(void)
{
	uint64_t generation = 1;

	install();
	mutate_policy = 1;
	CHECK(installed_port.begin(installed_port.context, &generation) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(!lease_owned && trace_count == 2 && trace[1] == OP_END);
}

static void test_alias(void)
{
	uint64_t generation;
	size_t completed = 1;

	install();
	CHECK(installed_port.begin(installed_port.context, &generation) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	CHECK(installed_port.read(installed_port.context, 0, &backend,
		1, &completed) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(trace_count == 1 && trace[0] == OP_BEGIN);
	CHECK(installed_port.end(installed_port.context) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(!lease_owned);
}

static void test_bounds(const char *mode)
{
	uint8_t bytes[2] = { 0 };
	uint64_t generation;
	size_t completed = 1;
	size_t operations;

	install();
	CHECK(installed_port.begin(installed_port.context, &generation) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	operations = trace_count;
	if (!strcmp(mode, "bounds-one-past"))
		CHECK(installed_port.read(installed_port.context,
			(uint32_t)contract.store_size, bytes, 1, &completed) ==
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	else if (!strcmp(mode, "bounds-crossing"))
		CHECK(installed_port.read(installed_port.context,
			(uint32_t)contract.store_size - 1U, bytes, sizeof(bytes),
			&completed) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	else if (!strcmp(mode, "bounds-overflow"))
		CHECK(installed_port.read(installed_port.context, 0, bytes,
			(size_t)UINT32_MAX + 1U, &completed) ==
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	else if (!strcmp(mode, "bounds-erase"))
		CHECK(installed_port.erase(installed_port.context, 1,
			contract.erase_size) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	else
		__builtin_trap();
	CHECK(trace_count == operations);
	/* A fresh process is used for every case; invalid use poisons the session. */
	CHECK(installed_port.end(installed_port.context) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(!lease_owned);
}

static void test_operation_failure(void)
{
	uint8_t byte = 0;
	uint64_t generation;

	install();
	CHECK(installed_port.begin(installed_port.context, &generation) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	lease_operation_failure = 1;
	CHECK(installed_port.program(installed_port.context, 0, &byte, 1) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(installed_port.sync(installed_port.context) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(installed_port.end(installed_port.context) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(!lease_owned);
}

static void test_callback_mutation(void)
{
	uint8_t byte = 0;
	uint64_t generation;

	install();
	CHECK(installed_port.begin(installed_port.context, &generation) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	mutate_policy = 1;
	CHECK(installed_port.program(installed_port.context, 0, &byte, 1) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(installed_port.end(installed_port.context) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(!lease_owned);
}

static void test_context_mutation(void)
{
	uint8_t byte = 0;
	uint64_t generation;

	install();
	CHECK(installed_port.begin(installed_port.context, &generation) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	mutate_context = 1;
	CHECK(installed_port.program(installed_port.context, 0, &byte, 1) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(installed_port.end(installed_port.context) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(!lease_owned);
}

static void test_close_failure(void)
{
	uint64_t generation;
	size_t operations;

	install();
	CHECK(installed_port.begin(installed_port.context, &generation) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	lease_end_failure = 1;
	CHECK(installed_port.end(installed_port.context) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(!lease_owned);
	operations = trace_count;
	CHECK(installed_port.begin(installed_port.context, &generation) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	CHECK(trace_count == operations);
}

struct concurrent_result {
	enum payload_mm_authvar_media_result begin;
	enum payload_mm_authvar_media_result end;
};

static void *concurrent_begin(void *argument)
{
	struct concurrent_result *result = argument;
	uint64_t generation;
	int barrier_result;

	result->begin = installed_port.begin(installed_port.context, &generation);
	barrier_result = pthread_barrier_wait(&transaction_barrier);
	CHECK(!barrier_result || barrier_result == PTHREAD_BARRIER_SERIAL_THREAD);
	if (result->begin == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result->end = installed_port.end(installed_port.context);
	return NULL;
}

static void test_concurrency(void)
{
	struct concurrent_result result[2] = { 0 };
	pthread_t thread[2];
	unsigned int success;
	int barrier_result;

	install();
	CHECK(!pthread_barrier_init(&transaction_barrier, NULL, 3));
	CHECK(!pthread_create(&thread[0], NULL, concurrent_begin, &result[0]));
	CHECK(!pthread_create(&thread[1], NULL, concurrent_begin, &result[1]));
	barrier_result = pthread_barrier_wait(&transaction_barrier);
	CHECK(!barrier_result || barrier_result == PTHREAD_BARRIER_SERIAL_THREAD);
	CHECK(!pthread_join(thread[0], NULL));
	CHECK(!pthread_join(thread[1], NULL));
	CHECK(!pthread_barrier_destroy(&transaction_barrier));
	success = (unsigned int)(result[0].begin ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) +
		(unsigned int)(result[1].begin ==
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	CHECK(success == 1);
	CHECK((result[0].begin == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR) !=
		(result[1].begin == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR));
}

int main(int argc, char **argv)
{
	CHECK(argc == 2);
	initialize();
	if (!strcmp(argv[1], "transaction"))
		test_transaction();
	else if (!strncmp(argv[1], "geometry-", 9))
		test_geometry(argv[1]);
	else if (!strcmp(argv[1], "contention"))
		test_contention();
	else if (!strcmp(argv[1], "reentry"))
		test_reentry();
	else if (!strcmp(argv[1], "begin-mutation"))
		test_begin_mutation_cleanup();
	else if (!strcmp(argv[1], "alias"))
		test_alias();
	else if (!strncmp(argv[1], "bounds-", 7))
		test_bounds(argv[1]);
	else if (!strcmp(argv[1], "operation-failure"))
		test_operation_failure();
	else if (!strcmp(argv[1], "callback-mutation"))
		test_callback_mutation();
	else if (!strcmp(argv[1], "context-mutation"))
		test_context_mutation();
	else if (!strcmp(argv[1], "close-failure"))
		test_close_failure();
	else if (!strcmp(argv[1], "concurrency"))
		test_concurrency();
	else
		abort();
	return 0;
}
