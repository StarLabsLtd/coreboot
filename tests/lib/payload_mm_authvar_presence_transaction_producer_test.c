/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_transaction.h>
#include <bootmem.h>
#include <pthread.h>
#include <random.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

_Static_assert(PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_REVISION == 3U,
	"producer composition semantic revision changed");

static enum cb_err
publication_take_and_complete(struct lb_authvar_presence_endpoint *endpoint)
{
	struct payload_mm_authvar_presence_receipt receipt = { 0 };
	enum cb_err status;

	status = payload_mm_authvar_presence_producer_publication_take(&receipt);
	if (status == CB_SUCCESS) {
		*endpoint = receipt.endpoint;
		status = payload_mm_authvar_presence_producer_publication_complete(&receipt);
	}
	return status;
}

#define payload_mm_authvar_presence_producer_publication_take(endpoint) \
	publication_take_and_complete(endpoint)

static uint8_t backing[PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE]
	__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT);
static unsigned int random_calls;
static unsigned int prepare_calls;
static unsigned int commit_calls;
static unsigned int abort_calls;
static unsigned int authority_scrubs;
static bool prepared;
static bool committed;
static bool aborted;
static bool prepare_error;
static bool commit_error;
static bool bad_prepare_ack;
static bool block_commit;
static bool block_prepare;
static bool prepare_entered;
static bool prepare_release;
static bool commit_entered;
static bool commit_release;
static bool block_query;
static bool query_entered;
static bool query_release;
static bool before_prepare_entered;
static bool before_prepare_release;
static pthread_mutex_t commit_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t commit_cond = PTHREAD_COND_INITIALIZER;
static enum cb_err finalize_result;
static enum cb_err compose_result;

static void *compose_thread(void *unused);

int bootmem_aligned_reservation_register(
	const struct bootmem_aligned_reservation_request *request,
	struct bootmem_aligned_reservation_handle *handle)
{
	assert(request->tag == BM_MEM_RESERVED);
	*handle = (struct bootmem_aligned_reservation_handle) {
		.opaque = { 1U, 2U },
	};
	return 0;
}

int bootmem_aligned_reservation_query(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_aligned_reservation *reservation)
{
	assert(handle->opaque[0] == 1U && handle->opaque[1] == 2U);
	if (block_query && !query_entered) {
		assert(!pthread_mutex_lock(&commit_mutex));
		query_entered = true;
		assert(!pthread_cond_broadcast(&commit_cond));
		while (!query_release)
			assert(!pthread_cond_wait(&commit_cond, &commit_mutex));
		assert(!pthread_mutex_unlock(&commit_mutex));
	}
	*reservation = (struct bootmem_aligned_reservation) {
		.base = (uintptr_t)backing,
		.size = sizeof(backing),
		.tag = BM_MEM_RESERVED,
	};
	return 0;
}

enum cb_err get_random_number_64(uint64_t *value)
{
	*value = 0x100U + ++random_calls;
	return CB_SUCCESS;
}

static bool true_state(void *context)
{
	return context && *(uint32_t *)context == 0x12345678U;
}

static bool true_range(void *context, uint64_t base, uint64_t size)
{
	return true_state(context) && base == (uintptr_t)backing &&
		size == sizeof(backing);
}

static void ack(const struct payload_mm_authvar_presence_transaction_binding *b,
	uint32_t decision,
	struct payload_mm_authvar_presence_transaction_ack *result,
	uint64_t *value)
{
	*result = (struct payload_mm_authvar_presence_transaction_ack) {
		.binding = *b,
		.decision = decision,
		.transport_status =
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ACCEPTED,
		.operation_status =
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ACCEPTED,
		.backing_status =
			decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT ?
			PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_CLEANED :
			PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_TRANSFERRED,
	};
	*value = payload_mm_authvar_presence_transaction_result(b, decision);
}

static enum cb_err prepare_authority(void *context,
	const struct payload_mm_authvar_presence_seed *seed,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *result, uint64_t *value)
{
	assert(true_state(context));
	assert(seed->endpoint.generation == binding->generation);
	prepare_calls++;
	prepared = true;
	backing[80] = 0xa5U;
	backing[sizeof(backing) - 1U] = 0x5aU;
	if (block_prepare) {
		assert(!pthread_mutex_lock(&commit_mutex));
		prepare_entered = true;
		assert(!pthread_cond_broadcast(&commit_cond));
		while (!prepare_release)
			assert(!pthread_cond_wait(&commit_cond, &commit_mutex));
		assert(!pthread_mutex_unlock(&commit_mutex));
	}
	ack(binding, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE, result, value);
	if (bad_prepare_ack)
		result->binding.transaction_id++;
	return prepare_error ? CB_ERR : CB_SUCCESS;
}

static enum cb_err commit_authority(void *context,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *result, uint64_t *value)
{
	assert(true_state(context));
	commit_calls++;
	if (block_commit) {
		assert(!pthread_mutex_lock(&commit_mutex));
		commit_entered = true;
		assert(!pthread_cond_broadcast(&commit_cond));
		while (!commit_release)
			assert(!pthread_cond_wait(&commit_cond, &commit_mutex));
		assert(!pthread_mutex_unlock(&commit_mutex));
	}
	committed = true;
	ack(binding, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT, result, value);
	return commit_error ? CB_ERR : CB_SUCCESS;
}

static enum cb_err abort_authority(void *context,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *result, uint64_t *value)
{
	assert(true_state(context));
	abort_calls++;
	aborted = true;
	prepared = false;
	memset(backing, 0, sizeof(backing));
	authority_scrubs++;
	ack(binding, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT, result, value);
	return CB_SUCCESS;
}

static __noreturn void fatal(void *context)
{
	assert(true_state(context));
	_exit(73);
}

static struct payload_mm_authvar_presence_composition composition(
	uint32_t *context)
{
	return (struct payload_mm_authvar_presence_composition) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_REVISION,
		.size = sizeof(struct payload_mm_authvar_presence_composition),
		.trigger_address = 0xb2,
		.trigger_value = 0xe8,
		.transaction_initiator_cpu = 0,
		.transaction_maximum_cpus = 4U,
		.cold_boot = true_state,
		.authority_prepare = prepare_authority,
		.authority_commit = commit_authority,
		.authority_abort = abort_authority,
		.fail_stop = fatal,
		.dma_protected = true_range,
		.cpu_rendezvous_ready = true_state,
		.cold_reset_ready = true_state,
		.lifecycle_sealed = true_state,
		.platform_ready = true_state,
		.context = context,
		.context_size = sizeof(*context),
	};
}

static void reset_fixture(void)
{
	payload_mm_authvar_presence_producer_reset_test();
	memset(backing, 0, sizeof(backing));
	random_calls = prepare_calls = commit_calls = abort_calls = 0;
	authority_scrubs = 0;
	prepared = committed = aborted = false;
	prepare_error = commit_error = bad_prepare_ack = false;
	block_commit = commit_entered = commit_release = false;
	block_prepare = prepare_entered = prepare_release = false;
	block_query = query_entered = query_release = false;
	before_prepare_entered = before_prepare_release = false;
	finalize_result = CB_ERR;
	compose_result = CB_ERR;
}

static void compose(void)
{
	uint32_t context = 0x12345678U;
	struct payload_mm_authvar_presence_composition policy =
		composition(&context);

	assert(payload_mm_authvar_presence_producer_reserve() == CB_SUCCESS);
	assert(payload_mm_authvar_presence_producer_compose(&policy) == CB_SUCCESS);
	assert(prepare_calls == 1U && prepared && !committed && !aborted);
	assert(random_calls == 12U);
}

static void success_and_abort(void)
{
	struct lb_authvar_presence_endpoint endpoint;

	reset_fixture();
	compose();
	memset(&endpoint, 0, sizeof(endpoint));
	assert(payload_mm_authvar_presence_producer_publication_take(&endpoint) ==
		CB_SUCCESS);
	assert(commit_calls == 1U && committed && !abort_calls);
	assert(endpoint.tag == LB_TAG_AUTHVAR_PRESENCE_ENDPOINT);
	payload_mm_authvar_presence_producer_abort();
	payload_mm_authvar_presence_producer_abort();
	assert(!abort_calls);

	reset_fixture();
	compose();
	payload_mm_authvar_presence_producer_abort();
	assert(abort_calls == 1U && aborted && !commit_calls);
	assert(authority_scrubs == 1U);
	assert(!backing[80] && !backing[sizeof(backing) - 1U]);
	assert(payload_mm_authvar_presence_producer_publication_take(&endpoint) ==
		CB_ERR);
}

static void ambiguous(void)
{
	uint32_t context = 0x12345678U;
	struct payload_mm_authvar_presence_composition policy =
		composition(&context);
	struct lb_authvar_presence_endpoint endpoint;
	pid_t child;
	int wait_status;

	reset_fixture();
	prepare_error = true;
	assert(payload_mm_authvar_presence_producer_reserve() == CB_SUCCESS);
	assert(payload_mm_authvar_presence_producer_compose(&policy) == CB_ERR);
	assert(prepare_calls == 1U && abort_calls == 1U && aborted);
	assert(authority_scrubs == 1U);
	assert(!backing[80] && !backing[sizeof(backing) - 1U]);

	reset_fixture();
	bad_prepare_ack = true;
	assert(payload_mm_authvar_presence_producer_reserve() == CB_SUCCESS);
	assert(payload_mm_authvar_presence_producer_compose(&policy) == CB_ERR);
	assert(prepare_calls == 1U && abort_calls == 1U);

	reset_fixture();
	compose();
	child = fork();
	assert(child >= 0);
	if (!child) {
		commit_error = true;
		(void)payload_mm_authvar_presence_producer_publication_take(&endpoint);
		_exit(0);
	}
	assert(waitpid(child, &wait_status, 0) == child);
	assert(WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 73);
}

static void reservation_owner_cleanup(void)
{
	uint32_t context = 0x12345678U;
	struct payload_mm_authvar_presence_composition policy =
		composition(&context);

	reset_fixture();
	assert(payload_mm_authvar_presence_producer_reserve() == CB_SUCCESS);
	memset(backing, 0x69, sizeof(backing));
	policy.revision = 2U;
	assert(payload_mm_authvar_presence_producer_compose(&policy) == CB_ERR);
	assert(!backing[80] && !backing[sizeof(backing) - 1U]);
	assert(!prepare_calls && !abort_calls && !authority_scrubs);

	reset_fixture();
	policy = composition(&context);
	assert(payload_mm_authvar_presence_producer_reserve() == CB_SUCCESS);
	memset(backing, 0x69, sizeof(backing));
	policy.revision++;
	assert(payload_mm_authvar_presence_producer_compose(&policy) == CB_ERR);
	assert(!backing[80] && !backing[sizeof(backing) - 1U]);
	assert(!prepare_calls && !abort_calls && !authority_scrubs);
}

static void busy_abort_owner_cleanup(void)
{
	pthread_t thread;

	reset_fixture();
	assert(payload_mm_authvar_presence_producer_reserve() == CB_SUCCESS);
	memset(backing, 0x96, sizeof(backing));
	block_query = true;
	assert(!pthread_create(&thread, NULL, compose_thread, NULL));
	assert(!pthread_mutex_lock(&commit_mutex));
	while (!query_entered)
		assert(!pthread_cond_wait(&commit_cond, &commit_mutex));
	assert(backing[80] == 0x96U && backing[sizeof(backing) - 1U] == 0x96U);
	payload_mm_authvar_presence_producer_abort();
	assert(backing[80] == 0x96U && backing[sizeof(backing) - 1U] == 0x96U);
	query_release = true;
	assert(!pthread_cond_broadcast(&commit_cond));
	assert(!pthread_mutex_unlock(&commit_mutex));
	assert(!pthread_join(thread, NULL));
	assert(compose_result == CB_ERR);
	assert(!backing[80] && !backing[sizeof(backing) - 1U]);
}

static void block_before_prepare_once(void)
{
	payload_mm_authvar_presence_producer_before_prepare_test_hook(NULL);
	backing[80] = 0x3cU;
	backing[sizeof(backing) - 1U] = 0xc3U;
	assert(!pthread_mutex_lock(&commit_mutex));
	before_prepare_entered = true;
	assert(!pthread_cond_broadcast(&commit_cond));
	while (!before_prepare_release)
		assert(!pthread_cond_wait(&commit_cond, &commit_mutex));
	assert(!pthread_mutex_unlock(&commit_mutex));
}

static void transaction_created_abort_owner_cleanup(void)
{
	pthread_t thread;

	reset_fixture();
	assert(payload_mm_authvar_presence_producer_reserve() == CB_SUCCESS);
	payload_mm_authvar_presence_producer_before_prepare_test_hook(
		block_before_prepare_once);
	assert(!pthread_create(&thread, NULL, compose_thread, NULL));
	assert(!pthread_mutex_lock(&commit_mutex));
	while (!before_prepare_entered)
		assert(!pthread_cond_wait(&commit_cond, &commit_mutex));
	assert(!pthread_mutex_unlock(&commit_mutex));
	payload_mm_authvar_presence_producer_abort();
	assert(!abort_calls && backing[80] == 0x3cU &&
		backing[sizeof(backing) - 1U] == 0xc3U);
	assert(!pthread_mutex_lock(&commit_mutex));
	before_prepare_release = true;
	assert(!pthread_cond_broadcast(&commit_cond));
	assert(!pthread_mutex_unlock(&commit_mutex));
	assert(!pthread_join(thread, NULL));
	assert(compose_result == CB_ERR && !abort_calls);
	assert(!backing[80] && !backing[sizeof(backing) - 1U]);
}

static void *finalize_thread(void *unused)
{
	struct lb_authvar_presence_endpoint endpoint;

	(void)unused;
	finalize_result = payload_mm_authvar_presence_producer_publication_take(
		&endpoint);
	return NULL;
}

static void *compose_thread(void *unused)
{
	uint32_t context = 0x12345678U;
	struct payload_mm_authvar_presence_composition policy =
		composition(&context);
	(void)unused;
	compose_result = payload_mm_authvar_presence_producer_compose(&policy);
	return NULL;
}

static void prepare_abort_race(void)
{
	pthread_t thread;

	reset_fixture();
	assert(payload_mm_authvar_presence_producer_reserve() == CB_SUCCESS);
	block_prepare = true;
	assert(!pthread_create(&thread, NULL, compose_thread, NULL));
	assert(!pthread_mutex_lock(&commit_mutex));
	while (!prepare_entered)
		assert(!pthread_cond_wait(&commit_cond, &commit_mutex));
	payload_mm_authvar_presence_producer_abort();
	payload_mm_authvar_presence_producer_abort();
	assert(!abort_calls);
	prepare_release = true;
	assert(!pthread_cond_broadcast(&commit_cond));
	assert(!pthread_mutex_unlock(&commit_mutex));
	assert(!pthread_join(thread, NULL));
	assert(compose_result == CB_ERR && abort_calls == 1U && aborted);
}

static void finalize_wins(void)
{
	pthread_t thread;

	reset_fixture();
	compose();
	block_commit = true;
	assert(!pthread_create(&thread, NULL, finalize_thread, NULL));
	assert(!pthread_mutex_lock(&commit_mutex));
	while (!commit_entered)
		assert(!pthread_cond_wait(&commit_cond, &commit_mutex));
	payload_mm_authvar_presence_producer_abort();
	assert(!abort_calls);
	commit_release = true;
	assert(!pthread_cond_broadcast(&commit_cond));
	assert(!pthread_mutex_unlock(&commit_mutex));
	assert(!pthread_join(thread, NULL));
	assert(finalize_result == CB_SUCCESS && commit_calls == 1U && !abort_calls);
}

#undef payload_mm_authvar_presence_producer_publication_take

struct complete_race_context {
	struct payload_mm_authvar_presence_receipt *receipt;
	enum cb_err result;
};

static u32 complete_race_start;

static void *complete_race_thread(void *argument)
{
	struct complete_race_context *context = argument;

	while (!__atomic_load_n(&complete_race_start, __ATOMIC_ACQUIRE))
		__asm__ __volatile__("pause");
	context->result =
		payload_mm_authvar_presence_producer_publication_complete(context->receipt);
	return NULL;
}

static void publication_complete_race(void)
{
	struct payload_mm_authvar_presence_receipt receipt = { 0 };
	struct complete_race_context contexts[2] = {
		{ .receipt = &receipt },
		{ .receipt = &receipt },
	};
	pthread_t threads[2];

	reset_fixture();
	compose();
	assert(payload_mm_authvar_presence_producer_publication_take(&receipt) ==
		CB_SUCCESS);
	__atomic_store_n(&complete_race_start, 0, __ATOMIC_RELEASE);
	for (size_t index = 0; index < ARRAY_SIZE(threads); index++)
		assert(!pthread_create(&threads[index], NULL, complete_race_thread,
				       &contexts[index]));
	__atomic_store_n(&complete_race_start, 1, __ATOMIC_RELEASE);
	for (size_t index = 0; index < ARRAY_SIZE(threads); index++)
		assert(!pthread_join(threads[index], NULL));
	assert((contexts[0].result == CB_SUCCESS) !=
	       (contexts[1].result == CB_SUCCESS));
}

static void expect_pending_fail_stop(unsigned int mutation)
{
	struct payload_mm_authvar_presence_receipt receipt = { 0 };
	struct payload_mm_authvar_presence_receipt copy;
	pid_t child;
	int wait_status;

	child = fork();
	assert(child >= 0);
	if (!child) {
		reset_fixture();
		compose();
		assert(payload_mm_authvar_presence_producer_publication_take(&receipt) ==
			CB_SUCCESS);
		copy = receipt;
		switch (mutation) {
		case 0:
			receipt.identity++;
			break;
		case 1:
			receipt.nonce++;
			break;
		case 2:
			receipt.endpoint.generation++;
			break;
		case 3:
			receipt.active = 0;
			break;
		case 4:
			(void)payload_mm_authvar_presence_producer_publication_complete(&copy);
			break;
		case 5:
			(void)payload_mm_authvar_presence_producer_publication_complete(NULL);
			break;
		case 6:
			payload_mm_authvar_presence_producer_publication_marker_test(0);
			break;
		case 7:
			payload_mm_authvar_presence_producer_publication_marker_test(4);
			break;
		case 8:
			payload_mm_authvar_presence_producer_publication_marker_test(UINT32_MAX);
			break;
		default:
			payload_mm_authvar_presence_producer_publication_fail_stop(NULL);
			break;
		}
		(void)payload_mm_authvar_presence_producer_publication_complete(&receipt);
		_exit(0);
	}
	assert(waitpid(child, &wait_status, 0) == child);
	assert(WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 73);
}

static void publication_receipts(void)
{
	struct payload_mm_authvar_presence_receipt receipt = { 0 };
	struct payload_mm_authvar_presence_receipt replay;

	reset_fixture();
	compose();
	assert(payload_mm_authvar_presence_producer_publication_take(&receipt) ==
		CB_SUCCESS);
	assert(receipt.active == 1 && receipt.identity == (uintptr_t)&receipt &&
	       receipt.nonce != 0);
	replay = receipt;
	assert(payload_mm_authvar_presence_producer_publication_complete(&receipt) ==
		CB_SUCCESS);
	assert(!memcmp(&receipt,
		       &(struct payload_mm_authvar_presence_receipt) { 0 },
		       sizeof(receipt)));
	receipt = replay;
	receipt.identity = (uintptr_t)&receipt;
	assert(payload_mm_authvar_presence_producer_publication_complete(&receipt) ==
		CB_ERR);
	assert(!memcmp(&receipt, &replay, sizeof(receipt)));
	memset(&receipt, 0, sizeof(receipt));

	for (unsigned int mutation = 0; mutation < 10; mutation++)
		expect_pending_fail_stop(mutation);
}

int main(void)
{
	success_and_abort();
	ambiguous();
	reservation_owner_cleanup();
	busy_abort_owner_cleanup();
	transaction_created_abort_owner_cleanup();
	prepare_abort_race();
	finalize_wins();
	publication_receipts();
	publication_complete_race();
	return 0;
}
