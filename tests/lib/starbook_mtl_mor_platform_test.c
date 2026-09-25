/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_linear.h>
#include <boot/payload_mm_authvar_smm_loader.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/mor_platform.h"
#include "../../src/mainboard/starlabs/starbook/variants/mtl/mor_clear_x86.h"
#include "../../src/mainboard/starlabs/starbook/variants/mtl/mor_cold_boot.h"

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)

static uint32_t boot_kind = STARBOOK_MTL_MOR_BOOT_COLD;
static unsigned int entropy_index;
static unsigned int private_calls;
static unsigned int prepare_calls;
static bool mutate_provider;
static bool descriptor_context;
static bool seed_reentry;
static bool seed_reentry_result;
static bool callback_reentry;
static bool boundary_failure;
static bool classification_failure;
static bool concurrent_hold;
static unsigned int boundary_load_calls;
static unsigned int classification_calls;
static unsigned int concurrent_start;
static unsigned int concurrent_entered;
static unsigned int concurrent_release;
static unsigned int concurrent_completed;
static bool claim_conflict_hold;
static unsigned int claim_conflict_entered;
static unsigned int claim_conflict_release;
static bool leave_hold;
static unsigned int leave_entered;
static unsigned int leave_release;
static unsigned int scratch_alias;
static struct { uint64_t word; } private_context;
static void *boundary_context = &private_context;
static size_t boundary_context_size = sizeof(private_context);
static struct payload_mm_authvar_smm_arena_seed reentry_seed;
static struct payload_mm_authvar_mor_linear_ops reentry_ops;

enum cb_err get_random_number_64(uint64_t *value)
{
	*value = 0x1020304050607080ULL + ++entropy_index;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_mor_early_dma_classify(uint32_t *kind,
	uint64_t *generation, struct starbook_mtl_dma_guard_snapshot *guard)
{
	classification_calls++;
	if (classification_failure)
		return CB_ERR;
	memset(guard, 0, sizeof(*guard));
	*kind = boot_kind;
	*generation = 7;
	guard->generation = boot_kind == STARBOOK_MTL_MOR_BOOT_COLD ? 7 : 0;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_mor_clear_x86_register(
	struct starbook_mtl_mor_clear_x86_reservations *reservations)
{
	memset(reservations, 0, sizeof(*reservations));
	reservations->revision = STARBOOK_MTL_MOR_CLEAR_X86_REVISION;
	reservations->size = sizeof(*reservations);
	reservations->registered = 1;
	return CB_SUCCESS;
}

static enum cb_err executor_stub(void *context,
	struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot)
{
	(void)context;
	(void)snapshot;
	return CB_SUCCESS;
}

static enum cb_err inventory_stub(void *context,
	const struct payload_mm_authvar_mor_clear_plan *plan)
{
	(void)context;
	(void)plan;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_mor_clear_x86_prepare(
	const struct starbook_mtl_mor_clear_x86_reservations *reservations,
	const struct starbook_mtl_dma_guard_snapshot *guard, bool resume_from_s3,
	struct payload_mm_authvar_mor_clear_plan *plan,
	struct starbook_mtl_mor_clear_x86_binding *binding)
{
	(void)reservations;
	(void)guard;
	(void)resume_from_s3;
	prepare_calls++;
	memset(plan, 0, sizeof(*plan));
	memset(binding, 0, sizeof(*binding));
	binding->ops.window_bytes = 4096;
	binding->ops.dma_snapshot = executor_stub;
	binding->ops.map_window = (void *)1;
	binding->ops.cache_writeback_invalidate = (void *)1;
	binding->ops.fence = (void *)1;
	binding->ops.unmap_window = (void *)1;
	binding->ops.inventory_validate = inventory_stub;
	return CB_SUCCESS;
}

static enum cb_err boundary_call(void *context, uint64_t generation,
	const uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE])
{
	CHECK(context == boundary_context && generation == 7 && owner);
	private_calls++;
	if (concurrent_hold) {
		__atomic_store_n(&concurrent_entered, 1U, __ATOMIC_RELEASE);
		while (!__atomic_load_n(&concurrent_release, __ATOMIC_ACQUIRE))
			sched_yield();
	}
	if (seed_reentry) {
		struct payload_mm_authvar_smm_arena_seed unchanged;
		const unsigned int entropy_calls = entropy_index;

		seed_reentry = false;
		memset(&reentry_seed, 0xa5, sizeof(reentry_seed));
		unchanged = reentry_seed;
		seed_reentry_result =
			platform_payload_mm_authvar_smm_arena_seed(&reentry_seed);
		CHECK(!seed_reentry_result && entropy_index == entropy_calls &&
			!memcmp(&reentry_seed, &unchanged, sizeof(unchanged)));
	}
	if (callback_reentry) {
		callback_reentry = false;
		CHECK(reentry_ops.private_close(reentry_ops.context) == CB_ERR);
	}
	if (mutate_provider) {
		struct payload_mm_authvar_mor_linear_ops ops;

		/* Re-entry cannot replace the already frozen provider. */
		CHECK(platform_payload_mm_authvar_mor_linear_ops(&ops));
	}
	return CB_SUCCESS;
}

static enum cb_err boundary_register(void *context, uint64_t generation)
{
	uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE] = { 1 };

	return boundary_call(context, generation, owner);
}

static enum cb_err boundary_complete(void *context,
	const struct payload_mm_authvar_mor_grant *grant)
{
	CHECK(context == boundary_context && grant);
	private_calls++;
	return CB_SUCCESS;
}

static enum cb_err boundary_close(void *context)
{
	CHECK(context == boundary_context);
	private_calls++;
	return CB_SUCCESS;
}

bool starbook_mtl_mor_private_boundary(
	struct starbook_mtl_mor_private_boundary_ops *ops)
{
	boundary_load_calls++;
	if (boundary_failure)
		return false;
	*ops = (struct starbook_mtl_mor_private_boundary_ops) {
		.revision = STARBOOK_MTL_MOR_PRIVATE_BOUNDARY_REVISION,
		.size = sizeof(*ops),
		.context = descriptor_context ? (void *)ops : boundary_context,
		.context_size = descriptor_context ? sizeof(*ops) :
			boundary_context_size,
		.arena_seed = boundary_call,
		.reservations_register = boundary_register,
		.resolve = boundary_call,
		.complete = boundary_complete,
		.close = boundary_close,
	};
	return true;
}

static void reset_test(void)
{
	starbook_mtl_mor_platform_reset_test();
	boot_kind = STARBOOK_MTL_MOR_BOOT_COLD;
	entropy_index = 0;
	private_calls = 0;
	prepare_calls = 0;
	mutate_provider = false;
	descriptor_context = false;
	seed_reentry = false;
	seed_reentry_result = true;
	callback_reentry = false;
	boundary_failure = false;
	classification_failure = false;
	concurrent_hold = false;
	boundary_load_calls = 0;
	classification_calls = 0;
	__atomic_store_n(&concurrent_start, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&concurrent_entered, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&concurrent_release, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&concurrent_completed, 0U, __ATOMIC_RELAXED);
	claim_conflict_hold = false;
	__atomic_store_n(&claim_conflict_entered, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&claim_conflict_release, 0U, __ATOMIC_RELAXED);
	leave_hold = false;
	__atomic_store_n(&leave_entered, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&leave_release, 0U, __ATOMIC_RELAXED);
	scratch_alias = 0;
	memset(&reentry_ops, 0, sizeof(reentry_ops));
	boundary_context = &private_context;
	boundary_context_size = sizeof(private_context);
}

void starbook_mtl_mor_platform_claim_conflict_test_hook(void)
{
	if (!claim_conflict_hold)
		return;
	__atomic_store_n(&claim_conflict_entered, 1U, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&claim_conflict_release, __ATOMIC_ACQUIRE))
		sched_yield();
}

void starbook_mtl_mor_platform_leave_test_hook(void)
{
	if (!leave_hold)
		return;
	__atomic_store_n(&leave_entered, 1U, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&leave_release, __ATOMIC_ACQUIRE))
		sched_yield();
}

void starbook_mtl_mor_platform_constructor_scratch_test_hook(
	const void *candidate, size_t candidate_size,
	const void *original, size_t original_size)
{
	if (scratch_alias == 1U) {
		boundary_context = (void *)candidate;
		boundary_context_size = candidate_size;
	} else if (scratch_alias == 2U) {
		boundary_context = (void *)original;
		boundary_context_size = original_size;
	}
}

struct concurrent_seed_call {
	struct payload_mm_authvar_smm_arena_seed seed;
	bool result;
};

struct concurrent_reservation_call {
	struct payload_mm_authvar_mor_linear_ops *ops;
	enum cb_err result;
};

struct concurrent_resolution_call {
	struct payload_mm_authvar_mor_linear_ops *ops;
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_clear_executor_ops executor;
	enum cb_err result;
};

struct concurrent_complete_call {
	struct payload_mm_authvar_mor_linear_ops *ops;
	const struct payload_mm_authvar_mor_grant *grant;
	enum cb_err result;
};

static void *concurrent_seed(void *argument)
{
	struct concurrent_seed_call *call = argument;

	while (!__atomic_load_n(&concurrent_start, __ATOMIC_ACQUIRE))
		sched_yield();
	call->result = platform_payload_mm_authvar_smm_arena_seed(&call->seed);
	__atomic_add_fetch(&concurrent_completed, 1U, __ATOMIC_RELEASE);
	return NULL;
}

static void *concurrent_reservation(void *argument)
{
	struct concurrent_reservation_call *call = argument;

	while (!__atomic_load_n(&concurrent_start, __ATOMIC_ACQUIRE))
		sched_yield();
	call->result = call->ops->reservations_register(call->ops->context);
	__atomic_add_fetch(&concurrent_completed, 1U, __ATOMIC_RELEASE);
	return NULL;
}

static void *concurrent_resolution(void *argument)
{
	struct concurrent_resolution_call *call = argument;

	while (!__atomic_load_n(&concurrent_start, __ATOMIC_ACQUIRE))
		sched_yield();
	call->result = call->ops->resolve_binding(call->ops->context, 7,
		&call->plan, &call->executor);
	__atomic_add_fetch(&concurrent_completed, 1U, __ATOMIC_RELEASE);
	return NULL;
}

static void *concurrent_complete(void *argument)
{
	struct concurrent_complete_call *call = argument;

	call->result = call->ops->private_complete(call->ops->context, call->grant);
	__atomic_add_fetch(&concurrent_completed, 1U, __ATOMIC_RELEASE);
	return NULL;
}

static void competing_seed_poison(void)
{
	struct concurrent_seed_call calls[2];
	struct payload_mm_authvar_smm_arena_seed unchanged;
	pthread_t threads[2];

	reset_test();
	concurrent_hold = true;
	memset(calls, 0x5a, sizeof(calls));
	memset(&unchanged, 0x5a, sizeof(unchanged));
	CHECK(!pthread_create(&threads[0], NULL, concurrent_seed, &calls[0]));
	__atomic_store_n(&concurrent_start, 1U, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&concurrent_entered, __ATOMIC_ACQUIRE))
		sched_yield();
	CHECK(!pthread_create(&threads[1], NULL, concurrent_seed, &calls[1]));
	while (!__atomic_load_n(&concurrent_completed, __ATOMIC_ACQUIRE))
		sched_yield();
	__atomic_store_n(&concurrent_release, 1U, __ATOMIC_RELEASE);
	CHECK(!pthread_join(threads[0], NULL));
	CHECK(!pthread_join(threads[1], NULL));
	CHECK(!calls[0].result && !calls[1].result);
	CHECK(starbook_mtl_mor_platform_poisoned_test());
	CHECK(!memcmp(&calls[0].seed, &unchanged, sizeof(unchanged)) &&
		!memcmp(&calls[1].seed, &unchanged, sizeof(unchanged)));
	CHECK(boundary_load_calls == 1 && classification_calls == 1 &&
		entropy_index == 4 && private_calls == 1);
}

static void claim_conflict_blocks_owner_release(void)
{
	struct payload_mm_authvar_smm_arena_seed initial;
	struct concurrent_seed_call calls[2];
	struct payload_mm_authvar_smm_arena_seed unchanged;
	pthread_t threads[2];

	reset_test();
	CHECK(platform_payload_mm_authvar_smm_arena_seed(&initial));
	claim_conflict_hold = true;
	leave_hold = true;
	memset(calls, 0x5a, sizeof(calls));
	memset(&unchanged, 0x5a, sizeof(unchanged));
	CHECK(!pthread_create(&threads[0], NULL, concurrent_seed, &calls[0]));
	__atomic_store_n(&concurrent_start, 1U, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&leave_entered, __ATOMIC_ACQUIRE))
		sched_yield();
	CHECK(!pthread_create(&threads[1], NULL, concurrent_seed, &calls[1]));
	while (!__atomic_load_n(&claim_conflict_entered, __ATOMIC_ACQUIRE))
		sched_yield();
	__atomic_store_n(&leave_release, 1U, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&concurrent_completed, __ATOMIC_ACQUIRE))
		sched_yield();
	CHECK(!calls[0].result &&
		!memcmp(&calls[0].seed, &unchanged, sizeof(unchanged)));
	CHECK(starbook_mtl_mor_platform_poisoned_test());
	CHECK(starbook_mtl_mor_platform_owner_zero_test());
	__atomic_store_n(&claim_conflict_release, 1U, __ATOMIC_RELEASE);
	CHECK(!pthread_join(threads[0], NULL));
	CHECK(!pthread_join(threads[1], NULL));
	CHECK(!calls[1].result &&
		!memcmp(&calls[1].seed, &unchanged, sizeof(unchanged)));
	CHECK(boundary_load_calls == 1 && classification_calls == 1 &&
		entropy_index == 4 && private_calls == 1 &&
		__atomic_load_n(&concurrent_completed, __ATOMIC_ACQUIRE) == 2U);
}

static void claim_conflict_blocks_terminal_completion(void)
{
	struct payload_mm_authvar_smm_arena_seed seed;
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_clear_executor_ops executor;
	struct payload_mm_authvar_mor_grant grant = { 0 };
	struct concurrent_complete_call complete;
	struct concurrent_seed_call contender;
	struct payload_mm_authvar_smm_arena_seed unchanged;
	pthread_t threads[2];

	reset_test();
	CHECK(platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(platform_payload_mm_authvar_mor_linear_ops(&ops));
	CHECK(ops.reservations_register(ops.context) == CB_SUCCESS);
	CHECK(ops.resolve_binding(ops.context, 7, &plan, &executor) == CB_SUCCESS);
	complete = (struct concurrent_complete_call) {
		.ops = &ops,
		.grant = &grant,
		.result = CB_SUCCESS,
	};
	memset(&contender, 0x5a, sizeof(contender));
	memset(&unchanged, 0x5a, sizeof(unchanged));
	claim_conflict_hold = true;
	leave_hold = true;
	CHECK(!pthread_create(&threads[0], NULL, concurrent_complete, &complete));
	while (!__atomic_load_n(&leave_entered, __ATOMIC_ACQUIRE))
		sched_yield();
	CHECK(!pthread_create(&threads[1], NULL, concurrent_seed, &contender));
	__atomic_store_n(&concurrent_start, 1U, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&claim_conflict_entered, __ATOMIC_ACQUIRE))
		sched_yield();
	__atomic_store_n(&leave_release, 1U, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&concurrent_completed, __ATOMIC_ACQUIRE))
		sched_yield();
	CHECK(complete.result == CB_ERR);
	CHECK(starbook_mtl_mor_platform_poisoned_test());
	CHECK(starbook_mtl_mor_platform_owner_zero_test());
	__atomic_store_n(&claim_conflict_release, 1U, __ATOMIC_RELEASE);
	CHECK(!pthread_join(threads[0], NULL));
	CHECK(!pthread_join(threads[1], NULL));
	CHECK(!contender.result &&
		!memcmp(&contender.seed, &unchanged, sizeof(unchanged)));
	CHECK(private_calls == 4 && prepare_calls == 1 &&
		__atomic_load_n(&concurrent_completed, __ATOMIC_ACQUIRE) == 2U);
}

static void competing_reservation_poison(void)
{
	struct payload_mm_authvar_smm_arena_seed seed;
	struct payload_mm_authvar_mor_linear_ops ops;
	struct concurrent_reservation_call calls[2];
	pthread_t threads[2];

	reset_test();
	CHECK(platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(platform_payload_mm_authvar_mor_linear_ops(&ops));
	calls[0].ops = &ops;
	calls[1].ops = &ops;
	calls[0].result = CB_SUCCESS;
	calls[1].result = CB_SUCCESS;
	concurrent_hold = true;
	CHECK(!pthread_create(&threads[0], NULL, concurrent_reservation, &calls[0]));
	__atomic_store_n(&concurrent_start, 1U, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&concurrent_entered, __ATOMIC_ACQUIRE))
		sched_yield();
	CHECK(!pthread_create(&threads[1], NULL, concurrent_reservation, &calls[1]));
	while (!__atomic_load_n(&concurrent_completed, __ATOMIC_ACQUIRE))
		sched_yield();
	__atomic_store_n(&concurrent_release, 1U, __ATOMIC_RELEASE);
	CHECK(!pthread_join(threads[0], NULL));
	CHECK(!pthread_join(threads[1], NULL));
	CHECK(calls[0].result == CB_ERR && calls[1].result == CB_ERR);
	CHECK(private_calls == 2 && prepare_calls == 0);
	CHECK(starbook_mtl_mor_platform_poisoned_test());
	CHECK(starbook_mtl_mor_platform_owner_zero_test());
	CHECK(ops.reservations_register(ops.context) == CB_ERR);
	CHECK(private_calls == 2 && prepare_calls == 0);
}

static void competing_resolution_poison(void)
{
	struct payload_mm_authvar_smm_arena_seed seed;
	struct payload_mm_authvar_mor_linear_ops ops;
	struct concurrent_resolution_call calls[2];
	struct payload_mm_authvar_mor_clear_plan unchanged_plan;
	struct payload_mm_authvar_mor_clear_executor_ops unchanged_executor;
	pthread_t threads[2];

	reset_test();
	CHECK(platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(platform_payload_mm_authvar_mor_linear_ops(&ops));
	CHECK(ops.reservations_register(ops.context) == CB_SUCCESS);
	memset(&unchanged_plan, 0x5a, sizeof(unchanged_plan));
	memset(&unchanged_executor, 0xa5, sizeof(unchanged_executor));
	memset(calls, 0, sizeof(calls));
	for (size_t index = 0; index < 2; index++) {
		calls[index].ops = &ops;
		calls[index].plan = unchanged_plan;
		calls[index].executor = unchanged_executor;
		calls[index].result = CB_SUCCESS;
	}
	concurrent_hold = true;
	CHECK(!pthread_create(&threads[0], NULL, concurrent_resolution, &calls[0]));
	__atomic_store_n(&concurrent_start, 1U, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&concurrent_entered, __ATOMIC_ACQUIRE))
		sched_yield();
	CHECK(!pthread_create(&threads[1], NULL, concurrent_resolution, &calls[1]));
	while (!__atomic_load_n(&concurrent_completed, __ATOMIC_ACQUIRE))
		sched_yield();
	__atomic_store_n(&concurrent_release, 1U, __ATOMIC_RELEASE);
	CHECK(!pthread_join(threads[0], NULL));
	CHECK(!pthread_join(threads[1], NULL));
	CHECK(calls[0].result == CB_ERR && calls[1].result == CB_ERR);
	for (size_t index = 0; index < 2; index++)
		CHECK(!memcmp(&calls[index].plan, &unchanged_plan,
				sizeof(unchanged_plan)) &&
			!memcmp(&calls[index].executor, &unchanged_executor,
				sizeof(unchanged_executor)));
	CHECK(private_calls == 3 && prepare_calls == 1);
	CHECK(starbook_mtl_mor_platform_poisoned_test());
	CHECK(starbook_mtl_mor_platform_owner_zero_test());
}

static void boundary_failure_is_terminal(void)
{
	struct payload_mm_authvar_smm_arena_seed seed;
	struct payload_mm_authvar_smm_arena_seed unchanged;

	reset_test();
	boundary_failure = true;
	memset(&seed, 0x5a, sizeof(seed));
	unchanged = seed;
	CHECK(!platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(starbook_mtl_mor_platform_poisoned_test());
	CHECK(boundary_load_calls == 1 && classification_calls == 0 &&
		entropy_index == 0 && private_calls == 0);
	boundary_failure = false;
	CHECK(!platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(starbook_mtl_mor_platform_poisoned_test());
	CHECK(boundary_load_calls == 1 && classification_calls == 0 &&
		entropy_index == 0 && private_calls == 0 &&
		!memcmp(&seed, &unchanged, sizeof(seed)));
}

static void classification_failure_is_terminal(void)
{
	struct payload_mm_authvar_smm_arena_seed seed;
	struct payload_mm_authvar_smm_arena_seed unchanged;

	reset_test();
	classification_failure = true;
	memset(&seed, 0x5a, sizeof(seed));
	unchanged = seed;
	CHECK(!platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(starbook_mtl_mor_platform_poisoned_test());
	CHECK(boundary_load_calls == 1 && classification_calls == 1 &&
		entropy_index == 0 && private_calls == 0);
	classification_failure = false;
	CHECK(!platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(boundary_load_calls == 1 && classification_calls == 1 &&
		entropy_index == 0 && private_calls == 0 &&
		!memcmp(&seed, &unchanged, sizeof(seed)));
}

static void cold_path(void)
{
	struct payload_mm_authvar_smm_arena_seed seed;
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_linear_boot boot;
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_clear_executor_ops executor;
	struct payload_mm_authvar_mor_grant grant = { 0 };

	reset_test();
	CHECK(platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(seed.cold_boot_generation == 7 && seed.owner[0]);
	CHECK(platform_payload_mm_authvar_mor_linear_ops(&ops));
	CHECK(!platform_payload_mm_authvar_smm_arena_seed(ops.context));
	CHECK(!platform_payload_mm_authvar_mor_linear_ops(ops.context));
	CHECK(ops.classify_guard(ops.context, ops.context) == CB_ERR);
	CHECK(ops.classify_guard(ops.context, &boot) == CB_SUCCESS);
	CHECK(boot.kind == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_COLD_BOOT &&
		boot.generation == 7);
	CHECK(ops.reservations_register(ops.context) == CB_SUCCESS);
	CHECK(ops.reservations_register(ops.context) == CB_ERR);
	memset(&plan, 0, sizeof(plan));
	memset(&executor, 0, sizeof(executor));
	CHECK(ops.resolve_binding(ops.context, 7, &plan, &executor) == CB_SUCCESS);
	CHECK(ops.resolve_binding(ops.context, 7, &plan, &executor) == CB_ERR);
	CHECK(executor.window_bytes == 4096);
	CHECK(ops.private_complete(ops.context, ops.context) == CB_ERR);
	CHECK(ops.private_complete(ops.context, &grant) == CB_SUCCESS);
	CHECK(private_calls == 4);
}

static void s3_path(void)
{
	struct payload_mm_authvar_smm_arena_seed seed;
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_linear_boot boot;

	reset_test();
	boot_kind = STARBOOK_MTL_MOR_BOOT_S3;
	CHECK(platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(seed.cold_boot_generation == 7 && seed.owner[0]);
	CHECK(platform_payload_mm_authvar_mor_linear_ops(&ops));
	CHECK(ops.classify_guard(ops.context, &boot) == CB_SUCCESS);
	CHECK(boot.kind == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_S3_RESUME &&
		boot.generation == 0);
	CHECK(ops.private_close(ops.context) == CB_SUCCESS);
	boot_kind = STARBOOK_MTL_MOR_BOOT_COLD;
}

static void seed_reentry_poison(void)
{
	struct payload_mm_authvar_smm_arena_seed seed;
	struct payload_mm_authvar_smm_arena_seed unchanged;

	reset_test();
	memset(&seed, 0x5a, sizeof(seed));
	unchanged = seed;
	seed_reentry = true;
	CHECK(!platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(!seed_reentry_result && entropy_index == 4 && private_calls == 1);
	CHECK(!memcmp(&seed, &unchanged, sizeof(seed)));
	CHECK(!platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(entropy_index == 4 && private_calls == 1);
	CHECK(!memcmp(&seed, &unchanged, sizeof(seed)));
}

static void descriptor_context_alias(void)
{
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_linear_ops unchanged;

	reset_test();
	descriptor_context = true;
	memset(&ops, 0x5a, sizeof(ops));
	unchanged = ops;
	CHECK(!platform_payload_mm_authvar_mor_linear_ops(&ops));
	CHECK(!memcmp(&ops, &unchanged, sizeof(ops)));
}

static void constructor_scratch_context_alias(void)
{
	struct payload_mm_authvar_smm_arena_seed seed;
	struct payload_mm_authvar_smm_arena_seed seed_unchanged;
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_linear_ops ops_unchanged;

	for (unsigned int alias = 1U; alias <= 2U; alias++) {
		reset_test();
		scratch_alias = alias;
		memset(&seed, 0x5a, sizeof(seed));
		seed_unchanged = seed;
		CHECK(!platform_payload_mm_authvar_smm_arena_seed(&seed));
		CHECK(!memcmp(&seed, &seed_unchanged, sizeof(seed)));
		CHECK(boundary_load_calls == 1 && classification_calls == 0 &&
			entropy_index == 0 && private_calls == 0);
		CHECK(starbook_mtl_mor_platform_poisoned_test());
		CHECK(starbook_mtl_mor_platform_owner_zero_test());

		reset_test();
		scratch_alias = alias;
		memset(&ops, 0xa5, sizeof(ops));
		ops_unchanged = ops;
		CHECK(!platform_payload_mm_authvar_mor_linear_ops(&ops));
		CHECK(!memcmp(&ops, &ops_unchanged, sizeof(ops)));
		CHECK(boundary_load_calls == 1 && classification_calls == 0 &&
			entropy_index == 0 && private_calls == 0);
		CHECK(starbook_mtl_mor_platform_poisoned_test());
		CHECK(starbook_mtl_mor_platform_owner_zero_test());
	}
}

static void callback_reentry_poison(void)
{
	struct payload_mm_authvar_smm_arena_seed seed;
	struct payload_mm_authvar_smm_arena_seed failed_seed;
	struct payload_mm_authvar_smm_arena_seed failed_seed_unchanged;
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_linear_ops failed_ops;
	struct payload_mm_authvar_mor_linear_ops failed_ops_unchanged;

	reset_test();
	CHECK(platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(platform_payload_mm_authvar_mor_linear_ops(&ops));
	reentry_ops = ops;
	callback_reentry = true;
	CHECK(ops.reservations_register(ops.context) == CB_ERR);
	CHECK(private_calls == 2 && prepare_calls == 0);
	CHECK(starbook_mtl_mor_platform_poisoned_test());
	CHECK(starbook_mtl_mor_platform_owner_zero_test());
	memset(&failed_seed, 0x5a, sizeof(failed_seed));
	failed_seed_unchanged = failed_seed;
	memset(&failed_ops, 0xa5, sizeof(failed_ops));
	failed_ops_unchanged = failed_ops;
	CHECK(!platform_payload_mm_authvar_smm_arena_seed(&failed_seed));
	CHECK(!platform_payload_mm_authvar_mor_linear_ops(&failed_ops));
	CHECK(!memcmp(&failed_seed, &failed_seed_unchanged, sizeof(failed_seed)) &&
		!memcmp(&failed_ops, &failed_ops_unchanged, sizeof(failed_ops)));
	CHECK(ops.reservations_register(ops.context) == CB_ERR);
	CHECK(private_calls == 2 && prepare_calls == 0);
}

static void resolve_context_alias(void)
{
	struct payload_mm_authvar_smm_arena_seed seed;
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_clear_plan plan_unchanged;
	struct payload_mm_authvar_mor_clear_executor_ops executor;
	struct payload_mm_authvar_mor_clear_executor_ops executor_unchanged;

	reset_test();
	boundary_context = &plan;
	boundary_context_size = sizeof(plan);
	CHECK(platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(platform_payload_mm_authvar_mor_linear_ops(&ops));
	CHECK(ops.reservations_register(ops.context) == CB_SUCCESS);
	memset(&plan, 0x5a, sizeof(plan));
	memset(&executor, 0xa5, sizeof(executor));
	plan_unchanged = plan;
	executor_unchanged = executor;
	CHECK(ops.resolve_binding(ops.context, 7, &plan, &executor) == CB_ERR);
	CHECK(!memcmp(&plan, &plan_unchanged, sizeof(plan)));
	CHECK(!memcmp(&executor, &executor_unchanged, sizeof(executor)));
	CHECK(prepare_calls == 0 && private_calls == 2);
}

static void classify_context_alias(void)
{
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_linear_boot boot;
	struct payload_mm_authvar_mor_linear_boot unchanged;

	reset_test();
	boundary_context = &boot;
	boundary_context_size = sizeof(boot);
	CHECK(platform_payload_mm_authvar_mor_linear_ops(&ops));
	memset(&boot, 0x5a, sizeof(boot));
	unchanged = boot;
	CHECK(ops.classify_guard(ops.context, &boot) == CB_ERR);
	CHECK(!memcmp(&boot, &unchanged, sizeof(boot)));
}

static void completion_context_alias(void)
{
	struct payload_mm_authvar_smm_arena_seed seed;
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_clear_executor_ops executor;
	struct payload_mm_authvar_mor_grant grant;
	struct payload_mm_authvar_mor_grant unchanged;

	reset_test();
	boundary_context = &grant;
	boundary_context_size = sizeof(grant);
	CHECK(platform_payload_mm_authvar_smm_arena_seed(&seed));
	CHECK(platform_payload_mm_authvar_mor_linear_ops(&ops));
	CHECK(ops.reservations_register(ops.context) == CB_SUCCESS);
	CHECK(ops.resolve_binding(ops.context, 7, &plan, &executor) == CB_SUCCESS);
	memset(&grant, 0x5a, sizeof(grant));
	unchanged = grant;
	CHECK(ops.private_complete(ops.context, &grant) == CB_ERR);
	CHECK(!memcmp(&grant, &unchanged, sizeof(grant)));
	CHECK(private_calls == 3);
}

int main(void)
{
	cold_path();
	s3_path();
	seed_reentry_poison();
	competing_seed_poison();
	claim_conflict_blocks_owner_release();
	claim_conflict_blocks_terminal_completion();
	competing_reservation_poison();
	competing_resolution_poison();
	boundary_failure_is_terminal();
	classification_failure_is_terminal();
	descriptor_context_alias();
	constructor_scratch_context_alias();
	callback_reentry_poison();
	classify_context_alias();
	resolve_context_alias();
	completion_context_alias();
	return 0;
}
