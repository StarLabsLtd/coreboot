/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_tuple_sender.h>
#include <commonlib/helpers.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

enum response_mode {
	RESPONSE_EXACT,
	RESPONSE_ABORT,
	RESPONSE_SENTINEL,
	RESPONSE_BAD_ACK,
	RESPONSE_DIRTY_REQUEST,
	RESPONSE_DIRTY_RESERVED,
	RESPONSE_MUTATE_BINDING,
	RESPONSE_MUTATE_SEED,
	RESPONSE_MUTATE_SENDER,
#if TEST_PRE_TRIGGER_HOOK_ENABLED
	RESPONSE_PRE_MUTATE_BINDING,
	RESPONSE_PRE_MUTATE_SEED,
	RESPONSE_PRE_MUTATE_SENDER,
#endif
};

static struct payload_mm_authvar_presence_transaction_page page
	__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE);
static uint8_t backing[4096] __aligned(4096);
static struct payload_mm_authvar_presence_tuple_sender sender;
static struct payload_mm_authvar_presence_transaction_binding binding;
static struct payload_mm_authvar_presence_seed seed;
static enum response_mode mode;
static unsigned int trigger_calls;

#if TEST_PRE_TRIGGER_HOOK_ENABLED
void tuple_sender_before_trigger(void)
{
	if (mode == RESPONSE_PRE_MUTATE_BINDING)
		binding.nonce++;
	if (mode == RESPONSE_PRE_MUTATE_SEED)
		seed.capability[0]++;
	if (mode == RESPONSE_PRE_MUTATE_SENDER)
		sender.size++;
}
#endif

static bool all_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}

static void make_ack(
	const struct payload_mm_authvar_presence_transaction_binding *request_binding,
	uint32_t decision)
{
	page.ack = (struct payload_mm_authvar_presence_transaction_ack) {
		.binding = *request_binding,
		.decision = decision,
		.transport_status =
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ACCEPTED,
		.operation_status =
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ACCEPTED,
		.backing_status = decision ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT ?
			PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_CLEANED :
			PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_TRANSFERRED,
	};
}

uint64_t smm_invocation_tuple_trigger(void)
{
	struct payload_mm_authvar_presence_transaction_binding request_binding =
		page.request.binding;
	uint32_t decision = page.request.decision;

	trigger_calls++;
	assert(page.request.transport_status == 0U);
	assert(page.request.operation_status == 0U);
	assert(page.request.reserved == 0U);
	assert(all_zero(&page.ack, sizeof(page.ack)));
	assert(all_zero(page.reserved, sizeof(page.reserved)));
	if (decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE) {
		assert(!memcmp(&page.request.seed, &seed, sizeof(seed)));
	} else {
		assert(all_zero(&page.request.seed, sizeof(page.request.seed)));
	}
	memset(&page, 0, sizeof(page));
	if (mode == RESPONSE_ABORT)
		decision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT;
	make_ack(&request_binding, decision);
	if (mode == RESPONSE_SENTINEL)
		return PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_SENTINEL;
	if (mode == RESPONSE_BAD_ACK)
		page.ack.binding.nonce++;
	if (mode == RESPONSE_DIRTY_REQUEST)
		page.request.decision = decision;
	if (mode == RESPONSE_DIRTY_RESERVED)
		page.reserved[sizeof(page.reserved) - 1U] = 1U;
	if (mode == RESPONSE_MUTATE_BINDING)
		binding.nonce++;
	if (mode == RESPONSE_MUTATE_SEED)
		seed.capability[0]++;
	if (mode == RESPONSE_MUTATE_SENDER)
		sender.size++;
	return payload_mm_authvar_presence_transaction_result(&request_binding,
		decision);
}

static void reset_fixture(void)
{
	memset(&page, 0xa5, sizeof(page));
	memset(&binding, 0, sizeof(binding));
	binding.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION;
	binding.size = sizeof(binding);
	binding.generation = 7U;
	binding.transaction_id = 11U;
	binding.nonce = 13U;
	binding.initiator_cpu = 0U;
	binding.maximum_cpus = 4U;
	for (size_t index = 0; index < sizeof(binding.capability); index++)
		binding.capability[index] = (uint8_t)(index + 1U);
	memset(&seed, 0, sizeof(seed));
	seed.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION;
	seed.size = sizeof(seed);
	seed.endpoint.generation = binding.generation;
	seed.endpoint.communication_base = (uintptr_t)backing;
	seed.endpoint.communication_size = 80U;
	seed.backing.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION;
	seed.backing.size = sizeof(seed.backing);
	seed.backing.base = (uintptr_t)backing;
	seed.backing.bytes = sizeof(backing);
	seed.backing.generation = binding.generation;
	sender = (struct payload_mm_authvar_presence_tuple_sender) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER_REVISION,
		.size = sizeof(sender),
		.page = &page,
	};
	mode = RESPONSE_EXACT;
	trigger_calls = 0U;
}

static void expect_page_clean(void)
{
	assert(all_zero(&page, sizeof(page)));
}

static void expect_zero_result(
	const struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t saved_value)
{
	assert(all_zero(ack, sizeof(*ack)));
	assert(saved_value == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_SENTINEL);
}

static void exact_sequence(void)
{
	struct payload_mm_authvar_presence_transaction_ack ack;
	uint64_t saved_value;

	reset_fixture();
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_SUCCESS);
	assert(trigger_calls == 1U);
	assert(payload_mm_authvar_presence_transaction_ack_valid(&binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE, &ack,
		saved_value));
	expect_page_clean();
	assert(payload_mm_authvar_presence_tuple_sender_commit(&sender, &binding,
		&ack, &saved_value) == CB_SUCCESS);
	assert(trigger_calls == 2U);
	assert(payload_mm_authvar_presence_transaction_ack_valid(&binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT, &ack,
		saved_value));
	expect_page_clean();

	reset_fixture();
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_tuple_sender_abort(&sender, &binding,
		&ack, &saved_value) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_transaction_ack_valid(&binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT, &ack,
		saved_value));
	expect_page_clean();
}

static void receiver_abort(void)
{
	struct payload_mm_authvar_presence_transaction_ack ack;
	uint64_t saved_value;

	reset_fixture();
	mode = RESPONSE_ABORT;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_ERR);
	assert(trigger_calls == 1U);
	assert(payload_mm_authvar_presence_transaction_ack_valid(&binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT, &ack,
		saved_value));
	expect_page_clean();
}

static void decide_response_failures(void)
{
	static const enum response_mode failures[] = {
		RESPONSE_ABORT,
		RESPONSE_SENTINEL,
		RESPONSE_BAD_ACK,
		RESPONSE_DIRTY_REQUEST,
		RESPONSE_DIRTY_RESERVED,
	};

	for (size_t index = 0; index < ARRAY_SIZE(failures); index++) {
		struct payload_mm_authvar_presence_transaction_ack ack;
		uint64_t saved_value;

		reset_fixture();
		mode = failures[index];
		memset(&ack, 0xa5, sizeof(ack));
		saved_value = 0;
		assert(payload_mm_authvar_presence_tuple_sender_commit(&sender,
			&binding, &ack, &saved_value) == CB_ERR);
		assert(trigger_calls == 1U);
		expect_zero_result(&ack, saved_value);
		expect_page_clean();

		reset_fixture();
		mode = failures[index];
		memset(&ack, 0xa5, sizeof(ack));
		saved_value = 0;
		assert(payload_mm_authvar_presence_tuple_sender_abort(&sender,
			&binding, &ack, &saved_value) ==
			(failures[index] == RESPONSE_ABORT ? CB_SUCCESS : CB_ERR));
		assert(trigger_calls == 1U);
		if (failures[index] == RESPONSE_ABORT) {
			assert(payload_mm_authvar_presence_transaction_ack_valid(
				&binding,
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT,
				&ack, saved_value));
		} else {
			expect_zero_result(&ack, saved_value);
		}
		expect_page_clean();
	}
}

static void response_failures(void)
{
	static const enum response_mode failures[] = {
		RESPONSE_SENTINEL,
		RESPONSE_BAD_ACK,
		RESPONSE_DIRTY_REQUEST,
		RESPONSE_DIRTY_RESERVED,
		RESPONSE_MUTATE_BINDING,
		RESPONSE_MUTATE_SEED,
		RESPONSE_MUTATE_SENDER,
	};

	for (size_t index = 0; index < ARRAY_SIZE(failures); index++) {
		struct payload_mm_authvar_presence_transaction_ack ack;
		uint64_t saved_value;

		reset_fixture();
		mode = failures[index];
		memset(&ack, 0xa5, sizeof(ack));
		saved_value = 0;
		assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender,
			&seed, &binding, &ack, &saved_value) == CB_ERR);
		assert(trigger_calls == 1U);
		expect_zero_result(&ack, saved_value);
		expect_page_clean();
	}
}

static void preflight_failures(void)
{
	struct payload_mm_authvar_presence_transaction_ack ack;
	uint64_t saved_value;
	struct payload_mm_authvar_presence_transaction_page before;

	reset_fixture();
	before = page;
	sender.revision++;
	memset(&ack, 0xa5, sizeof(ack));
	saved_value = 0;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_ERR);
	assert(!trigger_calls && !memcmp(&before, &page, sizeof(page)));
	expect_zero_result(&ack, saved_value);

	reset_fixture();
	before = page;
	binding.reserved[0] = 1U;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_ERR);
	assert(!trigger_calls && !memcmp(&before, &page, sizeof(page)));
	expect_zero_result(&ack, saved_value);

	reset_fixture();
	before = page;
	seed.backing.base = (uintptr_t)&page;
	seed.backing.bytes = sizeof(page);
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_ERR);
	assert(!trigger_calls && !memcmp(&before, &page, sizeof(page)));
	expect_zero_result(&ack, saved_value);

	reset_fixture();
	before = page;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &page.ack, &saved_value) == CB_ERR);
	assert(!trigger_calls && !memcmp(&before, &page, sizeof(page)));

	reset_fixture();
	before = page;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender,
		&page.request.seed, &binding, &ack, &saved_value) == CB_ERR);
	assert(!trigger_calls && !memcmp(&before, &page, sizeof(page)));

	reset_fixture();
	before = page;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&page.request.binding, &ack, &saved_value) == CB_ERR);
	assert(!trigger_calls && !memcmp(&before, &page, sizeof(page)));
}

static void expect_structural_rejection(
	void *context, const struct payload_mm_authvar_presence_seed *seed_pointer,
	const struct payload_mm_authvar_presence_transaction_binding *binding_pointer,
	struct payload_mm_authvar_presence_transaction_ack *ack_pointer,
	uint64_t *value_pointer)
{
	struct payload_mm_authvar_presence_transaction_page page_before = page;
	struct payload_mm_authvar_presence_tuple_sender sender_before = sender;
	struct payload_mm_authvar_presence_transaction_binding binding_before = binding;
	struct payload_mm_authvar_presence_seed seed_before = seed;
	struct payload_mm_authvar_presence_transaction_ack ack;
	struct payload_mm_authvar_presence_transaction_ack ack_before;
	uint64_t saved_value = 0x8877665544332211ULL;

	memset(&ack, 0xa5, sizeof(ack));
	ack_before = ack;
	if (!ack_pointer)
		ack_pointer = &ack;
	if (!value_pointer)
		value_pointer = &saved_value;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(context,
		seed_pointer, binding_pointer, ack_pointer, value_pointer) == CB_ERR);
	assert(trigger_calls == 0U);
	assert(!memcmp(&page, &page_before, sizeof(page)));
	assert(!memcmp(&sender, &sender_before, sizeof(sender)));
	assert(!memcmp(&binding, &binding_before, sizeof(binding)));
	assert(!memcmp(&seed, &seed_before, sizeof(seed)));
	assert(!memcmp(&ack, &ack_before, sizeof(ack)));
	assert(saved_value == 0x8877665544332211ULL);
}

static void structural_preflight_failures(void)
{
	struct payload_mm_authvar_presence_transaction_ack ack;
	struct payload_mm_authvar_presence_transaction_ack ack_before;
	struct payload_mm_authvar_presence_transaction_page page_before;
	uint64_t saved_value;
	uintptr_t overflow;

	/* Null, misaligned and wrapping objects are rejected before any access. */
	reset_fixture();
	memset(&ack, 0xa5, sizeof(ack));
	saved_value = 0x8877665544332211ULL;
	expect_structural_rejection(NULL, &seed, &binding, &ack, &saved_value);
	assert(!all_zero(&ack, sizeof(ack)) &&
		saved_value == 0x8877665544332211ULL);
	reset_fixture();
	expect_structural_rejection(&sender, NULL, &binding, NULL, NULL);
	reset_fixture();
	expect_structural_rejection(&sender, &seed, NULL, NULL, NULL);
	reset_fixture();
	page_before = page;
	saved_value = 0x8877665544332211ULL;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, NULL, &saved_value) == CB_ERR);
	assert(!trigger_calls && !memcmp(&page, &page_before, sizeof(page)));
	assert(saved_value == 0x8877665544332211ULL);
	reset_fixture();
	page_before = page;
	memset(&ack, 0xa5, sizeof(ack));
	ack_before = ack;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, NULL) == CB_ERR);
	assert(!trigger_calls && !memcmp(&page, &page_before, sizeof(page)));
	assert(!memcmp(&ack, &ack_before, sizeof(ack)));
	reset_fixture();
	expect_structural_rejection(&sender, &seed, &binding, NULL,
		(uint64_t *)((uintptr_t)&saved_value + 1U));
	reset_fixture();
	expect_structural_rejection((uint8_t *)&sender + 1U, &seed, &binding,
		NULL, NULL);
	reset_fixture();
	expect_structural_rejection(&sender,
		(const void *)((uintptr_t)&seed + 1U), &binding, NULL, NULL);
	reset_fixture();
	expect_structural_rejection(&sender, &seed,
		(const void *)((uintptr_t)&binding + 1U), NULL, NULL);
	reset_fixture();
	expect_structural_rejection(&sender, &seed, &binding,
		(void *)((uintptr_t)&ack + 1U), NULL);
	overflow = UINTPTR_MAX & ~(uintptr_t)7U;
	reset_fixture();
	expect_structural_rejection((void *)overflow, &seed, &binding, NULL, NULL);
	reset_fixture();
	expect_structural_rejection(&sender, (const void *)overflow, &binding,
		NULL, NULL);
	reset_fixture();
	expect_structural_rejection(&sender, &seed, (const void *)overflow,
		NULL, NULL);
	reset_fixture();
	expect_structural_rejection(&sender, &seed, &binding, (void *)overflow,
		NULL);

	/* Every pairwise callback-object alias is rejected before output reset. */
	reset_fixture();
	expect_structural_rejection(&sender, &seed, (const void *)&sender,
		NULL, NULL);
	reset_fixture();
	expect_structural_rejection(&sender, &seed, &binding, (void *)&sender,
		NULL);
	reset_fixture();
	expect_structural_rejection(&sender, &seed, &binding, NULL,
		(void *)&sender);
	reset_fixture();
	expect_structural_rejection(&sender, (const void *)&sender, &binding,
		NULL, NULL);
	reset_fixture();
	expect_structural_rejection(&sender, (const void *)&binding, &binding,
		NULL, NULL);
	reset_fixture();
	expect_structural_rejection(&sender, &seed, &binding, (void *)&binding,
		NULL);
	reset_fixture();
	expect_structural_rejection(&sender, &seed, &binding, NULL,
		(void *)&binding);
	reset_fixture();
	memset(&ack, 0xa5, sizeof(ack));
	ack_before = ack;
	saved_value = 0x8877665544332211ULL;
	expect_structural_rejection(&sender, (const void *)&ack, &binding, &ack,
		&saved_value);
	assert(!memcmp(&ack, &ack_before, sizeof(ack)));
	assert(saved_value == 0x8877665544332211ULL);
	reset_fixture();
	memset(&ack, 0xa5, sizeof(ack));
	ack_before = ack;
	saved_value = 0x8877665544332211ULL;
	expect_structural_rejection(&sender, (const void *)&saved_value, &binding,
		&ack, &saved_value);
	assert(!memcmp(&ack, &ack_before, sizeof(ack)));
	assert(saved_value == 0x8877665544332211ULL);
	reset_fixture();
	memset(&ack, 0xa5, sizeof(ack));
	ack_before = ack;
	expect_structural_rejection(&sender, &seed, &binding, &ack,
		(void *)&ack);
	assert(!memcmp(&ack, &ack_before, sizeof(ack)));
}

static void physical_overlap_boundaries(void)
{
	struct payload_mm_authvar_presence_transaction_ack ack;
	struct payload_mm_authvar_presence_transaction_page before;
	uint64_t saved_value;
	uintptr_t page_base = (uintptr_t)&page;

	/* Partial overlap from either side and wrapping ranges fail pre-trigger. */
	reset_fixture();
	before = page;
	seed.endpoint.communication_base = page_base - 1U;
	seed.endpoint.communication_size = 2U;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_ERR);
	assert(!trigger_calls && !memcmp(&page, &before, sizeof(page)));
	reset_fixture();
	before = page;
	seed.endpoint.communication_base = page_base + sizeof(page) - 1U;
	seed.endpoint.communication_size = 2U;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_ERR);
	assert(!trigger_calls && !memcmp(&page, &before, sizeof(page)));
	reset_fixture();
	before = page;
	seed.backing.base = page_base - 1U;
	seed.backing.bytes = 2U;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_ERR);
	assert(!trigger_calls && !memcmp(&page, &before, sizeof(page)));
	reset_fixture();
	before = page;
	seed.backing.base = page_base + sizeof(page) - 1U;
	seed.backing.bytes = 2U;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_ERR);
	assert(!trigger_calls && !memcmp(&page, &before, sizeof(page)));
	reset_fixture();
	before = page;
	seed.backing.base = UINT64_MAX - 1U;
	seed.backing.bytes = 4U;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_ERR);
	assert(!trigger_calls && !memcmp(&page, &before, sizeof(page)));
	reset_fixture();
	before = page;
	seed.endpoint.communication_base = UINT64_MAX - 1U;
	seed.endpoint.communication_size = 4U;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_ERR);
	assert(!trigger_calls && !memcmp(&page, &before, sizeof(page)));

	/* Exact adjacency and empty ranges do not overlap the transaction page. */
	reset_fixture();
	seed.endpoint.communication_base = page_base - 1U;
	seed.endpoint.communication_size = 1U;
	seed.backing.base = page_base + sizeof(page);
	seed.backing.bytes = 1U;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_SUCCESS);
	assert(trigger_calls == 1U);
	expect_page_clean();
	reset_fixture();
	seed.endpoint.communication_base = page_base + sizeof(page);
	seed.endpoint.communication_size = 1U;
	seed.backing.base = page_base - 1U;
	seed.backing.bytes = 1U;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_SUCCESS);
	assert(trigger_calls == 1U);
	expect_page_clean();
	reset_fixture();
	seed.endpoint.communication_base = page_base;
	seed.endpoint.communication_size = 0U;
	seed.backing.base = page_base;
	seed.backing.bytes = 0U;
	assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender, &seed,
		&binding, &ack, &saved_value) == CB_SUCCESS);
	assert(trigger_calls == 1U);
	expect_page_clean();
}

#if TEST_PRE_TRIGGER_HOOK_ENABLED
static void pre_trigger_rechecks(void)
{
	static const enum response_mode failures[] = {
		RESPONSE_PRE_MUTATE_BINDING,
		RESPONSE_PRE_MUTATE_SEED,
		RESPONSE_PRE_MUTATE_SENDER,
	};

	for (size_t index = 0; index < ARRAY_SIZE(failures); index++) {
		struct payload_mm_authvar_presence_transaction_ack ack;
		uint64_t saved_value;

		reset_fixture();
		mode = failures[index];
		assert(payload_mm_authvar_presence_tuple_sender_prepare(&sender,
			&seed, &binding, &ack, &saved_value) == CB_ERR);
		assert(trigger_calls == 0U);
		expect_zero_result(&ack, saved_value);
		expect_page_clean();
	}
}
#endif

int main(void)
{
	exact_sequence();
	receiver_abort();
	response_failures();
	decide_response_failures();
	preflight_failures();
	structural_preflight_failures();
	physical_overlap_boundaries();
#if TEST_PRE_TRIGGER_HOOK_ENABLED
	pre_trigger_rechecks();
#endif
	return 0;
}
