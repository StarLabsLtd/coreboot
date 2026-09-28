/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/lib/payload_mm_authvar_presence_lifecycle_close_internal.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define TEST_GENERATION 0x1020304050607080ULL

static jmp_buf failure;
static unsigned int claim_calls;
static unsigned int complete_calls;
static unsigned int restrict_calls;
static unsigned int fail_calls;
static unsigned int claim_failure;
static unsigned int complete_failure;
static unsigned int restrict_failure_call;
static unsigned int invalid_field;
static uint64_t smi_generation;
static uint64_t completed_values[5];
static struct payload_mm_authvar_presence_transaction_invocation claimed;
static atomic_bool block_claim;
static atomic_bool claim_entered;
static uint32_t context_word;
static uint64_t claim_values[5];
static struct payload_mm_authvar_presence_transaction_slot transaction_slot;
static bool transaction_enabled;
static bool freeze_generation;
static bool dirty_claim_error;
static bool mutate_completion_token;
static unsigned int proof_mutation;
static unsigned int proof_calls;

static bool protected(void *context, const void *base, size_t size)
{
	size_t owner_size;
	uint8_t *owner_state;

	(void)context;
	proof_calls++;
	if (proof_mutation && proof_calls == 6) {
		if (proof_mutation == 1) {
			owner_state = (uint8_t *)(uintptr_t)
				payload_mm_authvar_presence_lifecycle_close_state_test(
					&owner_size);
			assert(owner_size);
			owner_state[0] = 1;
		} else {
			payload_mm_authvar_presence_warm_reset_close_slot()->state = 1;
		}
	}
	return base && size;
}

static enum cb_err claim(void *context, uint64_t sentinel,
	struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	assert(context && *(uint32_t *)context == context_word);
	assert(sentinel);
	if (claim_calls < 5)
		claim_values[claim_calls] = sentinel;
	claim_calls++;
	if (claim_failure) {
		memset(invocation, 0, sizeof(*invocation));
		if (dirty_claim_error)
			invocation->reserved = 1;
		return CB_ERR;
	}
	atomic_store_explicit(&claim_entered, true, memory_order_release);
	while (atomic_load_explicit(&block_claim, memory_order_acquire))
		;
	*invocation = (struct payload_mm_authvar_presence_transaction_invocation) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_INVOCATION_REVISION,
		.size = sizeof(*invocation),
		.initiator_cpu = 0,
		.active_cpus = 4,
		.smi_generation = freeze_generation ? smi_generation : ++smi_generation,
		.rendezvous_generation = smi_generation,
		.rendezvous_proof = { 1, 2, 3 },
		.bsp = 1,
	};
	switch (invalid_field) {
	case 1:
		invocation->revision++;
		break;
	case 2:
		invocation->size--;
		break;
	case 3:
		invocation->initiator_cpu++;
		break;
	case 4:
		invocation->active_cpus++;
		break;
	case 5:
		invocation->smi_generation = 0;
		break;
	case 6:
		invocation->rendezvous_generation++;
		break;
	case 7:
		memset(invocation->rendezvous_proof, 0,
			sizeof(invocation->rendezvous_proof));
		break;
	case 8:
		invocation->bsp = 0;
		break;
	case 9:
		invocation->reserved = 1;
		break;
	default:
		break;
	}
	claimed = *invocation;
	return CB_SUCCESS;
}

static enum cb_err complete(void *context,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	uint64_t value)
{
	assert(context && *(uint32_t *)context == context_word);
	assert(!memcmp(invocation, &claimed, sizeof(claimed)));
	assert(value);
	if (complete_calls < 5)
		completed_values[complete_calls] = value;
	complete_calls++;
	if (mutate_completion_token)
		((struct payload_mm_authvar_presence_transaction_invocation *)
		 invocation)->reserved = 1;
	return complete_failure ? CB_ERR : CB_SUCCESS;
}

void smm_invocation_platform_fail_stop(void)
{
	fail_calls++;
	longjmp(failure, 1);
}

enum cb_err payload_mm_authvar_presence_authority_restrict(uint64_t generation)
{
	restrict_calls++;
	assert(generation == TEST_GENERATION);
	return restrict_failure_call == restrict_calls ? CB_ERR : CB_SUCCESS;
}

struct payload_mm_authvar_presence_transaction_slot *
smm_get_payload_mm_authvar_presence_transaction_slot(void)
{
	return &transaction_slot;
}

bool payload_mm_authvar_presence_transaction_dispatch_enabled(
	const struct payload_mm_authvar_presence_transaction_slot *slot,
	uint64_t generation)
{
	return transaction_enabled && slot == &transaction_slot &&
		generation == TEST_GENERATION;
}

static struct payload_mm_authvar_presence_lifecycle_close_policy policy(void)
{
	return (struct payload_mm_authvar_presence_lifecycle_close_policy) {
		.revision =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_POLICY_REVISION,
		.size = sizeof(struct payload_mm_authvar_presence_lifecycle_close_policy),
		.generation = TEST_GENERATION,
		.predecessor_invocation_generation = 10,
		.initiator_cpu = 0,
		.maximum_cpus = 4,
		.transaction_slot = &transaction_slot,
		.claim_invocation = claim,
		.complete_invocation = complete,
		.context = &context_word,
		.context_size = sizeof(context_word),
	};
}

static void reset(void)
{
	payload_mm_authvar_presence_lifecycle_close_reset_test();
	claim_calls = 0;
	complete_calls = 0;
	restrict_calls = 0;
	fail_calls = 0;
	claim_failure = 0;
	complete_failure = 0;
	restrict_failure_call = 0;
	invalid_field = 0;
	smi_generation = 10;
	context_word = 0xa55a5aa5;
	transaction_enabled = true;
	freeze_generation = false;
	dirty_claim_error = false;
	mutate_completion_token = false;
	proof_mutation = 0;
	proof_calls = 0;
	memset(&transaction_slot, 0, sizeof(transaction_slot));
	memset(completed_values, 0, sizeof(completed_values));
	memset(claim_values, 0, sizeof(claim_values));
	memset(&claimed, 0, sizeof(claimed));
	atomic_store(&block_claim, false);
	atomic_store(&claim_entered, false);
}

static void provision(void)
{
	struct payload_mm_authvar_presence_lifecycle_close_policy p = policy();

	assert(payload_mm_authvar_presence_lifecycle_close_provision(&p,
		protected, NULL) == CB_SUCCESS);
}

static void expect_fail_stop(enum cb_err (*operation)(void))
{
	if (!setjmp(failure)) {
		(void)operation();
		abort();
	}
	assert(fail_calls == 1);
}

static void basics(void)
{
	static enum cb_err (*const operations[])(void) = {
		payload_mm_authvar_presence_pre_external_image_close,
		payload_mm_authvar_presence_payload_failure_or_return_close,
		payload_mm_authvar_presence_warm_reset_close,
		payload_mm_authvar_presence_s3_resume_close,
		payload_mm_authvar_presence_closed_reproof,
	};
	struct payload_mm_authvar_presence_lifecycle_close_policy p;

	reset();
	assert(operations[0]() == CB_ERR);
	p = policy();
	assert(payload_mm_authvar_presence_lifecycle_close_provision(&p,
		protected, NULL) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_close_provision(&p,
		protected, NULL) == CB_ERR);
	for (size_t i = 0; i < 5; i++)
		assert(operations[i]() == CB_SUCCESS);
	assert(claim_calls == 5 && complete_calls == 5 && restrict_calls == 10);
	for (size_t i = 0; i < 5; i++) {
		assert(completed_values[i]);
		assert(claim_values[i]);
		for (size_t j = 0; j < i; j++)
			assert(completed_values[i] != completed_values[j] &&
				claim_values[i] != claim_values[j]);
	}
	for (size_t i = 0; i < 5; i++) {
		uint64_t value =
			payload_mm_authvar_presence_lifecycle_close_value_test(
				claim_values[i], TEST_GENERATION, &claimed);
		for (size_t j = 0; j < i; j++)
			assert(value !=
				payload_mm_authvar_presence_lifecycle_close_value_test(
					claim_values[j], TEST_GENERATION, &claimed));
	}
	expect_fail_stop(payload_mm_authvar_presence_pre_external_image_close);
}

static void invalid_policies(void)
{
	struct payload_mm_authvar_presence_lifecycle_close_policy p;

	for (unsigned int invalid = 0; invalid < 9; invalid++) {
		reset();
		p = policy();
		switch (invalid) {
		case 0:
			p.revision++;
			break;
		case 1:
			p.size--;
			break;
		case 2:
			p.generation = 0;
			break;
		case 3:
			p.predecessor_invocation_generation = 0;
			break;
		case 4:
			p.maximum_cpus = 0;
			break;
		case 5:
			p.initiator_cpu = p.maximum_cpus;
			break;
		case 6:
			p.claim_invocation = NULL;
			break;
		case 7:
			p.complete_invocation = NULL;
			break;
		case 8:
			p.context_size =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX + 1;
			break;
		}
		assert(payload_mm_authvar_presence_lifecycle_close_provision(&p,
			protected, NULL) == CB_ERR);
	}
	reset();
	p = policy();
	p.context = NULL;
	assert(payload_mm_authvar_presence_lifecycle_close_provision(&p,
		protected, NULL) == CB_ERR);
	reset();
	p = policy();
	assert(payload_mm_authvar_presence_lifecycle_close_provision(&p,
		NULL, NULL) == CB_ERR);
	reset();
	p = policy();
	transaction_enabled = false;
	assert(payload_mm_authvar_presence_lifecycle_close_provision(&p,
		protected, NULL) == CB_ERR);
	reset();
	p = policy();
	p.transaction_slot = (void *)(uintptr_t)&context_word;
	assert(payload_mm_authvar_presence_lifecycle_close_provision(&p,
		protected, NULL) == CB_ERR);
}

static void preclaim_error_is_retryable(void)
{
	reset();
	provision();
	claim_failure = 1;
	assert(payload_mm_authvar_presence_warm_reset_close() == CB_ERR);
	assert(!restrict_calls && !complete_calls && !fail_calls);
	claim_failure = 0;
	assert(payload_mm_authvar_presence_warm_reset_close() == CB_SUCCESS);
}

static void provisioning_mutation(void)
{
	struct payload_mm_authvar_presence_lifecycle_close_policy p;

	for (unsigned int mutation = 1; mutation <= 2; mutation++) {
		reset();
		p = policy();
		proof_mutation = mutation;
		assert(payload_mm_authvar_presence_lifecycle_close_provision(&p,
			protected, NULL) == CB_ERR);
	}
}

static void dirty_claim_error_fail_stops(void)
{
	reset();
	provision();
	claim_failure = 1;
	dirty_claim_error = true;
	expect_fail_stop(payload_mm_authvar_presence_warm_reset_close);
}

static void replay_and_freshness(void)
{
	reset();
	provision();
	smi_generation = 10;
	freeze_generation = true;
	expect_fail_stop(payload_mm_authvar_presence_warm_reset_close);

	reset();
	provision();
	assert(payload_mm_authvar_presence_warm_reset_close() == CB_SUCCESS);
	freeze_generation = true;
	expect_fail_stop(payload_mm_authvar_presence_s3_resume_close);

	reset();
	provision();
	smi_generation = UINT64_MAX - 1U;
	expect_fail_stop(payload_mm_authvar_presence_warm_reset_close);
}

static void reproof_ordering(void)
{
	reset();
	provision();
	expect_fail_stop(payload_mm_authvar_presence_closed_reproof);

	reset();
	provision();
	assert(payload_mm_authvar_presence_pre_external_image_close() == CB_SUCCESS);
	assert(payload_mm_authvar_presence_closed_reproof() == CB_SUCCESS);
	assert(claim_values[0] != claim_values[1]);
}

static void invalid_invocations_fail_stop(void)
{
	for (invalid_field = 1; invalid_field <= 9; invalid_field++) {
		unsigned int field = invalid_field;
		reset();
		provision();
		invalid_field = field;
		expect_fail_stop(
			payload_mm_authvar_presence_pre_external_image_close);
		assert(!restrict_calls && !complete_calls);
	}
}

static void restrict_and_complete_failures(void)
{
	for (unsigned int call = 1; call <= 2; call++) {
		reset();
		provision();
		restrict_failure_call = call;
		expect_fail_stop(payload_mm_authvar_presence_s3_resume_close);
		assert(!complete_calls);
	}
	reset();
	provision();
	complete_failure = 1;
	expect_fail_stop(payload_mm_authvar_presence_payload_failure_or_return_close);
	assert(restrict_calls == 2 && complete_calls == 1);
	reset();
	provision();
	mutate_completion_token = true;
	expect_fail_stop(payload_mm_authvar_presence_warm_reset_close);
}

static void corrupt_owner(void)
{
	size_t size;
	uint8_t *state = (uint8_t *)(uintptr_t)
		payload_mm_authvar_presence_lifecycle_close_state_test(&size);

	assert(size);
	state[0] ^= 1U;
}

static void lose_transaction_evidence(void)
{
	transaction_enabled = false;
}

static void corrupt_pre_external_slot(void)
{
	struct payload_mm_authvar_presence_lifecycle_close_slot *slot =
		payload_mm_authvar_presence_pre_external_image_close_slot();

	slot->state ^= 1U;
}

static void corrupt_warm_reset_slot(void)
{
	struct payload_mm_authvar_presence_lifecycle_close_slot *slot =
		payload_mm_authvar_presence_warm_reset_close_slot();

	slot->state ^= 1U;
}

static void mutation_failures(void)
{
	void (*const hook_setters[])(
		payload_mm_authvar_presence_lifecycle_close_test_hook_fn) = {
		payload_mm_authvar_presence_lifecycle_close_after_claim_test_hook,
		payload_mm_authvar_presence_lifecycle_close_after_restrict_test_hook,
		payload_mm_authvar_presence_lifecycle_close_after_proof_test_hook,
		payload_mm_authvar_presence_lifecycle_close_after_complete_test_hook,
	};

	for (size_t i = 0; i < 4; i++) {
		reset();
		provision();
		hook_setters[i](corrupt_owner);
		expect_fail_stop(payload_mm_authvar_presence_warm_reset_close);
	}
	reset();
	provision();
	payload_mm_authvar_presence_lifecycle_close_after_claim_test_hook(
		lose_transaction_evidence);
	expect_fail_stop(payload_mm_authvar_presence_warm_reset_close);
	reset();
	provision();
	assert(payload_mm_authvar_presence_pre_external_image_close() == CB_SUCCESS);
	payload_mm_authvar_presence_lifecycle_close_after_claim_test_hook(
		corrupt_pre_external_slot);
	expect_fail_stop(payload_mm_authvar_presence_closed_reproof);
	reset();
	provision();
	payload_mm_authvar_presence_lifecycle_close_after_restrict_test_hook(
		corrupt_warm_reset_slot);
	expect_fail_stop(payload_mm_authvar_presence_warm_reset_close);
}

static void *close_thread(void *unused)
{
	(void)unused;
	return (void *)(uintptr_t)
		payload_mm_authvar_presence_pre_external_image_close();
}

static void concurrency(void)
{
	pthread_t thread;

	reset();
	provision();
	atomic_store(&block_claim, true);
	assert(!pthread_create(&thread, NULL, close_thread, NULL));
	while (!atomic_load_explicit(&claim_entered, memory_order_acquire))
		;
	expect_fail_stop(payload_mm_authvar_presence_s3_resume_close);
	atomic_store(&block_claim, false);
	assert(!pthread_join(thread, NULL));
	assert(fail_calls == 1 && claim_calls == 1 && complete_calls == 1 &&
		restrict_calls == 2);
}

int main(void)
{
	basics();
	invalid_policies();
	preclaim_error_is_retryable();
	provisioning_mutation();
	dirty_claim_error_fail_stops();
	replay_and_freshness();
	reproof_ordering();
	invalid_invocations_fail_stop();
	restrict_and_complete_failures();
	mutation_failures();
	concurrency();
	return 0;
}
