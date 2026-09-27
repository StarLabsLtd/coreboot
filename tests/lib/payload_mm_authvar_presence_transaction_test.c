/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_transaction.h>
#include <commonlib/helpers.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(REAL_RECEIPT)
#include "bootmem_reservation_receipt_internal.h"
#endif

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static struct payload_mm_authvar_presence_transaction_slot slot;
static struct payload_mm_authvar_presence_transaction_page page;
static struct payload_mm_authvar_presence_transaction_page page_before;
static unsigned int prepare_calls, commit_calls, abort_calls, fail_calls;
static bool callback_error, bad_cpu, bad_dma, publish_error, claim_error;
static bool launder_state, launder_abort_state, mutate_claim_context;
static bool abort_error, launder_commit_state;
static bool bad_receipt;
static bool zero_context, reenter_complete;
static uint64_t published_rax;
static unsigned int claim_calls, publish_calls;
static unsigned int expected_publish_calls;
static unsigned int bad_rendezvous;
static bool mutate_provision_source;
static unsigned int mutate_proof_call, proof_calls;
static unsigned int claim_storage_mutation, mutate_failure_stage;
static unsigned int mutate_failure_field;
static unsigned int mutate_owner_stage;
static unsigned int complete_storage_mutation;
static const void *unprotected_object;
static bool block_prepare, prepare_entered, prepare_release, reenter_prepare;
static enum cb_err reenter_status, thread_status;
static pthread_mutex_t prepare_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t prepare_cond = PTHREAD_COND_INITIALIZER;

enum fatal_expectation {
	FATAL_NONE,
	FATAL_CLAIM,
	FATAL_TOKEN,
	FATAL_PAGE_UNTOUCHED,
	FATAL_PREPARE_MUTATION,
	FATAL_ABORT_MUTATION,
	FATAL_FINAL_ABORT,
	FATAL_COMMIT,
	FATAL_COMPLETE,
	FATAL_PROVISION,
	FATAL_PROVISIONED_CORRUPT,
	FATAL_PREPARED_CORRUPT,
	FATAL_OWNER_MUTATION,
	FATAL_COMPLETE_MUTATION,
	FATAL_EXACT,
};

static enum fatal_expectation fatal_expected;
static unsigned int fatal_claims, fatal_prepares, fatal_commits;
static unsigned int fatal_aborts, fatal_completes;
static const struct payload_mm_authvar_presence_transaction_policy *proof_policy;

static bool callback_context_valid(void *context)
{
	return zero_context ? !context :
		context && *(uint32_t *)context == 0x12345678U;
}

static void mutate_failure_closure(void)
{
	switch (mutate_failure_field) {
	case 1:
		slot.failure.callback = NULL;
		break;
	case 2:
		slot.failure.context_size++;
		break;
	case 3:
		slot.failure.generation++;
		break;
	case 4:
		slot.failure.context[0] ^= 1U;
		break;
	case 5:
		slot.failure.context[sizeof(slot.failure.context) - 1U] ^= 1U;
		break;
	default:
		slot.failure.reserved[0] = 1U;
		break;
	}
}

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
		.backing = {
			.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION,
			.size = sizeof(struct payload_mm_authvar_presence_backing),
			.base = 0x100000U,
			.bytes = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE,
			.generation = 7U,
			.tag = BM_MEM_RESERVED,
		},
	};
	memset(value.capability, 0xa5, sizeof(value.capability));
	return value;
}

static enum cb_err prepare(void *context,
	const struct payload_mm_authvar_presence_seed *value, uint64_t generation)
{
	assert(callback_context_valid(context) &&
	       value->endpoint.generation == generation);
	prepare_calls++;
	if (mutate_owner_stage == 3U)
		__atomic_store_n(&slot.dispatch_owner, 0U, __ATOMIC_RELEASE);
	if (mutate_failure_stage == 3U)
		mutate_failure_closure();
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
	assert(callback_context_valid(context) && generation == 7U);
	commit_calls++;
	if (mutate_owner_stage == 5U)
		__atomic_store_n(&slot.dispatch_owner, 0U, __ATOMIC_RELEASE);
	if (mutate_failure_stage == 5U)
		mutate_failure_closure();
	if (launder_commit_state)
		__atomic_store_n(&slot.state, 7U, __ATOMIC_RELEASE);
	return callback_error ? CB_ERR : CB_SUCCESS;
}

static enum cb_err abort_transaction(void *context, uint64_t generation)
{
	assert(callback_context_valid(context) && generation == 7U);
	abort_calls++;
	if (mutate_owner_stage == 4U)
		__atomic_store_n(&slot.dispatch_owner, 0U, __ATOMIC_RELEASE);
	if (mutate_failure_stage == 4U)
		mutate_failure_closure();
	if (launder_abort_state)
		__atomic_store_n(&slot.state, 7U, __ATOMIC_RELEASE);
	return abort_error ? CB_ERR : CB_SUCCESS;
}

static bool dma(void *context, uint64_t base, uint64_t size)
{
	if (mutate_owner_stage == 2U)
		__atomic_store_n(&slot.dispatch_owner, 0U, __ATOMIC_RELEASE);
	if (mutate_failure_stage == 2U)
		mutate_failure_closure();
	return callback_context_valid(context) && !bad_dma &&
		base == (uintptr_t)&page && size == sizeof(page);
}

static enum cb_err claim(void *context, uint64_t sentinel,
	struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	assert(callback_context_valid(context));
	claim_calls++;
	if (mutate_owner_stage == 1U)
		__atomic_store_n(&slot.dispatch_owner, 0U, __ATOMIC_RELEASE);
	if (mutate_failure_stage == 1U)
		mutate_failure_closure();
	switch (claim_storage_mutation) {
	case 1:
		slot.page_verifier.generation++;
		break;
	case 2:
		slot.page_verifier.secret[0] ^= 1U;
		break;
	case 3:
		slot.page_receipt.revision++;
		break;
	case 4:
		slot.page_receipt.size++;
		break;
	case 5:
		slot.page_receipt.boot_kind++;
		break;
	case 6:
		slot.page_receipt.reserved++;
		break;
	case 7:
		slot.page_receipt.generation++;
		break;
	case 8:
		slot.page_receipt.sequence++;
		break;
	case 9:
		slot.page_receipt.handle.opaque[0]++;
		break;
	case 10:
		slot.page_receipt.base += sizeof(page);
		break;
	case 11:
		slot.page_receipt.bytes++;
		break;
	case 12:
		slot.page_receipt.tag++;
		break;
	case 13:
		slot.page_receipt.use++;
		break;
	case 14:
		slot.page_receipt.mac[0] ^= 1U;
		break;
	}
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
	if (mutate_claim_context && context)
		*(uint32_t *)context = 0;
	if (!claim_error)
		return CB_SUCCESS;
	memset(invocation, 0, sizeof(*invocation));
	return CB_ERR;
}

static enum cb_err complete_invocation(void *context,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	uint64_t value)
{
	assert(callback_context_valid(context) &&
	       invocation->smi_generation == 11U);
	assert(slot.dispatch_owner == 1U && !slot.ack_published);
	publish_calls++;
	if (mutate_owner_stage == 6U)
		__atomic_store_n(&slot.dispatch_owner, 0U, __ATOMIC_RELEASE);
	switch (complete_storage_mutation) {
	case 1:
		slot.policy.size++;
		break;
	case 2:
		slot.failure.generation++;
		break;
	case 3:
		slot.binding.nonce++;
		break;
	case 4:
		slot.context[0] ^= 1U;
		break;
	case 5:
		slot.page_base++;
		break;
	case 6:
		slot.page_receipt.revision++;
		break;
	case 7:
		__atomic_store_n(&slot.state, 7U, __ATOMIC_RELEASE);
		break;
	case 8:
		__atomic_store_n(&slot.ack_published, 1U, __ATOMIC_RELEASE);
		break;
	case 9:
		((struct payload_mm_authvar_presence_transaction_invocation *)
		 invocation)->reserved++;
		break;
	}
	published_rax = value;
	if (reenter_complete) {
		const unsigned int claims = claim_calls;
		reenter_status = payload_mm_authvar_presence_transaction_dispatch(&slot);
		assert(reenter_status == CB_ERR && claim_calls == claims &&
		       slot.dispatch_owner == 1U && !slot.ack_published);
	}
	return publish_error ? CB_ERR : CB_SUCCESS;
}

static __noreturn void fail_stop(void *context)
{
	assert(callback_context_valid(context));
	fail_calls++;
	switch (fatal_expected) {
	case FATAL_CLAIM:
		assert(claim_calls == 1U && !prepare_calls && !abort_calls &&
		       !publish_calls);
		break;
	case FATAL_TOKEN:
		assert(claim_calls == 1U && !prepare_calls && !abort_calls &&
		       !publish_calls);
		break;
	case FATAL_PAGE_UNTOUCHED:
		assert(claim_calls == 1U && !prepare_calls && !abort_calls &&
		       !publish_calls && !memcmp(&page, &page_before, sizeof(page)));
		break;
	case FATAL_PREPARE_MUTATION:
		assert(claim_calls == 1U && prepare_calls == 1U &&
		       !abort_calls && !publish_calls);
		break;
	case FATAL_ABORT_MUTATION:
		assert(claim_calls == 1U && prepare_calls == 1U &&
		       abort_calls == 1U && !publish_calls);
		break;
	case FATAL_FINAL_ABORT:
		assert(claim_calls == 2U && prepare_calls == 1U &&
		       abort_calls == 1U && publish_calls == 1U);
		break;
	case FATAL_COMPLETE:
		assert(claim_calls && publish_calls == expected_publish_calls);
		break;
	case FATAL_COMMIT:
		assert(claim_calls == 2U && prepare_calls == 1U &&
		       commit_calls == 1U && !abort_calls && publish_calls == 1U);
		break;
	case FATAL_PROVISION:
		assert(!claim_calls && !publish_calls);
		break;
	case FATAL_PROVISIONED_CORRUPT:
		assert(!claim_calls && !prepare_calls && !abort_calls &&
		       !publish_calls);
		break;
	case FATAL_PREPARED_CORRUPT:
		assert(claim_calls == 1U && prepare_calls == 1U &&
		       !abort_calls && publish_calls == 1U);
		break;
	case FATAL_OWNER_MUTATION:
		assert(claim_calls == fatal_claims && prepare_calls == fatal_prepares &&
		       commit_calls == fatal_commits && abort_calls == fatal_aborts &&
		       publish_calls == fatal_completes);
		break;
	case FATAL_COMPLETE_MUTATION:
		assert(claim_calls == fatal_claims && prepare_calls == fatal_prepares &&
		       commit_calls == fatal_commits && abort_calls == fatal_aborts &&
		       publish_calls == fatal_completes);
		break;
	case FATAL_EXACT:
		assert(claim_calls == fatal_claims && prepare_calls == fatal_prepares &&
		       commit_calls == fatal_commits && abort_calls == fatal_aborts &&
		       publish_calls == fatal_completes);
		break;
	case FATAL_NONE:
		break;
	}
	if (fatal_expected != FATAL_NONE && fatal_expected != FATAL_PROVISION &&
	    fatal_expected != FATAL_OWNER_MUTATION &&
	    fatal_expected != FATAL_COMPLETE_MUTATION) {
		const unsigned int claims = claim_calls;
		assert(payload_mm_authvar_presence_transaction_dispatch(&slot) ==
			CB_ERR);
		assert(slot.dispatch_owner == 1U && claim_calls == claims);
	}
	_exit(73);
}

static bool protected(void *context, const void *object, size_t size)
{
	(void)context;
	proof_calls++;
	if (mutate_proof_call == proof_calls && proof_policy)
		((struct payload_mm_authvar_presence_transaction_policy *)
		 proof_policy)->size++;
	return object && size && object != &page && object != unprotected_object;
}

#if !defined(REAL_RECEIPT)
enum cb_err bootmem_reservation_receipt_verify_consume_exact_tag(
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt, enum bootmem_type tag)
{
	enum cb_err status = !bad_receipt && verifier->state == 1U &&
		tag == BM_MEM_RESERVED &&
		receipt->tag == BM_MEM_RESERVED && receipt->base == (uintptr_t)&page &&
		receipt->bytes == sizeof(page) ? CB_SUCCESS : CB_ERR;
	memset(verifier, 0, sizeof(*verifier));
	verifier->state = 4U;
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
#endif

static void reset_fixture(void)
{
	memset(&slot, 0, sizeof(slot));
	memset(&page, 0, sizeof(page));
	prepare_calls = commit_calls = abort_calls = fail_calls = 0;
	claim_calls = publish_calls = bad_rendezvous = 0;
	expected_publish_calls = 0;
	mutate_provision_source = false;
	mutate_proof_call = proof_calls = 0;
	claim_storage_mutation = mutate_failure_stage = 0;
	mutate_failure_field = 0;
	mutate_owner_stage = 0;
	complete_storage_mutation = 0;
	fatal_claims = fatal_prepares = fatal_commits = 0;
	fatal_aborts = fatal_completes = 0;
	proof_policy = NULL;
	unprotected_object = NULL;
	callback_error = bad_cpu = bad_dma = publish_error = claim_error = false;
	launder_state = launder_abort_state = mutate_claim_context = false;
	abort_error = launder_commit_state = false;
	bad_receipt = false;
	zero_context = reenter_complete = false;
	fatal_expected = FATAL_NONE;
	block_prepare = prepare_entered = prepare_release = reenter_prepare = false;
	reenter_status = thread_status = CB_SUCCESS;
	published_rax = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_RAX_SENTINEL;
}

void payload_mm_authvar_presence_transaction_test_after_provision_claim(
	const struct payload_mm_authvar_presence_transaction_policy *policy)
{
	if (mutate_provision_source)
		((struct payload_mm_authvar_presence_transaction_policy *)policy)->size++;
}

void payload_mm_authvar_presence_transaction_test_after_dispatch_owner(
	uint32_t state)
{
	assert(state != 0U && state != 1U);
}

void payload_mm_authvar_presence_transaction_test_set_orphan_state(
	struct payload_mm_authvar_presence_transaction_slot *value,
	unsigned int index);

static enum cb_err provision_status(void)
{
	uint32_t context = 0x12345678U;
	struct payload_mm_authvar_presence_transaction_policy policy = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_POLICY_REVISION,
		.size = sizeof(policy), .prepare = prepare, .commit = commit,
		.abort = abort_transaction, .dma_protected = dma,
		.claim_invocation = claim, .complete_invocation = complete_invocation,
		.fail_stop = fail_stop,
		.context = zero_context ? NULL : &context,
		.context_size = zero_context ? 0U : sizeof(context),
	};
	struct payload_mm_authvar_presence_transaction_binding b = binding();
	struct bootmem_reservation_receipt_authority verifier = {
		.generation = 7U, .state = 1U,
	};
	struct bootmem_reservation_receipt receipt = {
		.generation = 7U, .base = (uintptr_t)&page, .bytes = sizeof(page),
		.tag = BM_MEM_RESERVED,
	};
#if defined(REAL_RECEIPT)
	memset(verifier.secret, 0x3c, sizeof(verifier.secret));
	verifier.sequence = 1U;
	verifier.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT;
	verifier.handle.opaque[0] = 0x11223344U;
	verifier.handle.opaque[1] = 0x55667788U;
	verifier.state = 3U;
	receipt.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION;
	receipt.size = sizeof(receipt);
	receipt.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT;
	receipt.sequence = verifier.sequence;
	receipt.handle = verifier.handle;
	receipt.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE;
	assert(bootmem_reservation_receipt_mac(verifier.secret, &receipt,
		offsetof(struct bootmem_reservation_receipt, mac), receipt.mac) ==
		CB_SUCCESS);
#endif
	proof_policy = &policy;
	return payload_mm_authvar_presence_transaction_provision(&slot, &policy, &b,
		&verifier, &receipt, protected, NULL);
}

static void provision(void)
{
	assert(provision_status() == CB_SUCCESS);
}

static void expect_fatal_dispatch(void)
{
	pid_t child = fork();
	int status;

	assert(child >= 0);
	if (!child) {
		(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);
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
	struct payload_mm_authvar_presence_transaction_ack altered;
	uint32_t expected_backing;

	assert(payload_mm_authvar_presence_transaction_ack_valid(&b, decision,
		&page.ack, published_rax));
	altered = page.ack;
	altered.backing_status = 0;
	assert(!payload_mm_authvar_presence_transaction_ack_valid(&b, decision,
		&altered, published_rax));
	expected_backing = decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT ?
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_CLEANED :
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_TRANSFERRED;
	altered.backing_status = expected_backing ^ 0x01010101U;
	assert(!payload_mm_authvar_presence_transaction_ack_valid(&b, decision,
		&altered, published_rax));
	altered = page.ack;
	altered.binding.revision = 1U;
	assert(!payload_mm_authvar_presence_transaction_ack_valid(&b, decision,
		&altered, published_rax));
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
	assert(zero(&slot.policy, sizeof(slot.policy)) &&
	       zero(slot.context, sizeof(slot.context)) &&
	       zero(&slot.failure, sizeof(slot.failure)) &&
	       !slot.page_base && !slot.page_size);
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
	assert(zero(&slot.policy, sizeof(slot.policy)) &&
	       zero(slot.context, sizeof(slot.context)) &&
	       zero(&slot.failure, sizeof(slot.failure)) &&
	       !slot.page_base && !slot.page_size);
	assert(abort_calls == 1U && !commit_calls);
}

static void failures(void)
{
	static const uint8_t sentinel_capability[32] = {
		0xd5, 0x2b, 0x37, 0x6b, 0x0d, 0x32, 0x7d, 0x3c, 0x54,
	};
	struct payload_mm_authvar_presence_transaction_binding collision = binding();
	pid_t child;
	int status;

	memcpy(collision.capability, sentinel_capability,
		sizeof(collision.capability));
	assert(payload_mm_authvar_presence_transaction_rax(&collision,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE) == 0);

	reset_fixture();
	provision();
	bad_cpu = true;
	fatal_expected = FATAL_TOKEN;
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	child = fork();
	assert(child >= 0);
	if (!child) {
		(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);

	for (unsigned int mode = 0; mode < 2U; mode++) {
		reset_fixture();
		provision();
		request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
		assert(payload_mm_authvar_presence_transaction_dispatch(&slot) ==
			CB_SUCCESS);
		request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
		abort_error = mode == 0U;
		launder_abort_state = mode == 1U;
		fatal_expected = FATAL_FINAL_ABORT;
		child = fork();
		assert(child >= 0);
		if (!child) {
			(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
			_exit(0);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);
	}

	reset_fixture();
	provision();
	claim_error = true;
	fatal_expected = FATAL_CLAIM;
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	child = fork();
	assert(child >= 0);
	if (!child) {
		(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);

	reset_fixture();
	provision();
	callback_error = true;
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	exact_ack(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
	assert(prepare_calls == 1U && abort_calls == 1U);

	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	page.request.seed.endpoint.generation++;
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	exact_ack(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
	assert(!prepare_calls && !abort_calls && publish_calls == 1U);

	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	page.request.binding.nonce++;
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	exact_ack(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);

	for (unsigned int mode = 1U; mode <= 2U; mode++) {
		reset_fixture();
		bad_rendezvous = mode;
		fatal_expected = FATAL_TOKEN;
		provision();
		request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
		child = fork();
		assert(child >= 0);
		if (!child) {
			(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
			_exit(0);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);
	}
}

static void terminal_and_dirty_slot(void)
{
	struct payload_mm_authvar_presence_transaction_slot counterfeit;
	struct payload_mm_authvar_presence_transaction_page terminal_page;
	unsigned int claims;

	reset_fixture();
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_ERR);
	assert(!slot.dispatch_owner);
	slot.state = 1U;
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_ERR);
	assert(!slot.dispatch_owner);

	reset_fixture();
	memset(&slot, 0xa5, sizeof(slot));
	slot.state = 0;
	slot.dispatch_owner = 0;
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

	reset_fixture();
	zero_context = true;
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);

	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	reenter_complete = true;
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	assert(reenter_status == CB_ERR && claim_calls == 1U && publish_calls == 1U);
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
		expected_publish_calls = 2U;
		fatal_expected = FATAL_COMPLETE;
		(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);

	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	publish_error = true;
	expected_publish_calls = 1U;
	fatal_expected = FATAL_COMPLETE;
	child = fork();
	assert(child >= 0);
	if (!child) {
		(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);

	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
	publish_error = true;
	expected_publish_calls = 2U;
	fatal_expected = FATAL_COMPLETE;
	child = fork();
	assert(child >= 0);
	if (!child) {
		(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);

	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
	launder_commit_state = true;
	fatal_expected = FATAL_COMMIT;
	child = fork();
	assert(child >= 0);
	if (!child) {
		(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);
}

static void fatal_boundaries(void)
{
	pid_t child;
	int status;

	for (unsigned int mode = 0; mode < 3U; mode++) {
		reset_fixture();
		provision();
		request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
		bad_dma = mode == 0U;
		bad_receipt = mode == 1U;
		mutate_claim_context = mode == 2U;
		page_before = page;
		fatal_expected = mode < 2U ? FATAL_PAGE_UNTOUCHED : FATAL_TOKEN;
		child = fork();
		assert(child >= 0);
		if (!child) {
			(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
			_exit(0);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);
	}

	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	callback_error = true;
	launder_abort_state = true;
	fatal_expected = FATAL_ABORT_MUTATION;
	child = fork();
	assert(child >= 0);
	if (!child) {
		(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);

	reset_fixture();
	provision();
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
	callback_error = true;
	fatal_expected = FATAL_COMMIT;
	child = fork();
	assert(child >= 0);
	if (!child) {
		(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);
}

static void protected_callback_mutations_are_fatal(void)
{
	for (unsigned int mode = 1U; mode <= 14U; mode++) {
		reset_fixture();
		provision();
		request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
		page_before = page;
		claim_storage_mutation = mode;
		fatal_expected = FATAL_PAGE_UNTOUCHED;
		expect_fatal_dispatch();
	}
	for (unsigned int field = 1U; field <= 6U; field++) {
		reset_fixture();
		provision();
		request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
		mutate_failure_stage = 1U;
		mutate_failure_field = field;
		fatal_claims = 1U;
		fatal_expected = FATAL_EXACT;
		expect_fatal_dispatch();
	}

	for (unsigned int stage = 1U; stage <= 5U; stage++) {
		reset_fixture();
		provision();
		if (stage >= 4U) {
			request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
			assert(payload_mm_authvar_presence_transaction_dispatch(&slot) ==
				CB_SUCCESS);
		}
		request(stage == 4U ?
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT :
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
		if (stage < 4U)
			request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
		mutate_failure_stage = stage;
		fatal_claims = stage >= 4U ? 2U : 1U;
		fatal_prepares = stage == 3U ? 1U : stage >= 4U ? 1U : 0U;
		fatal_commits = stage == 5U ? 1U : 0U;
		fatal_aborts = stage == 3U || stage == 4U ? 1U : 0U;
		fatal_completes = stage >= 4U ? 1U : 0U;
		fatal_expected = FATAL_EXACT;
		expect_fatal_dispatch();
	}
}

static void callback_owner_loss_is_fatal(void)
{
	for (unsigned int stage = 1U; stage <= 6U; stage++) {
		reset_fixture();
		provision();
		if (stage == 4U || stage == 5U) {
			request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
			assert(payload_mm_authvar_presence_transaction_dispatch(&slot) ==
				CB_SUCCESS);
		}
		request(stage == 4U ?
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT :
			stage == 5U ?
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT :
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
		mutate_owner_stage = stage;
		reenter_complete = stage == 6U;
		fatal_claims = stage == 4U || stage == 5U ? 2U : 1U;
		fatal_prepares = stage >= 3U ? 1U : 0U;
		fatal_commits = stage == 5U ? 1U : 0U;
		fatal_aborts = stage == 4U ? 1U : 0U;
		fatal_completes = stage >= 4U ? 1U : stage == 6U ? 1U : 0U;
		fatal_expected = FATAL_OWNER_MUTATION;
		expect_fatal_dispatch();
	}
}

static void complete_mutations_are_fatal(void)
{
	for (unsigned int phase = 0U; phase < 3U; phase++) {
		for (unsigned int mode = 1U; mode <= 9U; mode++) {
			reset_fixture();
			provision();
			if (phase) {
				request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
				assert(payload_mm_authvar_presence_transaction_dispatch(&slot) ==
					CB_SUCCESS);
			}
			request(phase == 1U ?
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT :
				phase == 2U ?
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT :
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
			complete_storage_mutation = mode;
			fatal_claims = phase ? 2U : 1U;
			fatal_prepares = 1U;
			fatal_commits = phase == 1U ? 1U : 0U;
			fatal_aborts = phase == 2U ? 1U : 0U;
			fatal_completes = phase ? 2U : 1U;
			fatal_expected = FATAL_COMPLETE_MUTATION;
			expect_fatal_dispatch();
		}
	}
}

static void orphan_states_are_fatal(void)
{
	for (unsigned int index = 0; index < 7U; index++) {
		reset_fixture();
		provision();
		payload_mm_authvar_presence_transaction_test_set_orphan_state(&slot,
			index);
		fatal_expected = FATAL_PROVISIONED_CORRUPT;
		expect_fatal_dispatch();
	}
}

static void proof_mutations_are_rejected(void)
{
	for (unsigned int call = 1U; call <= 6U; call += 5U) {
		reset_fixture();
		mutate_proof_call = call;
		assert(provision_status() == CB_ERR);
		assert(zero(&slot, sizeof(slot)));
	}
}

static void protected_corruption_is_fatal(void)
{
	pid_t child;
	int status;

	for (unsigned int mode = 0; mode < 7U; mode++) {
		reset_fixture();
		provision();
		request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
		if (mode == 0U)
			slot.page_base = 8U;
		else if (mode == 1U)
			slot.ack_published = 1U;
		else if (mode == 2U)
			slot.reserved[0] = 1U;
		else if (mode == 3U)
			slot.binding.generation++;
		else if (mode == 4U)
			slot.policy.size++;
		else if (mode == 5U)
			slot.policy.context = &page;
		else
			slot.failure.generation++;
		fatal_expected = FATAL_PROVISIONED_CORRUPT;
		child = fork();
		assert(child >= 0);
		if (!child) {
			(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
			_exit(0);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);
	}

	for (unsigned int mode = 0; mode < 6U; mode++) {
		reset_fixture();
		provision();
		request(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
		assert(payload_mm_authvar_presence_transaction_dispatch(&slot) ==
			CB_SUCCESS);
		if (mode == 0U)
			slot.page_base = 8U;
		else if (mode == 1U)
			slot.ack_published = 0U;
		else if (mode == 2U)
			slot.reserved[1] = 1U;
		else if (mode == 3U)
			slot.binding.generation++;
		else if (mode == 4U)
			slot.policy.size++;
		else
			slot.context[0] ^= 1U;
		fatal_expected = FATAL_PREPARED_CORRUPT;
		child = fork();
		assert(child >= 0);
		if (!child) {
			(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
			_exit(0);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);
	}

	for (unsigned int mode = 0; mode < 4U; mode++) {
		reset_fixture();
		provision();
		if (mode == 0U)
			slot.failure.callback = NULL;
		else if (mode == 1U)
			slot.failure.context_size =
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX + 1U;
		else if (mode == 2U)
			slot.failure.generation = 0;
		else
			slot.failure.reserved[0] = 1U;
		child = fork();
		assert(child >= 0);
		if (!child) {
			(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
			_exit(0);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFSIGNALED(status));
	}
}

static void callback_protection_is_required(void)
{
	const void *callbacks[] = {
		(const void *)(uintptr_t)protected,
		(const void *)(uintptr_t)prepare,
		(const void *)(uintptr_t)commit,
		(const void *)(uintptr_t)abort_transaction,
		(const void *)(uintptr_t)dma,
		(const void *)(uintptr_t)claim,
		(const void *)(uintptr_t)complete_invocation,
		(const void *)(uintptr_t)fail_stop,
	};

	for (size_t index = 0; index < ARRAY_SIZE(callbacks); index++) {
		reset_fixture();
		unprotected_object = callbacks[index];
		assert(provision_status() == CB_ERR);
		assert(zero(&slot, sizeof(slot)));
	}
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
		fatal_expected = FATAL_PREPARE_MUTATION;
		(void)payload_mm_authvar_presence_transaction_dispatch(&slot);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);
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
		fatal_expected = FATAL_PROVISION;
		provision();
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);
}

int main(void)
{
	success();
	abort_path();
#if !defined(REAL_RECEIPT)
	failures();
	terminal_and_dirty_slot();
	contention_and_reentry();
	publish_failure_is_fatal();
	fatal_boundaries();
	protected_corruption_is_fatal();
	callback_protection_is_required();
	proof_mutations_are_rejected();
	protected_callback_mutations_are_fatal();
	callback_owner_loss_is_fatal();
	complete_mutations_are_fatal();
	orphan_states_are_fatal();
	state_laundering_is_fatal();
	provision_mutation_is_fatal();
#endif
	return 0;
}
