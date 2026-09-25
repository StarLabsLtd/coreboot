/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_linear.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)

enum failure_point {
	FAIL_NONE,
	FAIL_CLASSIFY,
	FAIL_PROBE,
	FAIL_RESERVE,
	FAIL_RESOLVE,
	FAIL_EXECUTE,
	FAIL_COMPLETE,
	FAIL_CLOSE,
	TAMPER_CLASSIFY,
	TAMPER_RESERVE,
	TAMPER_COMPLETE,
	REENTER_CLASSIFY,
	REENTER_RESERVE,
	REENTER_RESOLVE,
	REENTER_COMPLETE,
	REENTER_CLOSE,
};

struct race_sync {
	atomic_int callback_entered;
	atomic_int competitor_done;
	bool hold_classify;
	bool hold_resolve;
	bool hold_close;
};

struct test_context {
	struct payload_mm_authvar_mor_linear_state *state;
	struct payload_mm_authvar_mor_linear_state *nested_state;
	const struct payload_mm_authvar_mor_linear_ops *ops;
	struct race_sync *race;
	enum payload_mm_authvar_mor_linear_boot_kind kind;
	struct payload_mm_authvar_mor_entry entry;
	enum failure_point failure;
	unsigned int classify_calls;
	unsigned int probe_calls;
	unsigned int reserve_calls;
	unsigned int resolve_calls;
	unsigned int execute_calls;
	unsigned int complete_calls;
	unsigned int close_calls;
};

static struct test_context *active;
static struct payload_mm_authvar_mor_clear_workspace clear_workspace;
static struct payload_mm_authvar_mor_linear_state callback_snapshot;

static enum payload_mm_authvar_mor_linear_result test_before(
	struct payload_mm_authvar_mor_linear_state *state,
	const struct payload_mm_authvar_mor_linear_ops *ops)
{
	return payload_mm_authvar_mor_linear_before_bootmem(state, ops,
		&callback_snapshot);
}

static enum payload_mm_authvar_mor_linear_result test_after(
	struct payload_mm_authvar_mor_linear_state *state)
{
	return payload_mm_authvar_mor_linear_after_bootmem(state,
		&clear_workspace, &callback_snapshot);
}

#define payload_mm_authvar_mor_linear_before_bootmem test_before
#define payload_mm_authvar_mor_linear_after_bootmem test_after

static void reenter_before(struct test_context *context)
{
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(context->nested_state,
		context->ops) == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
}

static void reenter_after(struct test_context *context)
{
	CHECK(payload_mm_authvar_mor_linear_after_bootmem(context->nested_state) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
}

static enum cb_err dummy_callback(void)
{
	return CB_SUCCESS;
}

static enum cb_err classify_guard(void *argument,
	struct payload_mm_authvar_mor_linear_boot *boot)
{
	struct test_context *context = argument;

	context->classify_calls++;
	if (context->race && context->race->hold_classify) {
		atomic_store_explicit(&context->race->callback_entered, 1,
			memory_order_release);
		while (!atomic_load_explicit(&context->race->competitor_done,
			memory_order_acquire))
			sched_yield();
	}
	if (context->failure == REENTER_CLASSIFY)
		reenter_before(context);
	if (context->failure == TAMPER_CLASSIFY)
		context->state->reserved = 1;
	if (context->failure == FAIL_CLASSIFY)
		return CB_ERR;
	*boot = (struct payload_mm_authvar_mor_linear_boot) {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_REVISION,
		.size = sizeof(*boot),
		.generation = context->kind ==
			PAYLOAD_MM_AUTHVAR_MOR_LINEAR_COLD_BOOT ? 7 : 0,
		.kind = context->kind,
	};
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_probe_entry(
	struct payload_mm_authvar_mor_entry *entry)
{
	active->probe_calls++;
	memset(entry, 0, sizeof(*entry));
	if (active->failure == FAIL_PROBE)
		return CB_ERR;
	*entry = active->entry;
	return CB_SUCCESS;
}

static enum cb_err reservations_register(void *argument)
{
	struct test_context *context = argument;

	context->reserve_calls++;
	if (context->failure == REENTER_RESERVE)
		reenter_before(context);
	if (context->failure == TAMPER_RESERVE)
		context->state->generation++;
	return context->failure == FAIL_RESERVE ? CB_ERR : CB_SUCCESS;
}

static enum cb_err resolve_binding(void *argument, uint64_t generation,
	struct payload_mm_authvar_mor_clear_plan *plan,
	struct payload_mm_authvar_mor_clear_executor_ops *executor)
{
	struct test_context *context = argument;

	context->resolve_calls++;
	if (context->race && context->race->hold_resolve) {
		atomic_store_explicit(&context->race->callback_entered, 1,
			memory_order_release);
		while (!atomic_load_explicit(&context->race->competitor_done,
			memory_order_acquire))
			sched_yield();
	}
	if (context->failure == REENTER_RESOLVE)
		reenter_after(context);
	CHECK(generation == 7);
	if (context->failure == FAIL_RESOLVE)
		return CB_ERR;
	plan->revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	plan->size = sizeof(*plan);
	plan->inventory_generation = generation;
	plan->span_count = 1;
	plan->spans[0].base = 0x1000;
	plan->spans[0].size = 0x1000;
	plan->spans[0].span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED;
	const uintptr_t globals_base = (uintptr_t)&clear_workspace <
		(uintptr_t)&callback_snapshot ? (uintptr_t)&clear_workspace :
		(uintptr_t)&callback_snapshot;
	const uintptr_t globals_end = (uintptr_t)&clear_workspace +
		sizeof(clear_workspace) > (uintptr_t)&callback_snapshot +
		sizeof(callback_snapshot) ? (uintptr_t)&clear_workspace +
		sizeof(clear_workspace) : (uintptr_t)&callback_snapshot +
		sizeof(callback_snapshot);
	plan->spans[1] = (struct payload_mm_authvar_mor_grant_span) {
		.base = globals_base,
		.size = globals_end - globals_base,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
		.exclusion_reason = PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	plan->spans[2] = (struct payload_mm_authvar_mor_grant_span) {
		.base = (uintptr_t)context->state,
		.size = sizeof(*context->state),
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
		.exclusion_reason = PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	plan->span_count = 3;
	executor->context = context;
	executor->window_bytes = 0x1000;
	executor->dma_snapshot = (void *)dummy_callback;
	executor->map_window = (void *)dummy_callback;
	executor->cache_writeback_invalidate = (void *)dummy_callback;
	executor->fence = (void *)dummy_callback;
	executor->unmap_window = (void *)dummy_callback;
	executor->inventory_validate = (void *)dummy_callback;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_clear_plan_validate(
	const struct payload_mm_authvar_mor_clear_plan *plan)
{
	return plan->revision == PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION &&
		plan->inventory_generation == 7 && plan->span_count == 3 ?
		CB_SUCCESS : CB_ERR;
}

enum cb_err payload_mm_authvar_mor_clear_execute(
	struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	uint64_t cold_boot_generation,
	const struct payload_mm_authvar_mor_clear_executor_ops *ops,
	struct payload_mm_authvar_mor_clear_transcript *transcript,
	struct payload_mm_authvar_mor_grant *grant)
{
	struct test_context *context = ops->context;

	CHECK(workspace == &clear_workspace);
	context->execute_calls++;
	CHECK(plan->inventory_generation == 7 && entry->present == 1 &&
		(entry->value & 1) && cold_boot_generation == 7);
	if (context->failure == FAIL_EXECUTE)
		return CB_ERR;
	transcript->revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	grant->revision = PAYLOAD_MM_AUTHVAR_MOR_GRANT_REVISION;
	grant->size = sizeof(*grant);
	grant->cold_boot_generation = 7;
	return CB_SUCCESS;
}

static enum cb_err private_complete(void *argument,
	const struct payload_mm_authvar_mor_grant *grant)
{
	struct test_context *context = argument;

	context->complete_calls++;
	if (context->failure == REENTER_COMPLETE)
		reenter_after(context);
	CHECK(grant->revision == PAYLOAD_MM_AUTHVAR_MOR_GRANT_REVISION &&
		grant->cold_boot_generation == 7);
	if (context->failure == TAMPER_COMPLETE)
		context->state->grant.revision = 0;
	return context->failure == FAIL_COMPLETE ? CB_ERR : CB_SUCCESS;
}

static enum cb_err private_close(void *argument)
{
	struct test_context *context = argument;

	context->close_calls++;
	if (context->race && context->race->hold_close) {
		atomic_store_explicit(&context->race->callback_entered, 1,
			memory_order_release);
		while (!atomic_load_explicit(&context->race->competitor_done,
			memory_order_acquire))
			sched_yield();
	}
	if (context->failure == REENTER_CLOSE)
		reenter_before(context);
	return context->failure == FAIL_CLOSE ? CB_ERR : CB_SUCCESS;
}

static struct payload_mm_authvar_mor_linear_ops operations(
	struct test_context *context)
{
	return (struct payload_mm_authvar_mor_linear_ops) {
		.context = context,
		.context_size = sizeof(*context),
		.classify_guard = classify_guard,
		.reservations_register = reservations_register,
		.resolve_binding = resolve_binding,
		.private_complete = private_complete,
		.private_close = private_close,
	};
}

static void initialize(struct test_context *context,
	struct payload_mm_authvar_mor_linear_state *state)
{
	memset(context, 0, sizeof(*context));
	memset(state, 0, sizeof(*state));
	memset(&clear_workspace, 0, sizeof(clear_workspace));
	memset(&callback_snapshot, 0, sizeof(callback_snapshot));
	context->state = state;
	context->kind = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_COLD_BOOT;
	context->entry.present = 1;
	context->entry.value = 1;
	active = context;
	payload_mm_authvar_mor_linear_reset_test();
}

static void test_success(void)
{
	struct payload_mm_authvar_mor_linear_state state;
	struct test_context context;
	struct payload_mm_authvar_mor_linear_ops ops;

	initialize(&context, &state);
	ops = operations(&context);
	context.ops = &ops;
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(&state, &ops) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_WAIT_FOR_BOOTMEM);
	CHECK(context.classify_calls == 1 && context.probe_calls == 1 &&
		context.reserve_calls == 1 && !context.close_calls);
	CHECK(payload_mm_authvar_mor_linear_after_bootmem(&state) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_CONTINUE);
	CHECK(state.phase == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_COMPLETE &&
		context.resolve_calls == 1 && context.execute_calls == 1 &&
		context.complete_calls == 1 && !context.close_calls);
	CHECK(payload_mm_authvar_mor_linear_after_bootmem(&state) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(state.phase == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_COMPLETE &&
		state.failure == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_NONE);
}

static void test_non_requests(void)
{
	struct payload_mm_authvar_mor_linear_state state;
	struct test_context context;
	struct payload_mm_authvar_mor_linear_ops ops;

	initialize(&context, &state);
	context.entry.present = 0;
	ops = operations(&context);
	context.ops = &ops;
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(&state, &ops) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_CONTINUE);
	CHECK(state.phase == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_CLOSED &&
		context.close_calls == 1 && !context.reserve_calls);

	initialize(&context, &state);
	context.entry.value = 2;
	ops = operations(&context);
	context.ops = &ops;
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(&state, &ops) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_CONTINUE);
	CHECK(context.close_calls == 1 && !context.reserve_calls);

	initialize(&context, &state);
	context.kind = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_S3_RESUME;
	ops = operations(&context);
	context.ops = &ops;
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(&state, &ops) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_CONTINUE);
	CHECK(context.close_calls == 1 && !context.probe_calls &&
		!context.reserve_calls);
}

static void expect_before_failure(enum failure_point point,
	enum payload_mm_authvar_mor_linear_failure failure)
{
	struct payload_mm_authvar_mor_linear_state state;
	struct test_context context;
	struct payload_mm_authvar_mor_linear_ops ops;

	initialize(&context, &state);
	context.failure = point;
	ops = operations(&context);
	context.ops = &ops;
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(&state, &ops) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(state.phase == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILED &&
		state.failure == failure);
	CHECK(context.close_calls == 1);
}

static void expect_after_failure(enum failure_point point,
	enum payload_mm_authvar_mor_linear_failure failure)
{
	struct payload_mm_authvar_mor_linear_state state;
	struct test_context context;
	struct payload_mm_authvar_mor_linear_ops ops;

	initialize(&context, &state);
	ops = operations(&context);
	context.ops = &ops;
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(&state, &ops) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_WAIT_FOR_BOOTMEM);
	context.failure = point;
	CHECK(payload_mm_authvar_mor_linear_after_bootmem(&state) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(state.phase == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILED &&
		state.failure == failure);
	CHECK(context.close_calls == 1);
}

static void test_dirty_initial_state_closes_authority(void)
{
	struct payload_mm_authvar_mor_linear_state state;
	struct test_context context;
	struct payload_mm_authvar_mor_linear_ops ops;

	initialize(&context, &state);
	ops = operations(&context);
	context.ops = &ops;
	state.reserved = 1;
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(&state, &ops) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(state.phase == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILED &&
		state.failure == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PROVIDER &&
		context.close_calls == 1);
}

static void test_failures(void)
{
	payload_mm_authvar_mor_linear_reset_test();
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(NULL, NULL) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(payload_mm_authvar_mor_linear_after_bootmem(NULL) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	expect_before_failure(FAIL_CLASSIFY,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLASSIFICATION);
	expect_before_failure(TAMPER_CLASSIFY,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLASSIFICATION);
	expect_before_failure(FAIL_PROBE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PROBE);
	expect_before_failure(FAIL_RESERVE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_RESERVATION);
	expect_before_failure(TAMPER_RESERVE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_RESERVATION);
	expect_after_failure(FAIL_RESOLVE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_BINDING);
	expect_after_failure(FAIL_EXECUTE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLEAR);
	expect_after_failure(FAIL_COMPLETE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_COMMIT);
	expect_after_failure(TAMPER_COMPLETE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_COMMIT);

	struct payload_mm_authvar_mor_linear_state state;
	struct test_context context;
	struct payload_mm_authvar_mor_linear_ops ops;
	initialize(&context, &state);
	context.entry.present = 0;
	context.failure = FAIL_CLOSE;
	ops = operations(&context);
	context.ops = &ops;
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(&state, &ops) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(state.failure == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLOSE);
}

static void expect_reentry(enum failure_point point,
	enum payload_mm_authvar_mor_linear_failure failure, bool absent,
	bool owner_alias)
{
	struct payload_mm_authvar_mor_linear_state state, nested_state;
	struct test_context context;
	struct payload_mm_authvar_mor_linear_ops ops;

	initialize(&context, &state);
	memset(&nested_state, 0, sizeof(nested_state));
	context.nested_state = owner_alias ? &state : &nested_state;
	context.failure = point;
	if (absent)
		context.entry.present = 0;
	ops = operations(&context);
	context.ops = &ops;
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(&state, &ops) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(state.phase == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILED &&
		state.failure == failure);
	CHECK(context.classify_calls == 1);
	if (point == REENTER_CLASSIFY || point == REENTER_CLOSE)
		CHECK(!context.reserve_calls);
	CHECK(!context.resolve_calls && !context.execute_calls &&
		!context.complete_calls);
}

static void expect_late_reentry(enum failure_point point,
	enum payload_mm_authvar_mor_linear_failure failure, bool owner_alias)
{
	struct payload_mm_authvar_mor_linear_state state, nested_state;
	struct test_context context;
	struct payload_mm_authvar_mor_linear_ops ops;

	initialize(&context, &state);
	memset(&nested_state, 0, sizeof(nested_state));
	context.nested_state = owner_alias ? &state : &nested_state;
	ops = operations(&context);
	context.ops = &ops;
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(&state, &ops) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_WAIT_FOR_BOOTMEM);
	context.failure = point;
	CHECK(payload_mm_authvar_mor_linear_after_bootmem(&state) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(state.phase == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILED &&
		state.failure == failure);
	if (point == REENTER_RESOLVE)
		CHECK(!context.execute_calls && !context.complete_calls);
	else
		CHECK(context.execute_calls == 1 && context.complete_calls == 1);
}

static void test_reentry(void)
{
	expect_reentry(REENTER_CLASSIFY,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLASSIFICATION, false, false);
	expect_reentry(REENTER_RESERVE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_RESERVATION, false, false);
	expect_reentry(REENTER_CLOSE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLOSE, true, false);
	expect_late_reentry(REENTER_RESOLVE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_BINDING, false);
	expect_late_reentry(REENTER_COMPLETE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_COMMIT, false);
	/* Repeat every external boundary with the exact owner state aliased. */
	expect_reentry(REENTER_CLASSIFY,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLASSIFICATION, false, true);
	expect_reentry(REENTER_RESERVE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_RESERVATION, false, true);
	expect_reentry(REENTER_CLOSE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLOSE, true, true);
	expect_late_reentry(REENTER_RESOLVE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_BINDING, true);
	expect_late_reentry(REENTER_COMPLETE,
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_COMMIT, true);
}

static void test_owner_substitution(void)
{
	struct payload_mm_authvar_mor_linear_state state, substitute;
	struct test_context context;
	struct payload_mm_authvar_mor_linear_ops ops;

	initialize(&context, &state);
	ops = operations(&context);
	context.ops = &ops;
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(&state, &ops) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_WAIT_FOR_BOOTMEM);
	memcpy(&substitute, &state, sizeof(substitute));
	CHECK(payload_mm_authvar_mor_linear_after_bootmem(&substitute) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(!context.resolve_calls && !context.execute_calls &&
		!context.complete_calls);
	CHECK(context.close_calls == 0);
	CHECK(payload_mm_authvar_mor_linear_after_bootmem(&state) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(context.close_calls == 1);
}

struct thread_argument {
	struct payload_mm_authvar_mor_linear_state state;
	struct test_context context;
	struct payload_mm_authvar_mor_linear_ops ops;
	struct race_sync *race;
	enum payload_mm_authvar_mor_linear_result result;
};

static void *race_entry(void *argument)
{
	struct thread_argument *thread = argument;

	thread->result = payload_mm_authvar_mor_linear_before_bootmem(
		&thread->state, &thread->ops);
	atomic_store_explicit(&thread->race->competitor_done, 1,
		memory_order_release);
	return NULL;
}

static void test_concurrent_entry(void)
{
	struct race_sync race = { .hold_classify = true };
	struct thread_argument arguments[2];
	pthread_t threads[2];

	payload_mm_authvar_mor_linear_reset_test();
	for (size_t index = 0; index < 2; index++) {
		memset(&arguments[index], 0, sizeof(arguments[index]));
		arguments[index].context.state = &arguments[index].state;
		arguments[index].context.kind =
			PAYLOAD_MM_AUTHVAR_MOR_LINEAR_COLD_BOOT;
		arguments[index].context.entry.present = 1;
		arguments[index].context.entry.value = 1;
		arguments[index].context.race = &race;
		arguments[index].race = &race;
		arguments[index].ops = operations(&arguments[index].context);
		arguments[index].context.ops = &arguments[index].ops;
	}
	CHECK(!pthread_create(&threads[0], NULL, race_entry, &arguments[0]));
	while (!atomic_load_explicit(&race.callback_entered, memory_order_acquire))
		sched_yield();
	CHECK(!pthread_create(&threads[1], NULL, race_entry, &arguments[1]));
	CHECK(!pthread_join(threads[0], NULL));
	CHECK(!pthread_join(threads[1], NULL));
	CHECK(arguments[0].result == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT &&
		arguments[1].result == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(arguments[0].context.classify_calls +
		arguments[1].context.classify_calls == 1);
	CHECK(!arguments[0].context.probe_calls &&
		!arguments[1].context.probe_calls &&
		!arguments[0].context.reserve_calls &&
		!arguments[1].context.reserve_calls);
	CHECK(arguments[0].context.close_calls +
		arguments[1].context.close_calls == 1);
}

struct after_thread_argument {
	struct payload_mm_authvar_mor_linear_state *state;
	struct race_sync *race;
	enum payload_mm_authvar_mor_linear_result result;
};

static void *race_after(void *argument)
{
	struct after_thread_argument *thread = argument;

	thread->result = payload_mm_authvar_mor_linear_after_bootmem(thread->state);
	return NULL;
}

static void test_concurrent_after_same_owner(void)
{
	struct payload_mm_authvar_mor_linear_state state;
	struct test_context context;
	struct payload_mm_authvar_mor_linear_ops ops;
	struct race_sync race = { .hold_resolve = true };
	struct after_thread_argument arguments[2] = {
		{ .state = &state, .race = &race },
		{ .state = &state, .race = &race },
	};
	pthread_t threads[2];

	initialize(&context, &state);
	context.race = &race;
	ops = operations(&context);
	context.ops = &ops;
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(&state, &ops) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_WAIT_FOR_BOOTMEM);
	CHECK(!pthread_create(&threads[0], NULL, race_after, &arguments[0]));
	while (!atomic_load_explicit(&race.callback_entered, memory_order_acquire))
		sched_yield();
	CHECK(!pthread_create(&threads[1], NULL, race_after, &arguments[1]));
	CHECK(!pthread_join(threads[1], NULL));
	CHECK(payload_mm_authvar_mor_linear_after_bootmem(&state) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(context.close_calls == 0);
	atomic_store_explicit(&race.competitor_done, 1, memory_order_release);
	CHECK(!pthread_join(threads[0], NULL));
	CHECK(arguments[0].result == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT &&
		arguments[1].result == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(context.resolve_calls == 1 && !context.execute_calls &&
		!context.complete_calls && context.close_calls == 1);
}

static void test_concurrent_poisoned_reserved_cleanup(void)
{
	struct payload_mm_authvar_mor_linear_state state, substitute;
	struct test_context context;
	struct payload_mm_authvar_mor_linear_ops ops;
	struct race_sync race = { .hold_close = true };
	struct after_thread_argument arguments[2] = {
		{ .state = &state, .race = &race },
		{ .state = &state, .race = &race },
	};
	pthread_t threads[2];

	initialize(&context, &state);
	ops = operations(&context);
	context.ops = &ops;
	CHECK(payload_mm_authvar_mor_linear_before_bootmem(&state, &ops) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_WAIT_FOR_BOOTMEM);
	memcpy(&substitute, &state, sizeof(substitute));
	CHECK(payload_mm_authvar_mor_linear_after_bootmem(&substitute) ==
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	context.race = &race;
	CHECK(!pthread_create(&threads[0], NULL, race_after, &arguments[0]));
	while (!atomic_load_explicit(&race.callback_entered, memory_order_acquire))
		sched_yield();
	CHECK(!pthread_create(&threads[1], NULL, race_after, &arguments[1]));
	CHECK(!pthread_join(threads[1], NULL));
	atomic_store_explicit(&race.competitor_done, 1, memory_order_release);
	CHECK(!pthread_join(threads[0], NULL));
	CHECK(arguments[0].result == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT &&
		arguments[1].result == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT);
	CHECK(context.close_calls == 1 && !context.resolve_calls &&
		!context.execute_calls && !context.complete_calls);
}

int main(void)
{
	test_success();
	test_non_requests();
	test_dirty_initial_state_closes_authority();
	test_failures();
	test_reentry();
	test_owner_substitution();
	test_concurrent_entry();
	test_concurrent_after_same_owner();
	test_concurrent_poisoned_reserved_cleanup();
	CHECK(!strcmp(payload_mm_authvar_mor_linear_failure_name(
		PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLEAR),
		"memory clear/readback"));
	return 0;
}
