/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_s3_record.h>
#include <bootmem.h>
#include <commonlib/helpers.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) __builtin_trap(); } while (0)

#define STORAGE_SIZE 4096U

static uint8_t storage[STORAGE_SIZE] __aligned(8);

enum test_record_state {
	TEST_RECORD_CLEARED = 1,
	TEST_RECORD_ACTIVE,
	TEST_RECORD_SUSPENDED,
	TEST_RECORD_REARMING,
	TEST_RECORD_UPDATING,
	TEST_RECORD_POISONED,
};

struct test_record {
	uint32_t magic;
	uint32_t revision;
	uint32_t size;
	uint32_t reserved;
	uint32_t state_pair;
	uint32_t state_reserved;
	uint64_t sequence;
	uint64_t sequence_inverse;
	struct payload_mm_authvar_presence_s3_facts facts;
	struct payload_mm_authvar_presence_s3_facts facts_mirror;
	uint32_t digest;
	uint32_t digest_inverse;
} __aligned(8);

_Static_assert(sizeof(struct test_record) == 464, "test record layout");

static const struct payload_mm_authvar_presence_s3_facts valid_facts = {
	.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_S3_FACTS_REVISION,
	.size = sizeof(valid_facts),
	.presence_endpoint = {
		.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT,
		.size = sizeof(struct lb_authvar_presence_endpoint),
		.revision = LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION,
		.header_size = sizeof(struct lb_authvar_presence_endpoint),
		.flags = LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS,
		.generation = 0x1020304050607080ULL,
		.communication_base = 0x120000U,
		.communication_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
		.message_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
		.transport = LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8,
		.trigger_width = sizeof(uint8_t),
		.trigger_address = 0xb2U,
		.trigger_value = 0xe1U,
		.action_scope = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
		.capability_size = LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE,
	},
	.presence_backing = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION,
		.size = sizeof(struct payload_mm_authvar_presence_backing),
		.base = 0x120000U,
		.bytes = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE,
		.generation = 0x1020304050607080ULL,
		.tag = BM_MEM_RESERVED,
	},
	.close_endpoint = {
		.tag = LB_TAG_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT,
		.size = sizeof(struct lb_authvar_presence_lifecycle_close_endpoint),
		.revision = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT_REVISION,
		.header_size = sizeof(struct lb_authvar_presence_lifecycle_close_endpoint),
		.flags = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_REQUIRED_FLAGS,
		.generation = 0x1020304050607080ULL,
		.communication_base = 0x121000U,
		.communication_size = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
		.message_size = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
		.transport = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT_APM_IO8,
		.trigger_width = sizeof(uint8_t),
		.trigger_address = 0xb2U,
		.trigger_value = 0xfeU,
		.source_mask = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_SOURCE_MASK,
	},
	.close_backing_base = 0x121000U,
	.close_backing_bytes = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE,
	.close_backing_tag = BM_MEM_RESERVED,
	.presence_terminal = PAYLOAD_MM_AUTHVAR_PRESENCE_S3_CLOSED,
	.lifecycle_close_state = PAYLOAD_MM_AUTHVAR_PRESENCE_S3_LIFECYCLE_CLOSE_IDLE,
};

static bool all_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}

static void activate_and_seal(void)
{
	assert(payload_mm_authvar_presence_s3_record_cold_clear(storage,
		STORAGE_SIZE) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_record_cold_activate(storage,
		STORAGE_SIZE, &valid_facts) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_record_suspend_seal(storage,
		STORAGE_SIZE, &valid_facts) == CB_SUCCESS);
}

static struct test_record *test_record(void)
{
	return (struct test_record *)storage;
}

static void test_linear_cycles(void)
{
	struct payload_mm_authvar_presence_s3_facts borrowed;

	memset(storage, 0xa5, sizeof(storage));
	assert(payload_mm_authvar_presence_s3_record_cold_clear(storage,
		STORAGE_SIZE) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_record_size() < STORAGE_SIZE);
	assert(all_zero(storage + payload_mm_authvar_presence_s3_record_size(),
		STORAGE_SIZE - payload_mm_authvar_presence_s3_record_size()));
	assert(payload_mm_authvar_presence_s3_record_cold_activate(storage,
		STORAGE_SIZE, &valid_facts) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_record_suspend_seal(storage,
		STORAGE_SIZE, &valid_facts) == CB_SUCCESS);
	memset(&borrowed, 0xa5, sizeof(borrowed));
	assert(payload_mm_authvar_presence_s3_record_resume_borrow(storage,
		STORAGE_SIZE, &borrowed) == CB_SUCCESS);
	assert(!memcmp(&borrowed, &valid_facts, sizeof(borrowed)));
	assert(payload_mm_authvar_presence_s3_record_rearm_commit(storage,
		STORAGE_SIZE, &borrowed) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_record_suspend_seal(storage,
		STORAGE_SIZE, &valid_facts) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_record_resume_borrow(storage,
		STORAGE_SIZE, &borrowed) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_record_rearm_commit(storage,
		STORAGE_SIZE, &borrowed) == CB_SUCCESS);
}

static void test_invalid_terminal_facts_poison(void)
{
	struct payload_mm_authvar_presence_s3_facts changed = valid_facts;

	assert(payload_mm_authvar_presence_s3_record_cold_clear(storage,
		STORAGE_SIZE) == CB_SUCCESS);
	changed.presence_terminal = 0;
	assert(payload_mm_authvar_presence_s3_record_cold_activate(storage,
		STORAGE_SIZE, &changed) == CB_ERR);
	assert(payload_mm_authvar_presence_s3_record_cold_activate(storage,
		STORAGE_SIZE, &valid_facts) == CB_ERR);
}

static void activate_must_reject(struct payload_mm_authvar_presence_s3_facts *changed)
{
	assert(payload_mm_authvar_presence_s3_record_cold_clear(storage,
		STORAGE_SIZE) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_record_cold_activate(storage,
		STORAGE_SIZE, changed) == CB_ERR);
	assert(payload_mm_authvar_presence_s3_record_state_test(storage,
		STORAGE_SIZE) == TEST_RECORD_POISONED);
}

static void test_mailbox_and_backing_dimensions(void)
{
	struct payload_mm_authvar_presence_s3_facts changed = valid_facts;

	changed.presence_endpoint.communication_size =
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE;
	activate_must_reject(&changed);
	changed = valid_facts;
	changed.presence_endpoint.message_size--;
	activate_must_reject(&changed);
	changed = valid_facts;
	changed.presence_backing.bytes = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE;
	activate_must_reject(&changed);
	changed = valid_facts;
	changed.presence_endpoint.communication_base += sizeof(uint64_t);
	changed.presence_backing.base = changed.presence_endpoint.communication_base;
	activate_must_reject(&changed);
	changed = valid_facts;
	changed.close_endpoint.communication_size =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE;
	activate_must_reject(&changed);
	changed = valid_facts;
	changed.close_endpoint.message_size--;
	activate_must_reject(&changed);
	changed = valid_facts;
	changed.close_backing_bytes =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE;
	activate_must_reject(&changed);
	changed = valid_facts;
	changed.close_endpoint.communication_base =
		changed.presence_endpoint.communication_base;
	changed.close_backing_base = changed.close_endpoint.communication_base;
	activate_must_reject(&changed);
}

static void test_exact_identity_required(void)
{
	struct payload_mm_authvar_presence_s3_facts borrowed;
	struct payload_mm_authvar_presence_s3_facts changed;

	activate_and_seal();
	assert(payload_mm_authvar_presence_s3_record_resume_borrow(storage,
		STORAGE_SIZE, &borrowed) == CB_SUCCESS);
	changed = borrowed;
	changed.close_endpoint.trigger_value ^= 1U;
	assert(payload_mm_authvar_presence_s3_record_rearm_commit(storage,
		STORAGE_SIZE, &changed) == CB_ERR);
	assert(payload_mm_authvar_presence_s3_record_rearm_commit(storage,
		STORAGE_SIZE, &borrowed) == CB_ERR);
}

static void test_corruption_poison(void)
{
	struct payload_mm_authvar_presence_s3_facts borrowed;
	const size_t offsets[] = {
		offsetof(struct test_record, state_reserved),
		offsetof(struct test_record, state_pair),
		offsetof(struct test_record, state_pair) + sizeof(uint16_t),
		offsetof(struct test_record, sequence),
		offsetof(struct test_record, sequence_inverse),
		offsetof(struct test_record, facts) +
			offsetof(struct payload_mm_authvar_presence_s3_facts,
			close_backing_base),
		offsetof(struct test_record, facts_mirror) +
			offsetof(struct payload_mm_authvar_presence_s3_facts,
			close_backing_base),
		offsetof(struct test_record, digest),
		offsetof(struct test_record, digest_inverse),
	};

	for (size_t index = 0; index < ARRAY_SIZE(offsets); index++) {
		activate_and_seal();
		storage[offsets[index]] ^= 1U;
		memset(&borrowed, 0xa5, sizeof(borrowed));
		assert(payload_mm_authvar_presence_s3_record_resume_borrow(storage,
			STORAGE_SIZE, &borrowed) == CB_ERR);
		assert(all_zero(&borrowed, sizeof(borrowed)));
		assert(payload_mm_authvar_presence_s3_record_state_test(storage,
			STORAGE_SIZE) == TEST_RECORD_POISONED);
	}
}

static struct payload_mm_authvar_presence_s3_facts mutable_facts;
static struct payload_mm_authvar_presence_s3_facts nested_facts;
static enum payload_mm_authvar_presence_s3_record_test_point hook_point;
static bool hook_reenter;

static void mutation_hook(enum payload_mm_authvar_presence_s3_record_test_point point)
{
	if (point != hook_point)
		return;
	payload_mm_authvar_presence_s3_record_test_hook(NULL);
	if (hook_reenter) {
		if (point == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_COLD_ACTIVATE_CLAIMED)
			(void)payload_mm_authvar_presence_s3_record_cold_activate(storage,
				STORAGE_SIZE, &valid_facts);
		else if (point == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_SUSPEND_CLAIMED)
			(void)payload_mm_authvar_presence_s3_record_suspend_seal(storage,
				STORAGE_SIZE, &valid_facts);
		else if (point == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_BORROW_CLAIMED)
			(void)payload_mm_authvar_presence_s3_record_resume_borrow(storage,
				STORAGE_SIZE, &nested_facts);
		else
			(void)payload_mm_authvar_presence_s3_record_rearm_commit(storage,
				STORAGE_SIZE, &nested_facts);
		return;
	}
	if (point == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_BORROW_CLAIMED)
		test_record()->facts_mirror.close_backing_base ^= 1U;
	else
		mutable_facts.close_backing_base ^= 1U;
}

static void install_hook(enum payload_mm_authvar_presence_s3_record_test_point point,
	bool reenter)
{
	hook_point = point;
	hook_reenter = reenter;
	payload_mm_authvar_presence_s3_record_test_hook(mutation_hook);
}

static void test_input_mutation_and_record_mutation(void)
{
	struct payload_mm_authvar_presence_s3_facts borrowed;

	mutable_facts = valid_facts;
	assert(payload_mm_authvar_presence_s3_record_cold_clear(storage,
		STORAGE_SIZE) == CB_SUCCESS);
	install_hook(PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_COLD_ACTIVATE_CLAIMED, false);
	assert(payload_mm_authvar_presence_s3_record_cold_activate(storage,
		STORAGE_SIZE, &mutable_facts) == CB_ERR);
	assert(payload_mm_authvar_presence_s3_record_state_test(storage,
		STORAGE_SIZE) == TEST_RECORD_POISONED);

	assert(payload_mm_authvar_presence_s3_record_cold_clear(storage,
		STORAGE_SIZE) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_record_cold_activate(storage,
		STORAGE_SIZE, &valid_facts) == CB_SUCCESS);
	mutable_facts = valid_facts;
	install_hook(PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_SUSPEND_CLAIMED, false);
	assert(payload_mm_authvar_presence_s3_record_suspend_seal(storage,
		STORAGE_SIZE, &mutable_facts) == CB_ERR);

	activate_and_seal();
	memset(&borrowed, 0xa5, sizeof(borrowed));
	install_hook(PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_BORROW_CLAIMED, false);
	assert(payload_mm_authvar_presence_s3_record_resume_borrow(storage,
		STORAGE_SIZE, &borrowed) == CB_ERR);
	assert(all_zero(&borrowed, sizeof(borrowed)));

	activate_and_seal();
	assert(payload_mm_authvar_presence_s3_record_resume_borrow(storage,
		STORAGE_SIZE, &mutable_facts) == CB_SUCCESS);
	install_hook(PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_REARM_CLAIMED, false);
	assert(payload_mm_authvar_presence_s3_record_rearm_commit(storage,
		STORAGE_SIZE, &mutable_facts) == CB_ERR);
	payload_mm_authvar_presence_s3_record_test_hook(NULL);
}

static void test_reentry_poison(void)
{
	struct payload_mm_authvar_presence_s3_facts borrowed;

	assert(payload_mm_authvar_presence_s3_record_cold_clear(storage,
		STORAGE_SIZE) == CB_SUCCESS);
	install_hook(PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_COLD_ACTIVATE_CLAIMED, true);
	assert(payload_mm_authvar_presence_s3_record_cold_activate(storage,
		STORAGE_SIZE, &valid_facts) == CB_ERR);

	assert(payload_mm_authvar_presence_s3_record_cold_clear(storage,
		STORAGE_SIZE) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_record_cold_activate(storage,
		STORAGE_SIZE, &valid_facts) == CB_SUCCESS);
	install_hook(PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_SUSPEND_CLAIMED, true);
	assert(payload_mm_authvar_presence_s3_record_suspend_seal(storage,
		STORAGE_SIZE, &valid_facts) == CB_ERR);

	activate_and_seal();
	install_hook(PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_BORROW_CLAIMED, true);
	assert(payload_mm_authvar_presence_s3_record_resume_borrow(storage,
		STORAGE_SIZE, &borrowed) == CB_ERR);
	assert(all_zero(&borrowed, sizeof(borrowed)));

	activate_and_seal();
	assert(payload_mm_authvar_presence_s3_record_resume_borrow(storage,
		STORAGE_SIZE, &borrowed) == CB_SUCCESS);
	nested_facts = borrowed;
	install_hook(PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_REARM_CLAIMED, true);
	assert(payload_mm_authvar_presence_s3_record_rearm_commit(storage,
		STORAGE_SIZE, &borrowed) == CB_ERR);
	assert(payload_mm_authvar_presence_s3_record_state_test(storage,
		STORAGE_SIZE) == TEST_RECORD_POISONED);
	payload_mm_authvar_presence_s3_record_test_hook(NULL);
}

struct borrow_thread {
	struct payload_mm_authvar_presence_s3_facts facts;
	enum cb_err status;
};

static void *borrow_worker(void *opaque)
{
	struct borrow_thread *thread = opaque;

	thread->status = payload_mm_authvar_presence_s3_record_resume_borrow(
		storage, STORAGE_SIZE, &thread->facts);
	return NULL;
}

static void test_ambiguous_borrow_poison(void)
{
	pthread_t ids[2];
	struct borrow_thread threads[2] = { 0 };
	unsigned int successes = 0;

	activate_and_seal();
	assert(!pthread_create(&ids[0], NULL, borrow_worker, &threads[0]));
	assert(!pthread_create(&ids[1], NULL, borrow_worker, &threads[1]));
	assert(!pthread_join(ids[0], NULL));
	assert(!pthread_join(ids[1], NULL));
	for (size_t index = 0; index < 2; index++) {
		if (threads[index].status == CB_SUCCESS)
			successes++;
		else
			assert(all_zero(&threads[index].facts,
				sizeof(threads[index].facts)));
	}
	assert(successes <= 1U);
	assert(payload_mm_authvar_presence_s3_record_rearm_commit(storage,
		STORAGE_SIZE, &valid_facts) == CB_ERR);
}

static pthread_mutex_t writer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t writer_condition = PTHREAD_COND_INITIALIZER;
static enum payload_mm_authvar_presence_s3_record_test_point writer_point;
static bool writer_waiting;
static bool writer_release;

static void writer_hook(enum payload_mm_authvar_presence_s3_record_test_point point)
{
	assert(!pthread_mutex_lock(&writer_lock));
	if (point != writer_point) {
		assert(!pthread_mutex_unlock(&writer_lock));
		return;
	}
	writer_waiting = true;
	assert(!pthread_cond_broadcast(&writer_condition));
	while (!writer_release)
		assert(!pthread_cond_wait(&writer_condition, &writer_lock));
	assert(!pthread_mutex_unlock(&writer_lock));
}

struct writer_thread {
	struct payload_mm_authvar_presence_s3_facts facts;
	bool rearm;
	enum cb_err status;
};

static void *writer_worker(void *opaque)
{
	struct writer_thread *thread = opaque;

	if (thread->rearm)
		thread->status = payload_mm_authvar_presence_s3_record_rearm_commit(
			storage, STORAGE_SIZE, &thread->facts);
	else
		thread->status = payload_mm_authvar_presence_s3_record_suspend_seal(
			storage, STORAGE_SIZE, &thread->facts);
	return NULL;
}

static void race_writer_transition(bool rearm)
{
	pthread_t id;
	struct writer_thread winner = {
		.facts = valid_facts,
		.rearm = rearm,
	};
	struct writer_thread loser = winner;
	struct payload_mm_authvar_presence_s3_facts borrowed;

	activate_and_seal();
	if (rearm) {
		assert(payload_mm_authvar_presence_s3_record_resume_borrow(storage,
			STORAGE_SIZE, &borrowed) == CB_SUCCESS);
		winner.facts = borrowed;
		loser.facts = borrowed;
	} else {
		assert(payload_mm_authvar_presence_s3_record_resume_borrow(storage,
			STORAGE_SIZE, &borrowed) == CB_SUCCESS);
		assert(payload_mm_authvar_presence_s3_record_rearm_commit(storage,
			STORAGE_SIZE, &borrowed) == CB_SUCCESS);
	}

	assert(!pthread_mutex_lock(&writer_lock));
	writer_point = rearm ? PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_REARM_CLAIMED :
		PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_SUSPEND_CLAIMED;
	writer_waiting = false;
	writer_release = false;
	payload_mm_authvar_presence_s3_record_test_hook(writer_hook);
	assert(!pthread_mutex_unlock(&writer_lock));
	assert(!pthread_create(&id, NULL, writer_worker, &winner));

	assert(!pthread_mutex_lock(&writer_lock));
	while (!writer_waiting)
		assert(!pthread_cond_wait(&writer_condition, &writer_lock));
	assert(!pthread_mutex_unlock(&writer_lock));
	(void)writer_worker(&loser);
	assert(loser.status == CB_ERR);

	assert(!pthread_mutex_lock(&writer_lock));
	writer_release = true;
	assert(!pthread_cond_broadcast(&writer_condition));
	assert(!pthread_mutex_unlock(&writer_lock));
	assert(!pthread_join(id, NULL));
	assert(winner.status == CB_ERR);
	assert(payload_mm_authvar_presence_s3_record_state_test(storage,
		STORAGE_SIZE) == TEST_RECORD_POISONED);
	payload_mm_authvar_presence_s3_record_test_hook(NULL);
}

static void test_ambiguous_writer_transitions_poison(void)
{
	race_writer_transition(false);
	race_writer_transition(true);
}

static void test_bounds_and_explicit_poison(void)
{
	struct payload_mm_authvar_presence_s3_facts borrowed;
	struct payload_mm_authvar_presence_s3_facts alias_copy;
	struct payload_mm_authvar_presence_s3_facts *alias;
	const uintptr_t wrapping = UINTPTR_MAX &
		~((uintptr_t)_Alignof(struct payload_mm_authvar_presence_s3_facts) - 1U);

	assert(payload_mm_authvar_presence_s3_record_cold_clear(NULL,
		STORAGE_SIZE) == CB_ERR);
	assert(payload_mm_authvar_presence_s3_record_cold_clear(storage + 1,
		STORAGE_SIZE - 1U) == CB_ERR);
	assert(payload_mm_authvar_presence_s3_record_cold_clear(storage,
		payload_mm_authvar_presence_s3_record_size() - 1U) == CB_ERR);
	assert(payload_mm_authvar_presence_s3_record_cold_clear(storage,
		STORAGE_SIZE) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_record_cold_activate(storage,
		STORAGE_SIZE,
		(const struct payload_mm_authvar_presence_s3_facts *)wrapping) == CB_ERR);

	activate_and_seal();
	alias = (struct payload_mm_authvar_presence_s3_facts *)&test_record()->facts;
	alias_copy = *alias;
	assert(payload_mm_authvar_presence_s3_record_resume_borrow(storage,
		STORAGE_SIZE, alias) == CB_ERR);
	assert(!memcmp(alias, &alias_copy, sizeof(alias_copy)));

	activate_and_seal();
	payload_mm_authvar_presence_s3_record_poison(storage, STORAGE_SIZE);
	memset(&borrowed, 0xa5, sizeof(borrowed));
	assert(payload_mm_authvar_presence_s3_record_resume_borrow(storage,
		STORAGE_SIZE, &borrowed) == CB_ERR);
	assert(all_zero(&borrowed, sizeof(borrowed)));
}

int main(void)
{
	test_linear_cycles();
	test_invalid_terminal_facts_poison();
	test_mailbox_and_backing_dimensions();
	test_exact_identity_required();
	test_corruption_poison();
	test_input_mutation_and_record_mutation();
	test_reentry_poison();
	test_ambiguous_borrow_poison();
	test_ambiguous_writer_transitions_poison();
	test_bounds_and_explicit_poison();
	return 0;
}
