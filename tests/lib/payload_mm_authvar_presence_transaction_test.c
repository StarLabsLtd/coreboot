/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_transaction.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static struct payload_mm_authvar_presence_transaction_slot slot;
static struct payload_mm_authvar_presence_transaction_page page;
static unsigned int prepare_calls, commit_calls, abort_calls, fail_calls;
static bool callback_error, bad_cpu, bad_dma, publish_error, claim_error;
static bool launder_state;
static uint64_t published_rax;
static unsigned int claim_calls, publish_calls;
static unsigned int bad_rendezvous;
static bool mutate_provision_source;
static bool block_prepare, prepare_entered, prepare_release, reenter_prepare;
static enum cb_err reenter_status, thread_status;
static pthread_mutex_t prepare_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t prepare_cond = PTHREAD_COND_INITIALIZER;

struct payload_mm_authvar_presence_transaction_slot *
smm_get_payload_mm_authvar_presence_transaction_slot(void)
{
	return &slot;
}

static bool zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;
	while (size--)
		value |= *bytes++;
	return value == 0;
}

static struct payload_mm_authvar_presence_transaction_binding binding(void)
{
	struct payload_mm_authvar_presence_transaction_binding value = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION,
		.size = sizeof(value), .generation = 7U, .transaction_id = 8U,
		.nonce = 9U, .initiator_cpu = 0, .maximum_cpus = 4U,
	};
	memset(value.capability, 0x5a, sizeof(value.capability));
	return value;
}

static struct payload_mm_authvar_presence_seed seed(void)
{
	struct payload_mm_authvar_presence_seed value = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION,
		.size = sizeof(value), .endpoint = { .generation = 7U },
	};
	memset(value.capability, 0xa5, sizeof(value.capability));
	return value;
}

static enum cb_err prepare(void *context,
	const struct payload_mm_authvar_presence_seed *value, uint64_t generation)
{
	assert(context == &slot.context[0] && value->endpoint.generation == generation);
	prepare_calls++;
	if (launder_state)
		__atomic_store_n(&slot.state, 7U, __ATOMIC_RELEASE);
	if (reenter_prepare)
		reenter_status = payload_mm_authvar_presence_transaction_dispatch(&slot);
	if (block_prepare) {
		assert(!pthread_mutex_lock(&prepare_mutex));
		prepare_entered = true;
		assert(!pthread_cond_broadcast(&prepare_cond));
		while (!prepare_release)
			assert(!pthread_cond_wait(&prepare_cond, &prepare_mutex));
		assert(!pthread_mutex_unlock(&prepare_mutex));
	}
	return callback_error ? CB_ERR : CB_SUCCESS;
}

static enum cb_err commit(void *context, uint64_t generation)
{
	assert(context == &slot.context[0] && generation == 7U);
	commit_calls++;
	return callback_error ? CB_ERR : CB_SUCCESS;
}

static enum cb_err abort_transaction(void *context, uint64_t generation)
{
	assert(context == &slot.context[0] && generation == 7U);
	abort_calls++;
	return CB_SUCCESS;
}

static bool dma(void *context, uint64_t base, uint64_t size)
{
	return context == &slot.context[0] && !bad_dma &&
		base == (uintptr_t)&page && size == sizeof(page);
}

static enum cb_err claim(void *context, uint64_t sentinel,
	struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	assert(context == &slot.context[0]);
	claim_calls++;
	assert(sentinel == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_RAX_SENTINEL);
	*invocation = (struct payload_mm_authvar_presence_transaction_invocation) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_INVOCATION_REVISION,
		.size = sizeof(*invocation), .initiator_cpu = bad_cpu ? 1U : 0U,
		.active_cpus = 4U, .smi_generation = 11U, .bsp = 1U,
		.rendezvous_generation = 11U,
		.rendezvous_proof = { 1U, 2U, 3U },
	};
	if (bad_rendezvous == 1U)
		invocation->rendezvous_generation++;
	else if (bad_rendezvous == 2U)
		memset(invocation->rendezvous_proof, 0,
			sizeof(invocation->rendezvous_proof));
	return claim_error ? CB_ERR : CB_SUCCESS;
}

static enum cb_err publish(void *context,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	uint64_t value)
{
	assert(context == &slot.context[0] && invocation->smi_generation == 11U);
	publish_calls++;
	published_rax = value;
	return publish_error ? CB_ERR : CB_SUCCESS;
}

static __noreturn void fail_stop(void *context)
{
	assert(context && *(uint32_t *)context == 0x12345678U);
	fail_calls++;
	abort();
}

static bool protected(void *context, const void *object, size_t size)
{
	(void)context;
	return object && size && object != &page;
}

enum cb_err bootmem_reservation_receipt_verify_consume_exact_tag(
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt, enum bootmem_type tag)
{
	enum cb_err status = verifier->state == 1U && tag == BM_MEM_RESERVED &&
		receipt->tag == BM_MEM_RESERVED && receipt->base == (uintptr_t)&page &&
		receipt->bytes == sizeof(page) ? CB_SUCCESS : CB_ERR;
	memset(verifier, 0, sizeof(*verifier));
	memset(receipt, 0, sizeof(*receipt));
	return status;
}

void bootmem_reservation_receipt_close(
	struct bootmem_reservation_receipt_authority *authority)
{
	memset(authority, 0, sizeof(*authority));
}

enum cb_err bootmem_reservation_receipt_mac(const uint8_t key[32],
	const void *message, size_t size, uint8_t mac[32])
{
	const uint8_t *bytes = message;
	memcpy(mac, key, 32);
	for (size_t index = 0; index < size; index++)
		mac[index % 32U] ^= bytes[index];
	return CB_SUCCESS;
}

static void reset_fixture(void)
{
	memset(&slot, 0, sizeof(slot));
	memset(&page, 0, sizeof(page));
	prepare_calls = commit_calls = abort_calls = fail_calls = 0;
	claim_calls = publish_calls = bad_rendezvous = 0;
	mutate_provision_source = false;
	callback_error = bad_cpu = bad_dma = publish_error = claim_error = false;
	launder_state = false;
	block_prepare = prepare_entered = prepare_release = reenter_prepare = false;
	reenter_status = thread_status = CB_SUCCESS;
	published_rax = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_RAX_SENTINEL;
}

void payload_mm_authvar_presence_transaction_test_after_terminal_release(
	const struct payload_mm_authvar_presence_transaction_slot *value,
	const struct payload_mm_authvar_presence_transaction_page *request_page)
{
	assert(!payload_mm_authvar_presence_transaction_dispatch_enabled(value, 7U));
	assert(zero(request_page, sizeof(*request_page)));
	assert(zero(&value->policy, sizeof(value->policy)));
	assert(zero(value->context, sizeof(value->context)));
	assert(!value->page_base && !value->page_size &&
		!value->ack_published);
}

void payload_mm_authvar_presence_transaction_test_after_provision_claim(
	const struct payload_mm_authvar_presence_transaction_policy *policy)
{
	if (mutate_provision_source)
		((struct payload_mm_authvar_presence_transaction_policy *)policy)->size++;
}

static void provision(void)
{
	uint32_t context = 0x12345678U;
	struct payload_mm_authvar_presence_transaction_policy policy = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_POLICY_REVISION,
		.size = sizeof(policy), .prepare = prepare, .commit = commit,
		.abort = abort_transaction, .dma_protected = dma,
		.claim_invocation = claim, .publish_and_verify_rax = publish,
		.fail_stop = fail_stop, .context = &context,
		.context_size = sizeof(context),
	};
	struct payload_mm_authvar_presence_transaction_binding b = binding();
	struct bootmem_reservation_receipt_authority verifier = {
		.generation = 7U, .state = 1U,
	};
	struct bootmem_reservation_receipt receipt = {
		.generation = 7U, .base = (uintptr_t)&page, .bytes = sizeof(page),
		.tag = BM_MEM_RESERVED,
	};
	assert(payload_mm_authvar_presence_transaction_provision(&slot, &policy, &b,
		&verifier, &receipt, protected, NULL) == CB_SUCCESS);
}

static void request(uint32_t decision)
{
	memset(&page, 0, sizeof(page));
	page.request.binding = binding();
	page.request.decision = decision;
	if (decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE)
		page.request.seed = seed();
}

static void exact_ack(uint32_t decision)
{
	struct payload_mm_authvar_presence_transaction_binding b = binding();
	assert(payload_mm_authvar_presence_transaction_ack_valid(&b, decision,
		&page.ack, published_rax));
	assert(zero(&page.request, sizeof(page.request)));
}

static void success(void)
{
	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	exact_ack(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(!payload_mm_authvar_presence_transaction_dispatch_enabled(&slot, 7U));
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	exact_ack(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
	assert(payload_mm_authvar_presence_transaction_dispatch_enabled(&slot, 7U));
	assert(prepare_calls == 1U && commit_calls == 1U && !abort_calls);
}

static void abort_path(void)
{
	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	exact_ack(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
	assert(abort_calls == 1U && !commit_calls);
}

static void failures(void)
{
	static const uint8_t sentinel_capability[32] = {
		0xd5, 0x2b, 0x37, 0x6b, 0x0d, 0x32, 0x7d, 0x3c, 0x54,
	};
	struct payload_mm_authvar_presence_transaction_binding collision = binding();

	memcpy(collision.capability, sentinel_capability,
		sizeof(collision.capability));
	assert(payload_mm_authvar_presence_transaction_rax(&collision,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE) == 0);

	reset_fixture();
	provision();
	bad_cpu = true;
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_ERR);
	assert(!prepare_calls);

	reset_fixture();
	provision();
	claim_error = true;
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_ERR);
	assert(!prepare_calls);

	reset_fixture();
	provision();
	callback_error = true;
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_ERR);
	assert(prepare_calls == 1U && abort_calls == 1U);

	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	page.request.binding.nonce++;
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_ERR);
	assert(zero(&page, sizeof(page)));

	for (unsigned int mode = 1U; mode <= 2U; mode++) {
		reset_fixture();
		bad_rendezvous = mode;
		provision();
		request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
		assert(payload_mm_authvar_presence_transaction_dispatch(&slot) ==
			CB_ERR);
		assert(!prepare_calls);
	}
}

static void terminal_and_dirty_slot(void)
{
	struct payload_mm_authvar_presence_transaction_slot counterfeit;
	struct payload_mm_authvar_presence_transaction_page terminal_page;
	unsigned int claims;

	reset_fixture();
	memset(&slot, 0xa5, sizeof(slot));
	slot.state = 0;
	provision();
	assert(zero(slot.context + sizeof(uint32_t),
		sizeof(slot.context) - sizeof(uint32_t)));
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	terminal_page = page;
	claims = claim_calls;
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_ERR);
	assert(claim_calls == claims && !memcmp(&terminal_page, &page, sizeof(page)));
	counterfeit = slot;
	assert(!payload_mm_authvar_presence_transaction_dispatch_enabled(
		&counterfeit, 7U));
}

static void *dispatch_thread(void *unused)
{
	(void)unused;
	thread_status = payload_mm_authvar_presence_transaction_dispatch(&slot);
	return NULL;
}

static void contention_and_reentry(void)
{
	pthread_t thread;

	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	block_prepare = true;
	assert(!pthread_create(&thread, NULL, dispatch_thread, NULL));
	assert(!pthread_mutex_lock(&prepare_mutex));
	while (!prepare_entered)
		assert(!pthread_cond_wait(&prepare_cond, &prepare_mutex));
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_ERR);
	assert(claim_calls == 1U && prepare_calls == 1U);
	prepare_release = true;
	assert(!pthread_cond_broadcast(&prepare_cond));
	assert(!pthread_mutex_unlock(&prepare_mutex));
	assert(!pthread_join(thread, NULL));
	assert(thread_status == CB_SUCCESS);

	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	reenter_prepare = true;
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	assert(reenter_status == CB_ERR && prepare_calls == 1U && claim_calls == 1U);
}

static void publish_failure_is_fatal(void)
{
	pid_t child;
	int status;

	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	child = fork();
	assert(child >= 0);
	if (!child) {
		request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
		publish_error = true;
		(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

static void state_laundering_is_fatal(void)
{
	pid_t child;
	int status;

	reset_fixture();
	provision();
	child = fork();
	assert(child >= 0);
	if (!child) {
		request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
		callback_error = true;
		launder_state = true;
		(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

static void provision_mutation_is_fatal(void)
{
	pid_t child;
	int status;

	reset_fixture();
	memset(&slot, 0xa5, sizeof(slot));
	slot.state = 0;
	child = fork();
	assert(child >= 0);
	if (!child) {
		mutate_provision_source = true;
		provision();
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

int main(void)
{
	success();
	abort_path();
	failures();
	terminal_and_dirty_slot();
	contention_and_reentry();
	publish_failure_is_fatal();
	state_laundering_is_fatal();
	provision_mutation_is_fatal();
	return 0;
}
