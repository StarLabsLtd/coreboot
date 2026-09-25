/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_clear.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

static void *mutate_object;
static size_t mutate_size;

int memcmp(const void *left, const void *right, size_t size)
{
	const unsigned char *left_bytes = left;
	const unsigned char *right_bytes = right;
	int result = 0;

	if (mutate_object && size == mutate_size) {
		((unsigned char *)mutate_object)[0] ^= 1;
		mutate_object = NULL;
	}
	for (size_t index = 0; index < size; index++) {
		if (left_bytes[index] == right_bytes[index])
			continue;
		result = left_bytes[index] < right_bytes[index] ? -1 : 1;
		break;
	}
	return result;
}

static void assert_zero(const void *object, size_t size)
{
	const unsigned char *bytes = object;

	for (size_t index = 0; index < size; index++)
		CHECK(bytes[index] == 0);
}

static struct payload_mm_authvar_mor_clear_inventory valid_inventory(void)
{
	struct payload_mm_authvar_mor_clear_inventory inventory = { 0 };

	inventory.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	inventory.size = sizeof(inventory);
	inventory.generation = 7;
	inventory.identity[0] = 0xa5;
	inventory.span_count = 4;
	inventory.spans[0] = (struct payload_mm_authvar_mor_grant_span) {
		.base = 0x5000, .size = 0x1000,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
	};
	inventory.spans[1] = (struct payload_mm_authvar_mor_grant_span) {
		.base = 0x1000, .size = 0x1000,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
	};
	inventory.spans[2] = (struct payload_mm_authvar_mor_grant_span) {
		.base = 0x2000, .size = 0x1000,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
	};
	inventory.spans[3] = (struct payload_mm_authvar_mor_grant_span) {
		.base = 0x8000, .size = 0x800,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	return inventory;
}

static struct payload_mm_authvar_mor_clear_plan valid_plan(void)
{
	struct payload_mm_authvar_mor_clear_inventory inventory = valid_inventory();
	struct payload_mm_authvar_mor_clear_plan plan;

	CHECK(payload_mm_authvar_mor_clear_plan_build(&inventory, &plan) ==
		CB_SUCCESS);
	return plan;
}

static struct payload_mm_authvar_mor_clear_facts valid_facts(
	const struct payload_mm_authvar_mor_clear_plan *plan)
{
	struct payload_mm_authvar_mor_clear_facts facts = { 0 };

	facts.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	facts.size = sizeof(facts);
	facts.cold_boot_generation = 11;
	facts.dma_before.generation = 13;
	facts.dma_before.identity[0] = 0x5a;
	facts.dma_after = facts.dma_before;
	facts.inventory_generation = plan->inventory_generation;
	memcpy(facts.inventory_identity, plan->inventory_identity,
		sizeof(facts.inventory_identity));
	return facts;
}

static struct payload_mm_authvar_mor_clear_transcript valid_transcript(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	const struct payload_mm_authvar_mor_clear_facts *facts)
{
	struct payload_mm_authvar_mor_clear_transcript transcript = { 0 };

	transcript.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	transcript.size = sizeof(transcript);
	transcript.cold_boot_generation = facts->cold_boot_generation;
	transcript.entry = *entry;
	transcript.dma_before = facts->dma_before;
	transcript.dma_after = facts->dma_after;
	transcript.inventory_generation = plan->inventory_generation;
	memcpy(transcript.inventory_identity, plan->inventory_identity,
		sizeof(transcript.inventory_identity));
	for (size_t index = 0; index < plan->span_count; index++) {
		const struct payload_mm_authvar_mor_grant_span *span = &plan->spans[index];
		struct payload_mm_authvar_mor_clear_record *record;

		if (span->span_class != PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED)
			continue;
		record = &transcript.records[transcript.cleared_span_count++];
		record->base = span->base;
		record->size = span->size;
		record->written_bytes = span->size;
		record->cache_writeback_fenced_bytes = span->size;
		record->zero_readback_bytes = span->size;
	}
	return transcript;
}

static void test_plan(void)
{
	struct payload_mm_authvar_mor_clear_inventory inventory = valid_inventory();
	struct payload_mm_authvar_mor_clear_plan plan;

	memset(&plan, 0x5a, sizeof(plan));
	CHECK(payload_mm_authvar_mor_clear_plan_build(&inventory, &plan) ==
		CB_SUCCESS);
	CHECK(plan.span_count == 3);
	CHECK(plan.spans[0].base == 0x1000 && plan.spans[0].size == 0x2000);
	CHECK(plan.spans[1].base == 0x5000);
	CHECK(plan.spans[2].base == 0x8000);
	CHECK(plan.inventory_generation == inventory.generation);

	/* More than 15 raw spans can canonicalize within the receipt bound. */
	memset(&inventory, 0, sizeof(inventory));
	inventory.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	inventory.size = sizeof(inventory);
	inventory.generation = 1;
	inventory.identity[0] = 1;
	inventory.span_count = 32;
	for (size_t index = 0; index < inventory.span_count; index++) {
		inventory.spans[index].base = (32 - index) * 0x1000;
		inventory.spans[index].size = 0x1000;
		inventory.spans[index].span_class =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED;
	}
	CHECK(payload_mm_authvar_mor_clear_plan_build(&inventory, &plan) ==
		CB_SUCCESS);
	CHECK(plan.span_count == 1 && plan.spans[0].size == 32 * 0x1000);
}

static void test_plan_reject(void)
{
	struct payload_mm_authvar_mor_clear_inventory inventory = valid_inventory();
	struct payload_mm_authvar_mor_clear_plan plan;

#define REJECT_PLAN(expression) do { \
	memset(&plan, 0x5a, sizeof(plan)); \
	expression; \
	CHECK(payload_mm_authvar_mor_clear_plan_build(&inventory, &plan) != \
		CB_SUCCESS); \
	assert_zero(&plan, sizeof(plan)); \
} while (0)
	REJECT_PLAN(inventory.spans[1].base = 0x1800);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.spans[0].size = 0);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.spans[0].base = UINT64_MAX);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.spans[0].span_class = 9);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.spans[0].exclusion_reason = 1);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.spans[3].exclusion_reason = 0);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.spans[3].exclusion_reason =
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED + 1);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.spans[4].base = 1);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.identity[0] = 0);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.revision++);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.size--);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.generation = 0);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.span_count = 0);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.reserved = 1);
	inventory = valid_inventory();
	REJECT_PLAN(inventory.spans[0].span_class =
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED;
		inventory.spans[0].exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE;
		inventory.spans[1].span_class =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED;
		inventory.spans[1].exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE;
		inventory.spans[2].span_class =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED;
		inventory.spans[2].exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE);
#undef REJECT_PLAN

	memset(&inventory, 0, sizeof(inventory));
	inventory.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	inventory.size = sizeof(inventory);
	inventory.generation = 1;
	inventory.identity[0] = 1;
	inventory.span_count = 16;
	for (size_t index = 0; index < inventory.span_count; index++) {
		inventory.spans[index].base = index * 0x2000;
		inventory.spans[index].size = 0x1000;
		inventory.spans[index].span_class =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED;
	}
	memset(&plan, 0x5a, sizeof(plan));
	CHECK(payload_mm_authvar_mor_clear_plan_build(&inventory, &plan) == CB_ERR);
	assert_zero(&plan, sizeof(plan));
}

static void test_plan_mutation(void)
{
	struct payload_mm_authvar_mor_clear_inventory inventory = valid_inventory();
	struct payload_mm_authvar_mor_clear_plan plan;

	mutate_object = &inventory;
	mutate_size = sizeof(inventory);
	CHECK(payload_mm_authvar_mor_clear_plan_build(&inventory, &plan) == CB_ERR);
	assert_zero(&plan, sizeof(plan));
}

static void test_plan_owned(void)
{
	struct payload_mm_authvar_mor_clear_inventory inventory = valid_inventory();
	struct payload_mm_authvar_mor_clear_plan original;
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_clear_plan_workspace workspace;

	memset(&plan, 0x5a, sizeof(plan));
	memset(&workspace, 0xa5, sizeof(workspace));
	CHECK(payload_mm_authvar_mor_clear_plan_build_owned(&inventory, &plan,
		&workspace) == CB_SUCCESS);
	CHECK(plan.span_count == 3);
	assert_zero(&workspace, sizeof(workspace));

	memset(&plan, 0x6b, sizeof(plan));
	original = plan;
	inventory.revision++;
	memset(&workspace, 0xa5, sizeof(workspace));
	CHECK(payload_mm_authvar_mor_clear_plan_build_owned(&inventory, &plan,
		&workspace) == CB_ERR);
	CHECK(!memcmp(&plan, &original, sizeof(plan)));
	assert_zero(&workspace, sizeof(workspace));

	inventory = valid_inventory();
	memset(&workspace, 0xa5, sizeof(workspace));
	mutate_object = &plan;
	mutate_size = sizeof(plan);
	CHECK(payload_mm_authvar_mor_clear_plan_build_owned(&inventory, &plan,
		&workspace) == CB_ERR);
	CHECK(!memcmp(&plan, &original, sizeof(plan)));
	assert_zero(&workspace, sizeof(workspace));
}

static void test_plan_owned_aliases(void)
{
	union {
		struct payload_mm_authvar_mor_clear_plan_workspace workspace;
		struct payload_mm_authvar_mor_clear_inventory inventory;
		struct payload_mm_authvar_mor_clear_plan plan;
		uint8_t bytes[sizeof(struct payload_mm_authvar_mor_clear_plan_workspace)];
	} shared;
	struct payload_mm_authvar_mor_clear_inventory inventory = valid_inventory();
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_clear_plan original;
	struct payload_mm_authvar_mor_clear_plan_workspace workspace;
	uint8_t before[sizeof(shared)];

	memset(&shared, 0xa5, sizeof(shared));
	memcpy(before, shared.bytes, sizeof(before));
	CHECK(payload_mm_authvar_mor_clear_plan_build_owned(&inventory,
		&shared.plan, &shared.workspace) == CB_ERR_ARG);
	CHECK(!memcmp(before, shared.bytes, sizeof(before)));

	memset(&shared, 0xa5, sizeof(shared));
	memcpy(before, shared.bytes, sizeof(before));
	CHECK(payload_mm_authvar_mor_clear_plan_build_owned(&shared.inventory,
		&plan, &shared.workspace) == CB_ERR_ARG);
	CHECK(!memcmp(before, shared.bytes, sizeof(before)));

	memset(&plan, 0x6b, sizeof(plan));
	original = plan;
	memset(&workspace, 0xa5, sizeof(workspace));
	CHECK(payload_mm_authvar_mor_clear_plan_build_owned(NULL, &plan,
		&workspace) == CB_ERR_ARG);
	CHECK(!memcmp(&plan, &original, sizeof(plan)));
	assert_zero(&workspace, sizeof(workspace));
	memset(&workspace, 0xa5, sizeof(workspace));
	CHECK(payload_mm_authvar_mor_clear_plan_build_owned(
		(const void *)(uintptr_t)(UINTPTR_MAX - 7), &plan,
		&workspace) == CB_ERR_ARG);
	CHECK(!memcmp(&plan, &original, sizeof(plan)));
	assert_zero(&workspace, sizeof(workspace));

	memset(&workspace, 0xa5, sizeof(workspace));
	CHECK(payload_mm_authvar_mor_clear_plan_build_owned(&inventory, &plan,
		(void *)((uint8_t *)&workspace + 1)) == CB_ERR_ARG);
	CHECK(!memcmp(&plan, &original, sizeof(plan)));
	CHECK(((uint8_t *)&workspace)[0] == 0xa5);
	CHECK(payload_mm_authvar_mor_clear_plan_build_owned(&inventory, &plan,
		(void *)(uintptr_t)(UINTPTR_MAX - 7)) == CB_ERR_ARG);
	CHECK(!memcmp(&plan, &original, sizeof(plan)));
}

static void test_receipt(void)
{
	struct payload_mm_authvar_mor_clear_plan plan = valid_plan();
	struct payload_mm_authvar_mor_entry entry = { .present = 1, .value = 0x11 };
	struct payload_mm_authvar_mor_clear_facts facts = valid_facts(&plan);
	struct payload_mm_authvar_mor_clear_transcript transcript =
		valid_transcript(&plan, &entry, &facts);
	struct payload_mm_authvar_mor_grant grant;

	CHECK(payload_mm_authvar_mor_clear_receipt_build(&plan, &entry, &facts,
		&transcript, &grant) == CB_SUCCESS);
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_SUCCESS);
	CHECK(grant.entry.value == 0x11);
	CHECK(grant.total_spans == 3 && grant.cleared_spans == 2 &&
		grant.excluded_spans == 1);
	CHECK(grant.total_bytes == 0x3800 && grant.cleared_bytes == 0x3000 &&
		grant.excluded_bytes == 0x800);
}

static void test_receipt_reject(void)
{
	struct payload_mm_authvar_mor_clear_plan plan = valid_plan();
	struct payload_mm_authvar_mor_entry entry = { .present = 1, .value = 0xff };
	struct payload_mm_authvar_mor_clear_facts facts = valid_facts(&plan);
	struct payload_mm_authvar_mor_clear_transcript transcript =
		valid_transcript(&plan, &entry, &facts);
	struct payload_mm_authvar_mor_grant grant;

#define REJECT_RECEIPT(expression) do { \
	memset(&grant, 0x5a, sizeof(grant)); \
	expression; \
	CHECK(payload_mm_authvar_mor_clear_receipt_build(&plan, &entry, &facts, \
		&transcript, &grant) != CB_SUCCESS); \
	assert_zero(&grant, sizeof(grant)); \
} while (0)
	REJECT_RECEIPT(transcript.records[0].written_bytes--);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(transcript.records[0].cache_writeback_fenced_bytes--);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(transcript.records[0].zero_readback_bytes--);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(transcript.records[0].base++);
	transcript = valid_transcript(&plan, &entry, &facts);
	{
		struct payload_mm_authvar_mor_clear_record temporary =
			transcript.records[0];
		transcript.records[0] = transcript.records[1];
		transcript.records[1] = temporary;
	}
	REJECT_RECEIPT((void)0);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(transcript.cleared_span_count--);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(transcript.records[2].base = 1);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(transcript.records[0].reserved[0] = 1);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(facts.dma_after.generation++);
	facts = valid_facts(&plan);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(memset(facts.dma_before.identity, 0,
		sizeof(facts.dma_before.identity));
		memset(facts.dma_after.identity, 0,
		sizeof(facts.dma_after.identity));
		memset(transcript.dma_before.identity, 0,
		sizeof(transcript.dma_before.identity));
		memset(transcript.dma_after.identity, 0,
		sizeof(transcript.dma_after.identity)));
	facts = valid_facts(&plan);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(facts.dma_before.reserved[0] = 1);
	facts = valid_facts(&plan);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(facts.reserved[0] = 1);
	facts = valid_facts(&plan);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(transcript.dma_after.identity[1] = 1);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(transcript.reserved1 = 1);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(transcript.cold_boot_generation++);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(transcript.inventory_identity[1] = 1);
	transcript = valid_transcript(&plan, &entry, &facts);
	REJECT_RECEIPT(entry.value = 0x10);
#undef REJECT_RECEIPT
}

static void test_alias_and_range(void)
{
	uint8_t invalid_plan[sizeof(struct payload_mm_authvar_mor_clear_plan) + 8]
		__aligned(8);
	uint8_t invalid_grant[sizeof(struct payload_mm_authvar_mor_grant) + 8]
		__aligned(8);
	union {
		struct payload_mm_authvar_mor_grant grant;
		struct payload_mm_authvar_mor_clear_plan plan;
		uint8_t storage[sizeof(struct payload_mm_authvar_mor_grant)];
	} shared;
	struct payload_mm_authvar_mor_clear_plan plan = valid_plan();
	struct payload_mm_authvar_mor_entry entry = { .present = 1, .value = 1 };
	struct payload_mm_authvar_mor_clear_facts facts = valid_facts(&plan);
	struct payload_mm_authvar_mor_clear_transcript transcript =
		valid_transcript(&plan, &entry, &facts);

	memset(invalid_plan, 0x5a, sizeof(invalid_plan));
	CHECK(payload_mm_authvar_mor_clear_plan_build(&((struct
		payload_mm_authvar_mor_clear_inventory){ 0 }),
		(void *)(invalid_plan + 1)) == CB_ERR_ARG);
	for (size_t index = 0; index < sizeof(invalid_plan); index++)
		CHECK(invalid_plan[index] == 0x5a);
	CHECK(payload_mm_authvar_mor_clear_plan_build(NULL, &shared.plan) ==
		CB_ERR_ARG);
	assert_zero(&shared.plan, sizeof(shared.plan));

	memset(invalid_grant, 0x5a, sizeof(invalid_grant));
	CHECK(payload_mm_authvar_mor_clear_receipt_build(&plan, &entry, &facts,
		&transcript, (void *)(invalid_grant + 1)) == CB_ERR_ARG);
	for (size_t index = 0; index < sizeof(invalid_grant); index++)
		CHECK(invalid_grant[index] == 0x5a);
	memset(&shared.grant, 0x5a, sizeof(shared.grant));
	CHECK(payload_mm_authvar_mor_clear_receipt_build(NULL, &entry, &facts,
		&transcript, &shared.grant) == CB_ERR_ARG);
	assert_zero(&shared.grant, sizeof(shared.grant));

	shared.plan = plan;
	CHECK(payload_mm_authvar_mor_clear_receipt_build(&shared.plan, &entry,
		&facts, &transcript, &shared.grant) == CB_ERR_ARG);
	assert_zero(&shared.grant, sizeof(shared.grant));
	memset(&shared, 0x5a, sizeof(shared));
	CHECK(payload_mm_authvar_mor_clear_plan_build(
		(const void *)(uintptr_t)(UINTPTR_MAX - 7), &shared.plan) == CB_ERR_ARG);
	assert_zero(&shared.plan, sizeof(shared.plan));
}

static void test_alias_pairs(void)
{
	uint64_t shared[(sizeof(struct payload_mm_authvar_mor_clear_transcript) + 7) / 8];
	struct payload_mm_authvar_mor_clear_plan plan = valid_plan();
	struct payload_mm_authvar_mor_entry entry = { .present = 1, .value = 1 };
	struct payload_mm_authvar_mor_clear_facts facts = valid_facts(&plan);
	struct payload_mm_authvar_mor_clear_transcript transcript =
		valid_transcript(&plan, &entry, &facts);
	struct payload_mm_authvar_mor_grant grant;

#define REJECT_ALIAS(plan_pointer, entry_pointer, facts_pointer, transcript_pointer) do { \
	memset(&grant, 0x5a, sizeof(grant)); \
	CHECK(payload_mm_authvar_mor_clear_receipt_build(plan_pointer, entry_pointer, \
		facts_pointer, transcript_pointer, &grant) == CB_ERR_ARG); \
	assert_zero(&grant, sizeof(grant)); \
} while (0)
	REJECT_ALIAS((const void *)shared, (const void *)shared, &facts, &transcript);
	REJECT_ALIAS((const void *)shared, &entry, (const void *)shared, &transcript);
	REJECT_ALIAS((const void *)shared, &entry, &facts, (const void *)shared);
	REJECT_ALIAS(&plan, (const void *)shared, (const void *)shared, &transcript);
	REJECT_ALIAS(&plan, (const void *)shared, &facts, (const void *)shared);
	REJECT_ALIAS(&plan, &entry, (const void *)shared, (const void *)shared);
#undef REJECT_ALIAS

#define REJECT_OUTPUT_ALIAS(plan_pointer, entry_pointer, facts_pointer, transcript_pointer) do { \
	memset(shared, 0x5a, sizeof(shared)); \
	CHECK(payload_mm_authvar_mor_clear_receipt_build(plan_pointer, entry_pointer, \
		facts_pointer, transcript_pointer, (void *)shared) == CB_ERR_ARG); \
	assert_zero(shared, sizeof(struct payload_mm_authvar_mor_grant)); \
} while (0)
	REJECT_OUTPUT_ALIAS((const void *)shared, &entry, &facts, &transcript);
	REJECT_OUTPUT_ALIAS(&plan, (const void *)shared, &facts, &transcript);
	REJECT_OUTPUT_ALIAS(&plan, &entry, (const void *)shared, &transcript);
	REJECT_OUTPUT_ALIAS(&plan, &entry, &facts, (const void *)shared);
#undef REJECT_OUTPUT_ALIAS

	memset(shared, 0x5a, sizeof(shared));
	CHECK(payload_mm_authvar_mor_clear_plan_build((const void *)shared,
		(void *)shared) == CB_ERR_ARG);
	assert_zero(shared, sizeof(struct payload_mm_authvar_mor_clear_plan));
}

static void test_receipt_mutation(void)
{
	struct payload_mm_authvar_mor_clear_plan plan = valid_plan();
	struct payload_mm_authvar_mor_entry entry = { .present = 1, .value = 1 };
	struct payload_mm_authvar_mor_clear_facts facts = valid_facts(&plan);
	struct payload_mm_authvar_mor_clear_transcript transcript =
		valid_transcript(&plan, &entry, &facts);
	struct payload_mm_authvar_mor_grant grant;

	mutate_object = &plan;
	mutate_size = sizeof(plan);
	CHECK(payload_mm_authvar_mor_clear_receipt_build(&plan, &entry, &facts,
		&transcript, &grant) == CB_ERR);
	assert_zero(&grant, sizeof(grant));

	plan = valid_plan();
	facts = valid_facts(&plan);
	transcript = valid_transcript(&plan, &entry, &facts);
	mutate_object = &entry;
	mutate_size = sizeof(entry);
	CHECK(payload_mm_authvar_mor_clear_receipt_build(&plan, &entry, &facts,
		&transcript, &grant) == CB_ERR);
	assert_zero(&grant, sizeof(grant));

	entry.value = 1;
	facts = valid_facts(&plan);
	transcript = valid_transcript(&plan, &entry, &facts);
	mutate_object = &facts;
	mutate_size = sizeof(facts);
	CHECK(payload_mm_authvar_mor_clear_receipt_build(&plan, &entry, &facts,
		&transcript, &grant) == CB_ERR);
	assert_zero(&grant, sizeof(grant));

	facts = valid_facts(&plan);
	transcript = valid_transcript(&plan, &entry, &facts);
	mutate_object = &transcript;
	mutate_size = sizeof(transcript);
	CHECK(payload_mm_authvar_mor_clear_receipt_build(&plan, &entry, &facts,
		&transcript, &grant) == CB_ERR);
	assert_zero(&grant, sizeof(grant));
}

static void test_dma_mismatch(void)
{
	struct payload_mm_authvar_mor_clear_plan plan = valid_plan();
	struct payload_mm_authvar_mor_entry entry = { .present = 1, .value = 1 };
	struct payload_mm_authvar_mor_clear_facts facts = valid_facts(&plan);
	struct payload_mm_authvar_mor_clear_transcript transcript =
		valid_transcript(&plan, &entry, &facts);
	struct payload_mm_authvar_mor_grant grant;

	facts.dma_after.generation++;
	transcript.dma_after = facts.dma_after;
	CHECK(payload_mm_authvar_mor_clear_receipt_build(&plan, &entry, &facts,
		&transcript, &grant) == CB_ERR);
}

static void test_written_count(void)
{
	struct payload_mm_authvar_mor_clear_plan plan = valid_plan();
	struct payload_mm_authvar_mor_entry entry = { .present = 1, .value = 1 };
	struct payload_mm_authvar_mor_clear_facts facts = valid_facts(&plan);
	struct payload_mm_authvar_mor_clear_transcript transcript =
		valid_transcript(&plan, &entry, &facts);
	struct payload_mm_authvar_mor_grant grant;

	transcript.records[0].written_bytes--;
	CHECK(payload_mm_authvar_mor_clear_receipt_build(&plan, &entry, &facts,
		&transcript, &grant) == CB_ERR);
}

int main(int argc, char **argv)
{
	CHECK(argc == 2);
	if (!strcmp(argv[1], "plan"))
		test_plan();
	else if (!strcmp(argv[1], "plan-reject"))
		test_plan_reject();
	else if (!strcmp(argv[1], "plan-mutation"))
		test_plan_mutation();
	else if (!strcmp(argv[1], "plan-owned"))
		test_plan_owned();
	else if (!strcmp(argv[1], "plan-owned-aliases"))
		test_plan_owned_aliases();
	else if (!strcmp(argv[1], "receipt"))
		test_receipt();
	else if (!strcmp(argv[1], "receipt-reject"))
		test_receipt_reject();
	else if (!strcmp(argv[1], "alias-range"))
		test_alias_and_range();
	else if (!strcmp(argv[1], "alias-pairs"))
		test_alias_pairs();
	else if (!strcmp(argv[1], "receipt-mutation"))
		test_receipt_mutation();
	else if (!strcmp(argv[1], "dma-mismatch"))
		test_dma_mismatch();
	else if (!strcmp(argv[1], "written-count"))
		test_written_count();
	else
		CHECK(0);
	return 0;
}
