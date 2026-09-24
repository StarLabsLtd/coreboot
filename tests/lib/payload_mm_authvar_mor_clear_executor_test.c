/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_clear_executor.h>
#include <stdint.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)

#define BASE0 (0x100000000ULL + 0x1234ULL)
#define BASEX (BASE0 + 19)
#define BASE1 (BASEX + 5)

enum operation { OP_DMA, OP_MAP, OP_CACHE, OP_FENCE, OP_UNMAP };

struct mock {
	uint8_t first[19];
	uint8_t excluded[5];
	uint8_t second[11];
	unsigned int calls[5];
	enum operation fail_operation;
	unsigned int fail_call;
	bool failed_map_nonnull;
	bool corrupt_readback;
	bool accept_any;
	void *alias_mapping;
	void *mutate;
	size_t mutate_size;
	struct payload_mm_authvar_mor_clear_dma_snapshot dma[2];
};

static void mutate(struct mock *mock)
{
	if (mock->mutate) {
		((uint8_t *)mock->mutate)[0] ^= 1;
		mock->mutate = NULL;
	}
}

static enum cb_err dma_snapshot(void *context,
	struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot)
{
	struct mock *mock = context;
	unsigned int call = ++mock->calls[OP_DMA];

	mutate(mock);
	if (mock->fail_operation == OP_DMA && mock->fail_call == call)
		return CB_ERR;
	*snapshot = mock->dma[call > 1];
	return CB_SUCCESS;
}

static enum cb_err map_window(void *context, uint64_t physical, size_t size,
	void **mapping)
{
	struct mock *mock = context;
	unsigned int call = ++mock->calls[OP_MAP];

	mutate(mock);
	if (mock->fail_operation == OP_MAP && mock->fail_call == call) {
		*mapping = mock->failed_map_nonnull ? mock->first : NULL;
		return CB_ERR;
	}
	if (mock->alias_mapping) {
		*mapping = mock->alias_mapping;
		return CB_SUCCESS;
	}
	if (mock->accept_any && size <= sizeof(mock->first)) {
		*mapping = mock->first;
		return CB_SUCCESS;
	}
	if (physical >= BASE0 && physical <= BASE0 + sizeof(mock->first) &&
	    size <= BASE0 + sizeof(mock->first) - physical)
		*mapping = &mock->first[physical - BASE0];
	else if (physical >= BASE1 && physical <= BASE1 + sizeof(mock->second) &&
		 size <= BASE1 + sizeof(mock->second) - physical)
		*mapping = &mock->second[physical - BASE1];
	else
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err cache_writeback_invalidate(void *context, uint64_t physical,
	const volatile void *mapping, size_t size)
{
	struct mock *mock = context;
	unsigned int call = ++mock->calls[OP_CACHE];

	(void)physical;
	(void)mapping;
	(void)size;
	mutate(mock);
	/* Five write windows precede the five readback invalidations. */
	if (mock->corrupt_readback && call == 6)
		mock->first[0] = 1;
	if (mock->fail_operation == OP_CACHE && mock->fail_call == call)
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err fence(void *context)
{
	struct mock *mock = context;
	unsigned int call = ++mock->calls[OP_FENCE];

	mutate(mock);
	if (mock->fail_operation == OP_FENCE && mock->fail_call == call)
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err unmap_window(void *context, uint64_t physical, void *mapping,
	size_t size)
{
	struct mock *mock = context;
	unsigned int call = ++mock->calls[OP_UNMAP];

	(void)physical;
	(void)mapping;
	(void)size;
	mutate(mock);
	if (mock->fail_operation == OP_UNMAP && mock->fail_call == call)
		return CB_ERR;
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
	memset(mock->first, 0xa5, sizeof(mock->first));
	memset(mock->excluded, 0x5a, sizeof(mock->excluded));
	memset(mock->second, 0xa5, sizeof(mock->second));
	mock->dma[0].generation = 7;
	mock->dma[0].identity[0] = 0x77;
	mock->dma[1] = mock->dma[0];
	*ops = (struct payload_mm_authvar_mor_clear_executor_ops) {
		.context = mock,
		.window_bytes = 7,
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
	memset(transcript, 0xa5, sizeof(*transcript));
	memset(grant, 0xa5, sizeof(*grant));
	(void)mock;
	return payload_mm_authvar_mor_clear_execute(plan, entry, 11, ops,
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
	assert_zero(mock.first, sizeof(mock.first));
	assert_zero(mock.second, sizeof(mock.second));
	for (size_t index = 0; index < sizeof(mock.excluded); index++)
		CHECK(mock.excluded[index] == 0x5a);
	CHECK(mock.calls[OP_DMA] == 2 && mock.calls[OP_MAP] == 10);
	CHECK(mock.calls[OP_CACHE] == 10 && mock.calls[OP_FENCE] == 10);
	CHECK(mock.calls[OP_UNMAP] == 10);
	CHECK(transcript.cleared_span_count == 2);
	CHECK(transcript.records[0].written_bytes == 19);
	CHECK(transcript.records[0].zero_readback_bytes == 19);
	CHECK(transcript.records[1].written_bytes == 11);
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_SUCCESS);
}

static void test_failures(void)
{
	for (enum operation operation = OP_DMA; operation <= OP_UNMAP; operation++) {
		const unsigned int maximum = operation == OP_DMA ? 2 : 10;
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
			assert_zero(&transcript, sizeof(transcript));
			assert_zero(&grant, sizeof(grant));
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
	initialize(&mock, &ops); statement; \
	CHECK(execute(&mock, &plan, &entry, &ops, &transcript, &grant) != CB_SUCCESS); \
	assert_zero(&transcript, sizeof(transcript)); assert_zero(&grant, sizeof(grant)); \
} while (0)
	FAIL_HOSTILE(mock.corrupt_readback = true);
	FAIL_HOSTILE(mock.dma[1].generation++);
	FAIL_HOSTILE(mock.dma[1].identity[1] = 1);
	FAIL_HOSTILE(mock.dma[0].reserved[0] = 1);
	FAIL_HOSTILE(mock.alias_mapping = &plan);
	FAIL_HOSTILE(mock.fail_operation = OP_MAP; mock.fail_call = 1;
		mock.failed_map_nonnull = true);
	FAIL_HOSTILE(mock.mutate = &plan; mock.mutate_size = sizeof(plan));
	plan = valid_plan();
	FAIL_HOSTILE(mock.mutate = &entry; mock.mutate_size = sizeof(entry));
	entry = (struct payload_mm_authvar_mor_entry) { 1, 1, 0 };
	FAIL_HOSTILE(mock.mutate = &ops; mock.mutate_size = sizeof(ops));
	FAIL_HOSTILE(mock.mutate = &transcript; mock.mutate_size = sizeof(transcript));
	FAIL_HOSTILE(mock.mutate = &grant; mock.mutate_size = sizeof(grant));
	FAIL_HOSTILE(ops.window_bytes = 0);
	FAIL_HOSTILE(entry.present = 0);
	entry = (struct payload_mm_authvar_mor_entry) { 1, 2, 0 };
	FAIL_HOSTILE((void)0);
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
	union {
		uint64_t alignment;
		uint8_t bytes[2048];
	} backing;

	for (size_t left = 0; left < 5; left++) {
		for (size_t right = left + 1; right < 5; right++) {
			void *objects[] = { &plan, &entry, &ops, &transcript, &grant };

			initialize(&mock, &ops);
			memset(&backing, 0, sizeof(backing));
			objects[left] = backing.bytes;
			objects[right] = backing.bytes;
			CHECK(payload_mm_authvar_mor_clear_execute(objects[0], objects[1],
				11, objects[2], objects[3], objects[4]) == CB_ERR_ARG);
		}
	}

	initialize(&mock, &ops);
	memset(&transcript, 0xa5, sizeof(transcript));
	memset(&grant, 0xa5, sizeof(grant));
	CHECK(payload_mm_authvar_mor_clear_execute(NULL, &entry, 11, &ops,
		&transcript, &grant) == CB_ERR_ARG);
	assert_zero(&transcript, sizeof(transcript));
	assert_zero(&grant, sizeof(grant));
	memset(&grant, 0xa5, sizeof(grant));
	CHECK(payload_mm_authvar_mor_clear_execute(&plan, &entry, 11, &ops,
		(void *)((uintptr_t)-8), &grant) == CB_ERR_ARG);
	assert_zero(&grant, sizeof(grant));
	memset(&transcript, 0xa5, sizeof(transcript));
	CHECK(payload_mm_authvar_mor_clear_execute(&plan, &entry, 11, &ops,
		&transcript, (void *)((uintptr_t)-8)) == CB_ERR_ARG);
	assert_zero(&transcript, sizeof(transcript));

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
	else
		CHECK(false);
	return 0;
}
