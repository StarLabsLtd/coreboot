/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cpu/x86/smm_invocation_loader_instance.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

enum hook_action {
	HOOK_NONE,
	HOOK_MUTATE_INSTANCE,
	HOOK_MUTATE_INSTANCE_LOW,
	HOOK_MUTATE_INSTANCE_HIGH,
	HOOK_MUTATE_STATE,
	HOOK_MUTATE_SEED_LOW,
	HOOK_MUTATE_SEED_HIGH,
	HOOK_MUTATE_READ,
};

static enum hook_action action;
static struct smm_invocation_loader_instance *hook_instance;
static struct smm_invocation_loader_instance_seed *hook_seed;
static uint32_t hook_wait;
static uint32_t hook_seen;

void smm_invocation_loader_instance_test_hook(uint32_t point)
{
	if (point == 1U && action == HOOK_MUTATE_INSTANCE)
		hook_instance->reserved = 1U;
	if (point == 1U && action == HOOK_MUTATE_INSTANCE_LOW)
		hook_instance->loader_instance_nonce.low++;
	if (point == 1U && action == HOOK_MUTATE_INSTANCE_HIGH)
		hook_instance->loader_instance_nonce.high++;
	if (point == 1U && action == HOOK_MUTATE_STATE)
		hook_instance->state = SMM_INVOCATION_LOADER_INSTANCE_EMPTY;
	if (point == 1U && action == HOOK_MUTATE_SEED_LOW)
		hook_seed->loader_instance_nonce.low++;
	if (point == 1U && action == HOOK_MUTATE_SEED_HIGH)
		hook_seed->loader_instance_nonce.high++;
	if (point == 3U && action == HOOK_MUTATE_READ)
		hook_instance->loader_instance_nonce.low++;
	if (point == 2U && __atomic_load_n(&hook_wait, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&hook_seen, 1U, __ATOMIC_RELEASE);
		while (__atomic_load_n(&hook_wait, __ATOMIC_ACQUIRE))
			sched_yield();
	}
}

static struct smm_invocation_loader_instance_seed seed(uint64_t low,
	uint64_t high, uint32_t lifecycle)
{
	return (struct smm_invocation_loader_instance_seed) {
		.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION,
		.size = sizeof(struct smm_invocation_loader_instance_seed),
		.lifecycle = lifecycle,
		.loader_instance_nonce = { .low = low, .high = high },
	};
}

static void assert_empty(const struct smm_invocation_loader_instance *instance)
{
	const struct smm_invocation_loader_instance empty = { 0 };

	assert(!memcmp(instance, &empty, sizeof(empty)));
}

static void test_publish_read_reload(void)
{
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_loader_instance snapshot;
	struct smm_invocation_loader_instance_seed first = seed(7, 17,
		SMM_INVOCATION_LOADER_NON_S3_LOAD);
	struct smm_invocation_loader_instance_seed second = seed(11, 21,
		SMM_INVOCATION_LOADER_S3_RELOAD);

	memset(&instance, 0, sizeof(instance));
	assert(smm_invocation_loader_instance_publish(&instance, &first) == CB_SUCCESS);
	assert(smm_invocation_loader_instance_read(&instance, &snapshot) == CB_SUCCESS);
	assert(snapshot.state == SMM_INVOCATION_LOADER_INSTANCE_READY);
	assert(snapshot.revision == SMM_INVOCATION_LOADER_INSTANCE_REVISION);
	assert(snapshot.size == sizeof(snapshot));
	assert(snapshot.lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD);
	assert(snapshot.loader_instance_nonce.low == 7U);
	assert(snapshot.loader_instance_nonce.high == 17U);
	assert(snapshot.reserved == 0U);

	assert(smm_invocation_loader_instance_publish(&instance, &second) == CB_ERR);
	assert(!memcmp(&instance, &snapshot, sizeof(instance)));
	assert(smm_invocation_loader_instance_loader_result(&instance, 0) == 0);
	assert(!memcmp(&instance, &snapshot, sizeof(instance)));
	smm_invocation_loader_instance_scrub(&instance);
	assert(smm_invocation_loader_instance_publish(&instance, &second) == CB_SUCCESS);
	assert(smm_invocation_loader_instance_read(&instance, &snapshot) == CB_SUCCESS);
	assert(snapshot.lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD);
	assert(snapshot.loader_instance_nonce.low == 11U);
	assert(snapshot.loader_instance_nonce.high == 21U);
	assert(smm_invocation_loader_instance_loader_result(&instance, -1) == -1);
	assert_empty(&instance);
}

static void test_invalid_inputs_scrub(void)
{
	union {
		uint64_t alignment;
		uint8_t bytes[sizeof(struct smm_invocation_loader_instance) + 1U];
	} unaligned;
	union {
		uint64_t alignment;
		uint8_t bytes[sizeof(struct smm_invocation_loader_instance) +
			sizeof(struct smm_invocation_loader_instance_seed)];
	} overlap;
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_loader_instance snapshot;
	struct smm_invocation_loader_instance_seed input = seed(1, 2,
		SMM_INVOCATION_LOADER_NON_S3_LOAD);

#define REJECT(field, value) do { \
	memset(&instance, 0, sizeof(instance)); \
	input = seed(1, 2, SMM_INVOCATION_LOADER_NON_S3_LOAD); \
	input.field = (value); \
	assert(smm_invocation_loader_instance_publish(&instance, &input) != CB_SUCCESS); \
	assert_empty(&instance); \
} while (0)
	REJECT(revision, 2U);
	REJECT(size, 0U);
	REJECT(lifecycle, 0U);
	REJECT(lifecycle, 3U);
	REJECT(reserved, 1U);
#undef REJECT
	input = seed(0, 0, SMM_INVOCATION_LOADER_NON_S3_LOAD);
	assert(smm_invocation_loader_instance_publish(&instance, &input) == CB_ERR);
	assert_empty(&instance);
	input = seed(1, 0, SMM_INVOCATION_LOADER_NON_S3_LOAD);
	assert(smm_invocation_loader_instance_publish(&instance, &input) == CB_SUCCESS);
	smm_invocation_loader_instance_scrub(&instance);
	input = seed(0, 1, SMM_INVOCATION_LOADER_NON_S3_LOAD);
	assert(smm_invocation_loader_instance_publish(&instance, &input) == CB_SUCCESS);
	smm_invocation_loader_instance_scrub(&instance);

	memset(&instance, 0, sizeof(instance));
	assert(smm_invocation_loader_instance_publish(&instance,
		(const struct smm_invocation_loader_instance_seed *)&instance) == CB_ERR_ARG);
	assert_empty(&instance);
	memset(&snapshot, 0xa5, sizeof(snapshot));
	assert(smm_invocation_loader_instance_read(&instance, &snapshot) == CB_ERR);
	assert_empty(&snapshot);
	assert(smm_invocation_loader_instance_read(&instance, &instance) ==
		CB_ERR_ARG);
	assert(smm_invocation_loader_instance_publish(
		(struct smm_invocation_loader_instance *)&unaligned.bytes[1],
		&input) == CB_ERR_ARG);
	assert(smm_invocation_loader_instance_publish(&instance,
		(const struct smm_invocation_loader_instance_seed *)
			&unaligned.bytes[1]) == CB_ERR_ARG);
	assert(smm_invocation_loader_instance_read(&instance,
		(struct smm_invocation_loader_instance *)&unaligned.bytes[1]) ==
		CB_ERR_ARG);
	assert(smm_invocation_loader_instance_publish(
		(void *)(UINTPTR_MAX - sizeof(instance) + 2U), &input) ==
		CB_ERR_ARG);
	memset(&overlap, 0, sizeof(overlap));
	assert(smm_invocation_loader_instance_publish(
		(struct smm_invocation_loader_instance *)&overlap.bytes[0],
		(const struct smm_invocation_loader_instance_seed *)
			&overlap.bytes[8]) == CB_ERR_ARG);
	assert(smm_invocation_loader_instance_publish(
		(struct smm_invocation_loader_instance *)&overlap.bytes[8],
		(const struct smm_invocation_loader_instance_seed *)
			&overlap.bytes[0]) == CB_ERR_ARG);
}

static void test_read_rejects_corruption(void)
{
	struct smm_invocation_loader_instance instance = { 0 };
	struct smm_invocation_loader_instance snapshot;
	struct smm_invocation_loader_instance valid;
	struct smm_invocation_loader_instance_seed input = seed(43, 53,
		SMM_INVOCATION_LOADER_NON_S3_LOAD);

	assert(smm_invocation_loader_instance_publish(&instance, &input) ==
		CB_SUCCESS);
	valid = instance;
#define CORRUPT(field, value) do { \
	instance = valid; \
	instance.field = (value); \
	memset(&snapshot, 0xa5, sizeof(snapshot)); \
	assert(smm_invocation_loader_instance_read(&instance, &snapshot) == CB_ERR); \
	assert_empty(&snapshot); \
} while (0)
	CORRUPT(state, SMM_INVOCATION_LOADER_INSTANCE_PROVISIONING);
	CORRUPT(revision, 2U);
	CORRUPT(size, 0U);
	CORRUPT(lifecycle, 0U);
	CORRUPT(reserved, 1U);
#undef CORRUPT
	instance = valid;
	instance.loader_instance_nonce =
		(struct smm_invocation_loader_instance_nonce) { 0 };
	memset(&snapshot, 0xa5, sizeof(snapshot));
	assert(smm_invocation_loader_instance_read(&instance, &snapshot) == CB_ERR);
	assert_empty(&snapshot);
}

static void test_dirty_and_failed_claims(void)
{
	struct smm_invocation_loader_instance instance = { .reserved = 1U };
	struct smm_invocation_loader_instance before;
	struct smm_invocation_loader_instance_seed valid = seed(13, 23,
		SMM_INVOCATION_LOADER_NON_S3_LOAD);
	struct smm_invocation_loader_instance_seed invalid = valid;

	assert(smm_invocation_loader_instance_publish(&instance, &valid) == CB_ERR);
	assert_empty(&instance);
	instance.reserved = 1U;
	before = instance;
	invalid.loader_instance_nonce = (struct smm_invocation_loader_instance_nonce) { 0 };
	assert(smm_invocation_loader_instance_publish(&instance, &invalid) == CB_ERR);
	assert(!memcmp(&instance, &before, sizeof(instance)));
	instance.state = SMM_INVOCATION_LOADER_INSTANCE_PROVISIONING;
	before = instance;
	assert(smm_invocation_loader_instance_publish(&instance, &valid) == CB_ERR);
	assert(!memcmp(&instance, &before, sizeof(instance)));
}

static void test_full_scrub(void)
{
	struct smm_invocation_loader_instance instance;

	memset(&instance, 0xa5, sizeof(instance));
	smm_invocation_loader_instance_scrub(&instance);
	assert_empty(&instance);
}

static void test_mutation(void)
{
	struct smm_invocation_loader_instance instance = { 0 };
	struct smm_invocation_loader_instance snapshot;
	struct smm_invocation_loader_instance_seed input = seed(19, 29,
		SMM_INVOCATION_LOADER_NON_S3_LOAD);

	hook_instance = &instance;
	hook_seed = &input;
	action = HOOK_MUTATE_INSTANCE;
	assert(smm_invocation_loader_instance_publish(&instance, &input) == CB_ERR);
	assert_empty(&instance);
	action = HOOK_MUTATE_INSTANCE_LOW;
	assert(smm_invocation_loader_instance_publish(&instance, &input) == CB_ERR);
	assert_empty(&instance);
	action = HOOK_MUTATE_INSTANCE_HIGH;
	assert(smm_invocation_loader_instance_publish(&instance, &input) == CB_ERR);
	assert_empty(&instance);
	action = HOOK_MUTATE_STATE;
	assert(smm_invocation_loader_instance_publish(&instance, &input) == CB_ERR);
	assert_empty(&instance);
	action = HOOK_MUTATE_SEED_LOW;
	assert(smm_invocation_loader_instance_publish(&instance, &input) == CB_ERR);
	assert_empty(&instance);
	input = seed(19, 29, SMM_INVOCATION_LOADER_NON_S3_LOAD);
	action = HOOK_MUTATE_SEED_HIGH;
	assert(smm_invocation_loader_instance_publish(&instance, &input) == CB_ERR);
	assert_empty(&instance);
	action = HOOK_NONE;
	input = seed(23, 33, SMM_INVOCATION_LOADER_NON_S3_LOAD);
	assert(smm_invocation_loader_instance_publish(&instance, &input) == CB_SUCCESS);
	action = HOOK_MUTATE_READ;
	assert(smm_invocation_loader_instance_read(&instance, &snapshot) == CB_ERR);
	assert_empty(&snapshot);
	action = HOOK_NONE;
}

struct concurrent_context {
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_loader_instance_seed seed;
};

static void *reader(void *argument)
{
	struct concurrent_context *context = argument;
	struct smm_invocation_loader_instance snapshot;

	while (__atomic_load_n(&context->instance.state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_LOADER_INSTANCE_READY)
		;
	assert(smm_invocation_loader_instance_read(&context->instance, &snapshot) ==
		CB_SUCCESS);
	assert(smm_invocation_loader_instance_nonce_equal(
		snapshot.loader_instance_nonce, context->seed.loader_instance_nonce));
	return NULL;
}

static void test_release_acquire(void)
{
	struct concurrent_context context = {
		.seed = seed(29, 39, SMM_INVOCATION_LOADER_NON_S3_LOAD),
	};
	pthread_t thread;

	assert(!pthread_create(&thread, NULL, reader, &context));
	assert(smm_invocation_loader_instance_publish(&context.instance, &context.seed) ==
		CB_SUCCESS);
	assert(!pthread_join(thread, NULL));
}

struct publisher_context {
	struct smm_invocation_loader_instance *instance;
	struct smm_invocation_loader_instance_seed seed;
	pthread_barrier_t *barrier;
	enum cb_err result;
};

static void *publisher(void *argument)
{
	struct publisher_context *context = argument;
	int result;

	result = pthread_barrier_wait(context->barrier);
	assert(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
	context->result = smm_invocation_loader_instance_publish(context->instance,
		&context->seed);
	return NULL;
}

static void test_single_publisher(void)
{
	struct smm_invocation_loader_instance instance = { 0 };
	pthread_barrier_t barrier;
	pthread_t threads[2];
	struct publisher_context contexts[2] = {
		{ .instance = &instance, .seed = seed(31, 41,
			SMM_INVOCATION_LOADER_NON_S3_LOAD),
		  .barrier = &barrier },
		{ .instance = &instance, .seed = seed(37, 47,
			SMM_INVOCATION_LOADER_S3_RELOAD), .barrier = &barrier },
	};

	assert(!pthread_barrier_init(&barrier, NULL, 2));
	assert(!pthread_create(&threads[0], NULL, publisher, &contexts[0]));
	assert(!pthread_create(&threads[1], NULL, publisher, &contexts[1]));
	assert(!pthread_join(threads[0], NULL));
	assert(!pthread_join(threads[1], NULL));
	assert(!pthread_barrier_destroy(&barrier));
	assert((contexts[0].result == CB_SUCCESS) !=
		(contexts[1].result == CB_SUCCESS));
	assert(instance.state == SMM_INVOCATION_LOADER_INSTANCE_READY);
	assert((instance.loader_instance_nonce.low == 31U &&
		instance.loader_instance_nonce.high == 41U) ||
	       (instance.loader_instance_nonce.low == 37U &&
		instance.loader_instance_nonce.high == 47U));
}

struct paused_publisher_context {
	struct smm_invocation_loader_instance *instance;
	struct smm_invocation_loader_instance_seed *seed;
	enum cb_err result;
};

static void *paused_publisher(void *argument)
{
	struct paused_publisher_context *context = argument;

	context->result = smm_invocation_loader_instance_publish(
		context->instance, context->seed);
	return NULL;
}

static void test_ready_published_last(void)
{
	struct smm_invocation_loader_instance instance = { 0 };
	struct smm_invocation_loader_instance snapshot;
	struct smm_invocation_loader_instance_seed input = seed(59, 61,
		SMM_INVOCATION_LOADER_NON_S3_LOAD);
	struct paused_publisher_context context = {
		.instance = &instance,
		.seed = &input,
	};
	pthread_t thread;

	hook_seen = 0;
	__atomic_store_n(&hook_wait, 1U, __ATOMIC_RELEASE);
	assert(!pthread_create(&thread, NULL, paused_publisher, &context));
	while (!__atomic_load_n(&hook_seen, __ATOMIC_ACQUIRE))
		sched_yield();
	assert(smm_invocation_loader_instance_read(&instance, &snapshot) == CB_ERR);
	assert_empty(&snapshot);
	__atomic_store_n(&hook_wait, 0U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(context.result == CB_SUCCESS);
	assert(smm_invocation_loader_instance_read(&instance, &snapshot) ==
		CB_SUCCESS);
}

int main(void)
{
	test_publish_read_reload();
	test_invalid_inputs_scrub();
	test_read_rejects_corruption();
	test_dirty_and_failed_claims();
	test_full_scrub();
	test_mutation();
	test_release_acquire();
	test_single_publisher();
	test_ready_published_last();
	return 0;
}
