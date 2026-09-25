/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_clear_executor.h>
#include <stdint.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)

#define BASE0 (0x100000000ULL + 0x1234ULL)
#define BASEX (BASE0 + 19)
#define BASE1 (BASEX + 5)

enum operation { OP_INVENTORY, OP_DMA, OP_MAP, OP_CACHE, OP_FENCE, OP_UNMAP };

struct mock {
	unsigned int calls[6];
	unsigned int sequence;
	unsigned int inventory_sequence[2];
	unsigned int dma_sequence[2];
	unsigned int first_map_sequence;
	unsigned int last_unmap_sequence;
	enum operation fail_operation;
	unsigned int fail_call;
	bool failed_map_nonnull;
	bool corrupt_readback;
	bool omit_dma[2];
	bool accept_any;
	void *alias_mapping;
	bool alias_stack_owner;
	bool program_sized_owner;
	bool tamper_mapping_tuple;
	void *mutate;
	size_t mutate_size;
	void *workspace;
	enum operation workspace_mutate_operation;
	unsigned int workspace_mutate_call;
	unsigned int inventory_mutate_call;
	void *inventory_mutate;
	uint64_t mapped_physical[32];
	size_t mapped_size[32];
	bool mapping_active;
	uint64_t active_physical;
	size_t active_size;
	void *active_mapping;
	uint64_t last_unmapped_physical;
	size_t last_unmapped_size;
	void *last_unmapped_mapping;
	struct payload_mm_authvar_mor_clear_dma_snapshot dma[2];
	struct payload_mm_authvar_mor_clear_plan expected_plan;
};

static uint8_t physical_first[19];
static uint8_t physical_excluded[5];
static uint8_t physical_second[11];

static void mutate(struct mock *mock, enum operation operation,
	unsigned int call)
{
	if (mock->mutate) {
		((uint8_t *)mock->mutate)[0] ^= 1;
		mock->mutate = NULL;
	}
	if (mock->workspace && mock->workspace_mutate_operation == operation &&
	    mock->workspace_mutate_call == call) {
		((uint8_t *)mock->workspace)[0] ^= 1;
		mock->workspace = NULL;
	}
}

static enum cb_err inventory_validate(void *context,
	const struct payload_mm_authvar_mor_clear_plan *plan)
{
	struct mock *mock = context;
	unsigned int call = ++mock->calls[OP_INVENTORY];

	CHECK(call <= 2);
	mock->inventory_sequence[call - 1U] = ++mock->sequence;
	if (memcmp(plan, &mock->expected_plan, sizeof(*plan)))
		return CB_ERR;
	if (mock->inventory_mutate_call == call && mock->inventory_mutate)
		((uint8_t *)mock->inventory_mutate)[0] ^= 1;
	if (mock->fail_operation == OP_INVENTORY && mock->fail_call == call)
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err dma_snapshot(void *context,
	struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot)
{
	struct mock *mock = context;
	unsigned int call = ++mock->calls[OP_DMA];

	mock->dma_sequence[call - 1U] = ++mock->sequence;
	mutate(mock, OP_DMA, call);
	if (mock->fail_operation == OP_DMA && mock->fail_call == call)
		return CB_ERR;
	if (mock->omit_dma[call - 1U])
		return CB_SUCCESS;
	*snapshot = mock->dma[call > 1];
	return CB_SUCCESS;
}

static enum cb_err map_window(void *context, uint64_t physical, size_t size,
	void **mapping)
{
	struct mock *mock = context;
	unsigned int call = ++mock->calls[OP_MAP];

	if (!mock->first_map_sequence)
		mock->first_map_sequence = ++mock->sequence;
	else
		mock->sequence++;
	CHECK(call <= 32);
	mock->mapped_physical[call - 1U] = physical;
	mock->mapped_size[call - 1U] = size;
	mutate(mock, OP_MAP, call);
	if (mock->fail_operation == OP_MAP && mock->fail_call == call) {
		*mapping = mock->failed_map_nonnull ? physical_first : NULL;
		return CB_ERR;
	}
	if (mock->alias_mapping) {
		*mapping = mock->alias_mapping;
	} else if (mock->accept_any && size <= sizeof(physical_first)) {
		*mapping = physical_first;
	} else if (physical >= BASE0 &&
		   physical <= BASE0 + sizeof(physical_first) &&
	    size <= BASE0 + sizeof(physical_first) - physical)
		*mapping = &physical_first[physical - BASE0];
	else if (physical >= BASE1 && physical <= BASE1 + sizeof(physical_second) &&
		 size <= BASE1 + sizeof(physical_second) - physical)
		*mapping = &physical_second[physical - BASE1];
	else
		return CB_ERR;
	CHECK(!mock->mapping_active);
	mock->mapping_active = true;
	mock->active_physical = physical;
	mock->active_size = size;
	mock->active_mapping = *mapping;
	return CB_SUCCESS;
}

static enum cb_err cache_writeback_invalidate(void *context, uint64_t physical,
	const volatile void *mapping, size_t size)
{
	struct mock *mock = context;
	unsigned int call = ++mock->calls[OP_CACHE];

	mock->sequence++;
	CHECK(physical == mock->active_physical && mapping == mock->active_mapping &&
		size == mock->active_size);
	mutate(mock, OP_CACHE, call);
	if (mock->tamper_mapping_tuple && call == 1)
		payload_mm_authvar_mor_clear_executor_mapping_tamper_test(
			mock->workspace, physical + 1U, (uint8_t *)mapping + 1,
			size + 1U);
	/* Six write windows precede the six readback invalidations. */
	if (mock->corrupt_readback && call == 7)
		physical_first[0] = 1;
	if (mock->fail_operation == OP_CACHE && mock->fail_call == call)
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err fence(void *context)
{
	struct mock *mock = context;
	unsigned int call = ++mock->calls[OP_FENCE];

	CHECK(mock->mapping_active);
	mock->sequence++;
	mutate(mock, OP_FENCE, call);
	if (mock->fail_operation == OP_FENCE && mock->fail_call == call)
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err unmap_window(void *context, uint64_t physical, void *mapping,
	size_t size)
{
	struct mock *mock = context;
	unsigned int call = ++mock->calls[OP_UNMAP];

	mock->last_unmap_sequence = ++mock->sequence;
	if (!mock->mapping_active) {
		CHECK(physical == mock->last_unmapped_physical &&
			mapping == mock->last_unmapped_mapping &&
			size == mock->last_unmapped_size);
		return CB_SUCCESS;
	}
	CHECK(physical == mock->active_physical && mapping == mock->active_mapping &&
		size == mock->active_size);
	mutate(mock, OP_UNMAP, call);
	if (mock->fail_operation == OP_UNMAP && mock->fail_call == call)
		return CB_ERR;
	mock->last_unmapped_physical = physical;
	mock->last_unmapped_mapping = mapping;
	mock->last_unmapped_size = size;
	mock->mapping_active = false;
	return CB_SUCCESS;
}

static struct payload_mm_authvar_mor_clear_plan valid_plan(void)
{
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };

	plan.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	plan.size = sizeof(plan);
	plan.inventory_generation = 3;
	plan.inventory_identity[0] = 0x33;
	plan.span_count = 3;
	plan.spans[0] = (struct payload_mm_authvar_mor_grant_span) {
		.base = BASE0, .size = 19,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
	};
	plan.spans[1] = (struct payload_mm_authvar_mor_grant_span) {
		.base = BASEX, .size = 5,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	plan.spans[2] = (struct payload_mm_authvar_mor_grant_span) {
		.base = BASE1, .size = 11,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
	};
	return plan;
}

static void initialize(struct mock *mock,
	struct payload_mm_authvar_mor_clear_executor_ops *ops)
{
	memset(mock, 0, sizeof(*mock));
	memset(physical_first, 0xa5, sizeof(physical_first));
	memset(physical_excluded, 0x5a, sizeof(physical_excluded));
	memset(physical_second, 0xa5, sizeof(physical_second));
	mock->dma[0].generation = 7;
	mock->dma[0].identity[0] = 0x77;
	mock->dma[1] = mock->dma[0];
	*ops = (struct payload_mm_authvar_mor_clear_executor_ops) {
		.context = mock,
		.window_bytes = 7,
		.inventory_context = mock,
		.inventory_validate = inventory_validate,
		.dma_snapshot = dma_snapshot,
		.map_window = map_window,
		.cache_writeback_invalidate = cache_writeback_invalidate,
		.fence = fence,
		.unmap_window = unmap_window,
	};
}

static void assert_zero(const void *object, size_t size)
{
	const uint8_t *bytes = object;

	for (size_t index = 0; index < size; index++)
		CHECK(bytes[index] == 0);
}

static enum cb_err execute(struct mock *mock,
	struct payload_mm_authvar_mor_clear_plan *plan,
	struct payload_mm_authvar_mor_entry *entry,
	struct payload_mm_authvar_mor_clear_executor_ops *ops,
	struct payload_mm_authvar_mor_clear_transcript *transcript,
	struct payload_mm_authvar_mor_grant *grant)
{
	struct payload_mm_authvar_mor_clear_workspace workspace = { 0 };
	uint8_t stack_only = 0xa5;
	const uintptr_t callbacks[] = { (uintptr_t)inventory_validate,
		(uintptr_t)dma_snapshot, (uintptr_t)map_window,
		(uintptr_t)cache_writeback_invalidate, (uintptr_t)fence,
		(uintptr_t)unmap_window };
	const void *objects[] = { &workspace, plan, entry, ops, transcript, grant,
		mock, &stack_only };
	const size_t sizes[] = { sizeof(workspace), sizeof(*plan), sizeof(*entry),
		sizeof(*ops), sizeof(*transcript), sizeof(*grant), sizeof(*mock),
		sizeof(stack_only) };
	uintptr_t code_base = callbacks[0];
	uintptr_t code_end = callbacks[0] + 1U;
	uintptr_t stack_base = (uintptr_t)objects[0];
	uintptr_t stack_end = stack_base + sizes[0];

	for (size_t index = 1; index < ARRAY_SIZE(callbacks); index++) {
		if (callbacks[index] < code_base)
			code_base = callbacks[index];
		if (callbacks[index] + 1U > code_end)
			code_end = callbacks[index] + 1U;
	}
	for (size_t index = 1; index < ARRAY_SIZE(objects); index++) {
		const uintptr_t base = (uintptr_t)objects[index];
		if (base < stack_base)
			stack_base = base;
		if (base + sizes[index] > stack_end)
			stack_end = base + sizes[index];
	}
	plan->spans[plan->span_count++] = (struct payload_mm_authvar_mor_grant_span) {
		.base = code_base, .size = code_end - code_base,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
		.exclusion_reason = PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	plan->spans[plan->span_count++] = (struct payload_mm_authvar_mor_grant_span) {
		.base = stack_base, .size = stack_end - stack_base,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
		.exclusion_reason = PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	for (size_t index = 1; index < plan->span_count; index++) {
		struct payload_mm_authvar_mor_grant_span span = plan->spans[index];
		size_t insert = index;

		while (insert && plan->spans[insert - 1U].base > span.base) {
			plan->spans[insert] = plan->spans[insert - 1U];
			insert--;
		}
		plan->spans[insert] = span;
	}
	ops->context_size = sizeof(*mock);
	ops->inventory_context_size = sizeof(*mock);
	ops->executable_owner = (void *)code_base;
	ops->executable_owner_size = code_end - code_base;
	if (mock->program_sized_owner) {
		if (stack_base < code_base)
			code_base = stack_base;
		if (stack_end > code_end)
			code_end = stack_end;
		ops->executable_owner = (void *)code_base;
		ops->executable_owner_size = code_end - code_base;
	}
	ops->stack_owner = (void *)stack_base;
	ops->stack_owner_size = stack_end - stack_base;
	if (mock->alias_stack_owner)
		mock->alias_mapping = &stack_only;
	mock->workspace = &workspace;
	memset(transcript, 0xa5, sizeof(*transcript));
	memset(grant, 0xa5, sizeof(*grant));
	mock->expected_plan = *plan;
	return payload_mm_authvar_mor_clear_execute(&workspace, plan, entry, 11, ops,
		transcript, grant);
}

static void test_success(void)
{
	struct mock mock;
	struct payload_mm_authvar_mor_clear_executor_ops ops;
	struct payload_mm_authvar_mor_clear_plan plan = valid_plan();
	struct payload_mm_authvar_mor_entry entry = { .present = 1, .value = 0x81 };
	struct payload_mm_authvar_mor_clear_transcript transcript;
	struct payload_mm_authvar_mor_grant grant;

	initialize(&mock, &ops);
	CHECK(execute(&mock, &plan, &entry, &ops, &transcript, &grant) == CB_SUCCESS);
	assert_zero(physical_first, sizeof(physical_first));
	assert_zero(physical_second, sizeof(physical_second));
	for (size_t index = 0; index < sizeof(physical_excluded); index++)
		CHECK(physical_excluded[index] == 0x5a);
	CHECK(mock.calls[OP_DMA] == 2 && mock.calls[OP_MAP] == 12);
	CHECK(mock.calls[OP_INVENTORY] == 2);
	CHECK(mock.calls[OP_CACHE] == 12 && mock.calls[OP_FENCE] == 12);
	CHECK(mock.calls[OP_UNMAP] == 12);
	CHECK(mock.inventory_sequence[0] < mock.dma_sequence[0]);
	CHECK(mock.dma_sequence[0] < mock.first_map_sequence);
	CHECK(mock.last_unmap_sequence < mock.dma_sequence[1]);
	CHECK(mock.dma_sequence[1] < mock.inventory_sequence[1]);
	CHECK(transcript.cleared_span_count == 2);
	CHECK(transcript.records[0].written_bytes == 19);
	CHECK(transcript.records[0].zero_readback_bytes == 19);
	CHECK(transcript.records[1].written_bytes == 11);
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_SUCCESS);
}

static void test_failures(void)
{
	for (enum operation operation = OP_INVENTORY;
	     operation <= OP_UNMAP; operation++) {
		const unsigned int maximum = operation == OP_INVENTORY ||
			operation == OP_DMA ? 2 : 12;
		for (unsigned int call = 1; call <= maximum; call++) {
			struct mock mock;
			struct payload_mm_authvar_mor_clear_executor_ops ops;
			struct payload_mm_authvar_mor_clear_plan plan = valid_plan();
			struct payload_mm_authvar_mor_entry entry = { 1, 1, 0 };
			struct payload_mm_authvar_mor_clear_transcript transcript;
			struct payload_mm_authvar_mor_grant grant;

			initialize(&mock, &ops);
			mock.fail_operation = operation;
			mock.fail_call = call;
			CHECK(execute(&mock, &plan, &entry, &ops, &transcript,
				&grant) != CB_SUCCESS);
			CHECK(!mock.mapping_active);
			assert_zero(&transcript, sizeof(transcript));
			assert_zero(&grant, sizeof(grant));
			if (operation == OP_INVENTORY && call == 1)
				CHECK(mock.calls[OP_DMA] == 0 && mock.calls[OP_MAP] == 0);
			if (operation == OP_INVENTORY && call == 2)
				CHECK(mock.calls[OP_DMA] == 2 && mock.calls[OP_MAP] == 12);
			if (operation == OP_CACHE)
				CHECK(mock.calls[OP_FENCE] >= call);
			if (operation != OP_MAP)
				CHECK(mock.calls[OP_UNMAP] >=
					(mock.calls[OP_MAP] ? mock.calls[OP_MAP] - 1 : 0));
		}
	}
}

static void test_hostile(void)
{
	struct mock mock;
	struct payload_mm_authvar_mor_clear_executor_ops ops;
	struct payload_mm_authvar_mor_clear_plan plan = valid_plan();
	struct payload_mm_authvar_mor_entry entry = { 1, 1, 0 };
	struct payload_mm_authvar_mor_clear_transcript transcript;
	struct payload_mm_authvar_mor_grant grant;

#define FAIL_HOSTILE(statement) do { \
	plan = valid_plan(); entry = (struct payload_mm_authvar_mor_entry) { 1, 1, 0 }; \
	initialize(&mock, &ops); statement; \
	CHECK(execute(&mock, &plan, &entry, &ops, &transcript, &grant) != CB_SUCCESS); \
	CHECK(!mock.mapping_active); \
	assert_zero(&transcript, sizeof(transcript)); assert_zero(&grant, sizeof(grant)); \
} while (0)
	FAIL_HOSTILE(mock.corrupt_readback = true);
	FAIL_HOSTILE(mock.dma[1].generation++);
	FAIL_HOSTILE(mock.dma[1].identity[1] = 1);
	FAIL_HOSTILE(mock.dma[0].reserved[0] = 1);
	FAIL_HOSTILE(mock.omit_dma[0] = true);
	FAIL_HOSTILE(mock.omit_dma[1] = true);
	FAIL_HOSTILE(mock.alias_mapping = &plan);
	FAIL_HOSTILE(mock.alias_mapping = &mock);
	FAIL_HOSTILE(mock.alias_mapping = (void *)inventory_validate);
	FAIL_HOSTILE(mock.alias_stack_owner = true);
	FAIL_HOSTILE(mock.workspace_mutate_operation = OP_MAP;
		mock.workspace_mutate_call = 1);
	CHECK(mock.calls[OP_MAP] == 1 && mock.calls[OP_UNMAP] == 1);
	FAIL_HOSTILE(mock.workspace_mutate_operation = OP_CACHE;
		mock.workspace_mutate_call = 1);
	CHECK(mock.calls[OP_MAP] == 1 && mock.calls[OP_UNMAP] == 1);
	FAIL_HOSTILE(mock.tamper_mapping_tuple = true);
	CHECK(mock.calls[OP_MAP] == 1 && mock.calls[OP_UNMAP] == 1 &&
		mock.last_unmapped_physical == mock.mapped_physical[0] &&
		mock.last_unmapped_mapping == physical_first &&
		mock.last_unmapped_size == mock.mapped_size[0]);
	FAIL_HOSTILE(mock.workspace_mutate_operation = OP_FENCE;
		mock.workspace_mutate_call = 1);
	CHECK(mock.calls[OP_MAP] == 1 && mock.calls[OP_UNMAP] == 1);
	FAIL_HOSTILE(mock.workspace_mutate_operation = OP_UNMAP;
		mock.workspace_mutate_call = 1);
	CHECK(mock.calls[OP_MAP] == 1 && mock.calls[OP_UNMAP] == 1);
	FAIL_HOSTILE(mock.fail_operation = OP_MAP; mock.fail_call = 1;
		mock.failed_map_nonnull = true);
	FAIL_HOSTILE(mock.mutate = &plan; mock.mutate_size = sizeof(plan));
	plan = valid_plan();
	FAIL_HOSTILE(mock.mutate = &entry; mock.mutate_size = sizeof(entry));
	entry = (struct payload_mm_authvar_mor_entry) { 1, 1, 0 };
	FAIL_HOSTILE(mock.mutate = &ops; mock.mutate_size = sizeof(ops));
	FAIL_HOSTILE(mock.mutate = &transcript; mock.mutate_size = sizeof(transcript));
	FAIL_HOSTILE(mock.mutate = &grant; mock.mutate_size = sizeof(grant));
	FAIL_HOSTILE(mock.inventory_mutate_call = 1;
		mock.inventory_mutate = &ops);
	FAIL_HOSTILE(mock.inventory_mutate_call = 1;
		mock.inventory_mutate = &ops.inventory_context);
	FAIL_HOSTILE(mock.inventory_mutate_call = 2;
		mock.inventory_mutate = &ops.inventory_validate);
	FAIL_HOSTILE(mock.inventory_mutate_call = 2;
		mock.inventory_mutate = &plan);
	plan = valid_plan();
	FAIL_HOSTILE(ops.window_bytes = 0);
	FAIL_HOSTILE(ops.inventory_validate = NULL);
	FAIL_HOSTILE(entry.present = 0);
	FAIL_HOSTILE(entry.value = 2);
	plan = valid_plan();
	entry = (struct payload_mm_authvar_mor_entry) { 1, 1, 0 };
	initialize(&mock, &ops);
	plan.revision++;
	CHECK(execute(&mock, &plan, &entry, &ops, &transcript, &grant) != CB_SUCCESS);
	CHECK(mock.calls[OP_DMA] == 0 && mock.calls[OP_MAP] == 0);
#undef FAIL_HOSTILE
}

static void test_object_boundaries(void)
{
	struct mock mock;
	struct payload_mm_authvar_mor_clear_executor_ops ops;
	struct payload_mm_authvar_mor_clear_plan plan = valid_plan();
	struct payload_mm_authvar_mor_entry entry = { 1, 1, 0 };
	struct payload_mm_authvar_mor_clear_transcript transcript;
	struct payload_mm_authvar_mor_grant grant;
	struct payload_mm_authvar_mor_clear_workspace workspace = { 0 };
	union {
		uint64_t alignment;
		uint8_t bytes[8192];
	} backing;

	for (size_t left = 0; left < 6; left++) {
		for (size_t right = left + 1; right < 6; right++) {
			void *objects[] = { &workspace, &plan, &entry, &ops, &transcript,
				&grant };

			initialize(&mock, &ops);
			memset(&workspace, 0, sizeof(workspace));
			memset(&backing, 0, sizeof(backing));
			objects[left] = backing.bytes;
			objects[right] = backing.bytes;
			CHECK(payload_mm_authvar_mor_clear_execute(objects[0], objects[1],
				objects[2], 11, objects[3], objects[4], objects[5]) == CB_ERR_ARG);
		}
	}

	initialize(&mock, &ops);
	memset(&workspace, 0, sizeof(workspace));
	memset(&transcript, 0xa5, sizeof(transcript));
	memset(&grant, 0xa5, sizeof(grant));
	memset(&workspace, 0, sizeof(workspace));
	CHECK(payload_mm_authvar_mor_clear_execute(&workspace, NULL, &entry, 11, &ops,
		&transcript, &grant) == CB_ERR_ARG);
	CHECK(((uint8_t *)&transcript)[0] == 0xa5 && ((uint8_t *)&grant)[0] == 0xa5);
	memset(&grant, 0xa5, sizeof(grant));
	CHECK(payload_mm_authvar_mor_clear_execute(&workspace, &plan, &entry, 11, &ops,
		(void *)((uintptr_t)-8), &grant) == CB_ERR_ARG);
	CHECK(((uint8_t *)&grant)[0] == 0xa5);
	memset(&transcript, 0xa5, sizeof(transcript));
	memset(&workspace, 0, sizeof(workspace));
	CHECK(payload_mm_authvar_mor_clear_execute(&workspace, &plan, &entry, 11, &ops,
		&transcript, (void *)((uintptr_t)-8)) == CB_ERR_ARG);
	CHECK(((uint8_t *)&transcript)[0] == 0xa5);

	initialize(&mock, &ops);
	memset(&workspace, 0, sizeof(workspace));
	memset(&transcript, 0xa5, sizeof(transcript));
	memset(&grant, 0xa5, sizeof(grant));
	ops.context = &workspace;
	ops.context_size = sizeof(workspace);
	CHECK(payload_mm_authvar_mor_clear_execute(&workspace, &plan, &entry, 11,
		&ops, &transcript, &grant) == CB_ERR_ARG);
	assert_zero(&workspace, sizeof(workspace));
	CHECK(((uint8_t *)&transcript)[0] == 0xa5 &&
		((uint8_t *)&grant)[0] == 0xa5);

	/* A whole-program owner also contains static/public state and is invalid. */
	initialize(&mock, &ops);
	mock.program_sized_owner = true;
	CHECK(execute(&mock, &plan, &entry, &ops, &transcript, &grant) ==
		CB_ERR_ARG);
	CHECK(!mock.calls[OP_INVENTORY] && !mock.calls[OP_DMA] &&
		!mock.calls[OP_MAP]);
	CHECK(((uint8_t *)&transcript)[0] == 0xa5 &&
		((uint8_t *)&grant)[0] == 0xa5);

	initialize(&mock, &ops);
	ops.inventory_context = &transcript;
	ops.inventory_context_size = sizeof(transcript);
	CHECK(payload_mm_authvar_mor_clear_execute(&workspace, &plan, &entry, 11,
		&ops, &transcript, &grant) == CB_ERR_ARG);
	assert_zero(&workspace, sizeof(workspace));
	CHECK(((uint8_t *)&transcript)[0] == 0xa5 &&
		((uint8_t *)&grant)[0] == 0xa5);

	initialize(&mock, &ops);
	ops.executable_owner = &grant;
	ops.executable_owner_size = sizeof(grant);
	CHECK(payload_mm_authvar_mor_clear_execute(&workspace, &plan, &entry, 11,
		&ops, &transcript, &grant) == CB_ERR_ARG);
	assert_zero(&workspace, sizeof(workspace));
	CHECK(((uint8_t *)&transcript)[0] == 0xa5 &&
		((uint8_t *)&grant)[0] == 0xa5);

	initialize(&mock, &ops);
	memset(&plan, 0, sizeof(plan));
	plan.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	plan.size = sizeof(plan);
	plan.inventory_generation = 3;
	plan.inventory_identity[0] = 1;
	plan.span_count = 1;
	plan.spans[0] = (struct payload_mm_authvar_mor_grant_span) {
		.base = UINT64_MAX - 10, .size = 10,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
	};
	ops.window_bytes = 64;
	mock.accept_any = true;
	CHECK(execute(&mock, &plan, &entry, &ops, &transcript, &grant) == CB_SUCCESS);
	CHECK(mock.calls[OP_MAP] == 2 && transcript.records[0].written_bytes == 10);
}

struct expected_window {
	uint64_t physical;
	size_t size;
};

static void check_physical_windows(uint64_t base, uint64_t size,
	size_t window_bytes, const struct expected_window *expected,
	size_t expected_count)
{
	struct mock mock;
	struct payload_mm_authvar_mor_clear_executor_ops ops;
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct payload_mm_authvar_mor_entry entry = { 1, 1, 0 };
	struct payload_mm_authvar_mor_clear_transcript transcript;
	struct payload_mm_authvar_mor_grant grant;

	plan.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	plan.size = sizeof(plan);
	plan.inventory_generation = 3;
	plan.inventory_identity[0] = 1;
	plan.span_count = 1;
	plan.spans[0] = (struct payload_mm_authvar_mor_grant_span) {
		.base = base,
		.size = size,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
	};
	initialize(&mock, &ops);
	ops.window_bytes = window_bytes;
	mock.accept_any = true;
	CHECK(execute(&mock, &plan, &entry, &ops, &transcript, &grant) ==
		CB_SUCCESS);
	CHECK(mock.calls[OP_MAP] == expected_count * 2U);
	for (size_t pass = 0; pass < 2; pass++) {
		for (size_t index = 0; index < expected_count; index++) {
			const size_t call = pass * expected_count + index;

			CHECK(mock.mapped_physical[call] == expected[index].physical);
			CHECK(mock.mapped_size[call] == expected[index].size);
			CHECK(mock.mapped_size[call] <= window_bytes);
			CHECK(mock.mapped_physical[call] / window_bytes ==
				(mock.mapped_physical[call] + mock.mapped_size[call] - 1U) /
				window_bytes);
		}
	}
}

static void test_physical_window_boundaries(void)
{
	static const struct expected_window physical_zero[] = {
		{ 0, 8 }, { 8, 8 }, { 16, 1 },
	};
	static const struct expected_window unaligned[] = {
		{ 5, 3 }, { 8, 8 }, { 16, 8 }, { 24, 1 },
	};
	static const struct expected_window four_gib[] = {
		{ 0xffffffffULL, 1 }, { 0x100000000ULL, 2 },
		{ 0x100000002ULL, 1 },
	};
	static const struct expected_window uint64_end[] = {
		{ UINT64_MAX - 14U, 7 }, { UINT64_MAX - 7U, 7 },
	};
	struct mock mock;
	struct payload_mm_authvar_mor_clear_executor_ops ops;
	struct payload_mm_authvar_mor_clear_plan plan = valid_plan();
	struct payload_mm_authvar_mor_entry entry = { 1, 1, 0 };
	struct payload_mm_authvar_mor_clear_transcript transcript;
	struct payload_mm_authvar_mor_grant grant;

	check_physical_windows(0, 17, 8, physical_zero,
		ARRAY_SIZE(physical_zero));
	check_physical_windows(5, 20, 8, unaligned, ARRAY_SIZE(unaligned));
	check_physical_windows(0xffffffffULL, 4, 2, four_gib,
		ARRAY_SIZE(four_gib));
	check_physical_windows(UINT64_MAX - 14U, 14, 8, uint64_end,
		ARRAY_SIZE(uint64_end));

	initialize(&mock, &ops);
	plan.span_count = 1;
	plan.spans[0] = (struct payload_mm_authvar_mor_grant_span) {
		.base = UINT64_MAX - 3U,
		.size = 4,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
	};
	CHECK(execute(&mock, &plan, &entry, &ops, &transcript, &grant) !=
		CB_SUCCESS);
	CHECK(mock.calls[OP_DMA] == 0 && mock.calls[OP_MAP] == 0);
	assert_zero(&transcript, sizeof(transcript));
	assert_zero(&grant, sizeof(grant));
}

int main(int argc, char **argv)
{
	CHECK(argc == 2);
	if (!strcmp(argv[1], "success"))
		test_success();
	else if (!strcmp(argv[1], "failures"))
		test_failures();
	else if (!strcmp(argv[1], "hostile"))
		test_hostile();
	else if (!strcmp(argv[1], "objects"))
		test_object_boundaries();
	else if (!strcmp(argv[1], "windows"))
		test_physical_window_boundaries();
	else
		CHECK(false);
	return 0;
}
