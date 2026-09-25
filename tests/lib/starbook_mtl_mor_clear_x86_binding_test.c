/* SPDX-License-Identifier: GPL-2.0-only */

#include "../../src/mainboard/starlabs/starbook/variants/mtl/mor_clear_x86.h"
#include "../../src/mainboard/starlabs/starbook/variants/mtl/mor_live_inventory.h"

#include <boot/payload_mm_authvar_mor_live_inventory.h>
#include <commonlib/helpers.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)

static struct bootmem_aligned_reservation page_result = {
	.base = 0x200000,
	.size = PAE_PGTL_SIZE,
	.tag = BM_MEM_TABLE,
};
static struct bootmem_aligned_reservation aperture_result = {
	.base = 0x400000,
	.size = PAE_VMEM_SIZE,
	.tag = BM_MEM_RESERVED,
};
static unsigned int register_calls;
static unsigned int query_calls;
static unsigned int compose_calls;
static unsigned int backend_calls;
static unsigned int guard_calls;
static unsigned int fail_register_call;
static unsigned int fail_query_call;
static unsigned int fail_guard_call;
static void *mutate;
static void *mutate_second;
static struct starbook_mtl_mor_clear_x86_binding *reenter_binding;
static const struct payload_mm_authvar_mor_clear_plan *reenter_plan;
static unsigned int concurrent_hold;
static unsigned int concurrent_entered;
static unsigned int concurrent_release;

static void apply_mutation(void)
{
	if (mutate) {
		((uint8_t *)mutate)[0] ^= 1;
		mutate = NULL;
	}
	if (mutate_second) {
		((uint8_t *)mutate_second)[0] ^= 1;
		mutate_second = NULL;
	}
}

enum cb_err payload_mm_authvar_mor_clear_plan_validate(
	const struct payload_mm_authvar_mor_clear_plan *plan)
{
	return plan && plan->revision == PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION &&
		plan->size == sizeof(*plan) && plan->inventory_generation == 9 &&
		plan->inventory_identity[0] == 0x99 ? CB_SUCCESS : CB_ERR;
}

int bootmem_aligned_reservations_register(
	const struct bootmem_aligned_reservation_request *requests,
	size_t request_count, struct bootmem_aligned_reservation_handle *handles)
{
	register_calls++;
	CHECK(request_count == 2 && requests[0].revision ==
		BOOTMEM_ALIGNED_RESERVATION_REVISION &&
		requests[0].size == sizeof(*requests) &&
		requests[0].limit_exclusive == (1ULL << 32) &&
		requests[0].bytes == PAE_PGTL_SIZE &&
		requests[0].alignment == PAE_PGTL_ALIGN &&
		requests[0].tag == BM_MEM_TABLE &&
		requests[1].revision == BOOTMEM_ALIGNED_RESERVATION_REVISION &&
		requests[1].size == sizeof(*requests) &&
		requests[1].limit_exclusive == (1ULL << 32) &&
		requests[1].bytes == PAE_VMEM_SIZE &&
		requests[1].alignment == PAE_VMEM_ALIGN &&
		requests[1].tag == BM_MEM_RESERVED);
	if (register_calls == fail_register_call)
		return -1;
	for (size_t index = 0; index < request_count; index++) {
		handles[index].opaque[0] = index + 1U;
		handles[index].opaque[1] = 0x55aa0000U | (index + 1U);
	}
	return 0;
}

int bootmem_aligned_reservation_query(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_aligned_reservation *reservation)
{
	query_calls++;
	if (query_calls == fail_query_call)
		return -1;
	CHECK(handle->opaque[0] == query_calls &&
		handle->opaque[1] == (0x55aa0000U | query_calls));
	*reservation = query_calls == 1 ? page_result : aperture_result;
	apply_mutation();
	return 0;
}

enum cb_err starbook_mtl_mor_live_inventory_compose_with_overlays_owned(
	const struct starbook_mtl_dma_guard_snapshot *dma_guard,
	const struct payload_mm_authvar_mor_live_inventory_overlay *overlays,
	size_t overlay_count, struct payload_mm_authvar_mor_clear_plan *plan,
	struct starbook_mtl_mor_live_inventory_workspace *workspace)
{
	static const uint32_t reasons[] = {
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};

	compose_calls++;
	CHECK(workspace != NULL);
	if (__atomic_load_n(&concurrent_hold, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&concurrent_entered, 1U, __ATOMIC_RELEASE);
		while (!__atomic_load_n(&concurrent_release, __ATOMIC_ACQUIRE))
			sched_yield();
	}
	if (reenter_binding) {
		struct starbook_mtl_mor_clear_x86_binding *binding = reenter_binding;
		const struct payload_mm_authvar_mor_clear_plan *outer = reenter_plan;

		reenter_binding = NULL;
		reenter_plan = NULL;
		CHECK(binding->ops.inventory_validate(
			binding->ops.inventory_context, outer) != CB_SUCCESS);
	}
	CHECK(dma_guard->generation == 9 && overlay_count == 5);
	CHECK(overlays[0].base == page_result.base &&
		overlays[0].size == page_result.size);
	CHECK(overlays[1].base == aperture_result.base &&
		overlays[1].size == aperture_result.size);
	CHECK(overlays[2].size == sizeof(*plan) &&
		overlays[3].size == sizeof(struct starbook_mtl_mor_clear_x86_binding) &&
		overlays[4].base && overlays[4].size);
	for (size_t index = 0; index < overlay_count; index++)
		CHECK(overlays[index].exclusion_reason == reasons[index] &&
			!overlays[index].reserved);
	apply_mutation();
	memset(plan, 0, sizeof(*plan));
	plan->revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	plan->size = sizeof(*plan);
	plan->inventory_generation = dma_guard->generation;
	memcpy(plan->inventory_identity, dma_guard->identity,
		sizeof(plan->inventory_identity));
	plan->span_count = overlay_count;
	for (size_t index = 0; index < overlay_count; index++) {
		plan->spans[index].base = overlays[index].base;
		plan->spans[index].size = overlays[index].size;
		plan->spans[index].span_class =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED;
		plan->spans[index].exclusion_reason = reasons[index];
	}
	for (size_t index = 1; index < overlay_count; index++) {
		struct payload_mm_authvar_mor_grant_span selected = plan->spans[index];
		size_t position = index;

		while (position && plan->spans[position - 1].base > selected.base) {
			plan->spans[position] = plan->spans[position - 1];
			position--;
		}
		plan->spans[position] = selected;
	}
	memset(workspace, 0, sizeof(*workspace));
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_dma_guard_bind_owned(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	struct starbook_mtl_dma_guard_snapshot *bound,
	struct payload_mm_authvar_mor_clear_dma_snapshot *dma,
	struct starbook_mtl_dma_guard_bind_workspace *workspace)
{
	guard_calls++;
	CHECK(workspace != NULL);
	apply_mutation();
	if (guard_calls == fail_guard_call)
		return CB_ERR;
	CHECK(plan->inventory_generation == prepared->generation &&
		!memcmp(plan->inventory_identity, prepared->identity,
			sizeof(plan->inventory_identity)));
	*bound = *prepared;
	*dma = (struct payload_mm_authvar_mor_clear_dma_snapshot) {
		.generation = prepared->generation,
	};
	memcpy(dma->identity, prepared->identity, sizeof(dma->identity));
	memset(workspace, 0, sizeof(*workspace));
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_clear_x86_prepare(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	void *page_tables, void *aperture,
	struct payload_mm_authvar_mor_clear_x86_backend *backend,
	struct payload_mm_authvar_mor_clear_executor_ops *ops)
{
	static uint8_t executable_owner[1];
	static uint8_t stack_owner[1];

	backend_calls++;
	CHECK(plan->span_count == 5 &&
		(uintptr_t)page_tables == page_result.base &&
		(uintptr_t)aperture == aperture_result.base);
	memset(backend, 0, sizeof(*backend));
	backend->revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_REVISION;
	backend->size = sizeof(*backend);
	backend->page_tables = (uintptr_t)page_tables;
	backend->aperture = (uintptr_t)aperture;
	backend->prepared = true;
	memset(ops, 0, sizeof(*ops));
	ops->context = backend;
	ops->context_size = sizeof(*backend);
	ops->executable_owner = executable_owner;
	ops->executable_owner_size = sizeof(executable_owner);
	ops->stack_owner = stack_owner;
	ops->stack_owner_size = sizeof(stack_owner);
	return CB_SUCCESS;
}

static void reset(void)
{
	starbook_mtl_mor_clear_x86_lifecycle_reset_test();
	page_result = (struct bootmem_aligned_reservation) {
		.base = 0x200000,
		.size = PAE_PGTL_SIZE,
		.tag = BM_MEM_TABLE,
	};
	aperture_result = (struct bootmem_aligned_reservation) {
		.base = 0x800000,
		.size = PAE_VMEM_SIZE,
		.tag = BM_MEM_RESERVED,
	};
	register_calls = 0;
	query_calls = 0;
	compose_calls = 0;
	backend_calls = 0;
	guard_calls = 0;
	fail_register_call = 0;
	fail_query_call = 0;
	fail_guard_call = 0;
	mutate = NULL;
	mutate_second = NULL;
	reenter_binding = NULL;
	reenter_plan = NULL;
	__atomic_store_n(&concurrent_hold, 0U, __ATOMIC_RELEASE);
	__atomic_store_n(&concurrent_entered, 0U, __ATOMIC_RELEASE);
	__atomic_store_n(&concurrent_release, 0U, __ATOMIC_RELEASE);
}

static struct starbook_mtl_mor_clear_x86_reservations registered(void)
{
	struct starbook_mtl_mor_clear_x86_reservations reservations = { 0 };

	CHECK(starbook_mtl_mor_clear_x86_register(&reservations) == CB_SUCCESS);
	return reservations;
}

static struct starbook_mtl_dma_guard_snapshot guard(void)
{
	return (struct starbook_mtl_dma_guard_snapshot) {
		.revision = STARBOOK_MTL_DMA_GUARD_REVISION,
		.size = sizeof(struct starbook_mtl_dma_guard_snapshot),
		.generation = 9,
		.identity = { 0x99 },
	};
}

static void expect_zero(const void *object, size_t size)
{
	const uint8_t *bytes = object;

	for (size_t index = 0; index < size; index++)
		CHECK(!bytes[index]);
}

struct concurrent_prepare_call {
	struct starbook_mtl_mor_clear_x86_reservations reservations;
	struct starbook_mtl_dma_guard_snapshot snapshot;
	struct payload_mm_authvar_mor_clear_plan plan;
	struct starbook_mtl_mor_clear_x86_binding binding;
	enum cb_err result;
};

struct concurrent_inventory_call {
	struct starbook_mtl_mor_clear_x86_binding *binding;
	const struct payload_mm_authvar_mor_clear_plan *plan;
	enum cb_err result;
};

static void *concurrent_prepare(void *argument)
{
	struct concurrent_prepare_call *call = argument;

	call->result = starbook_mtl_mor_clear_x86_prepare(&call->reservations,
		&call->snapshot, false, &call->plan, &call->binding);
	return NULL;
}

static void *concurrent_inventory(void *argument)
{
	struct concurrent_inventory_call *call = argument;

	call->result = call->binding->ops.inventory_validate(
		call->binding->ops.inventory_context, call->plan);
	return NULL;
}

static void concurrent_workspace_contention(void)
{
	struct concurrent_prepare_call owner = {
		.reservations = registered(),
		.snapshot = guard(),
	};
	struct concurrent_prepare_call contender = {
		.reservations = owner.reservations,
		.snapshot = owner.snapshot,
	};
	pthread_t thread;

	__atomic_store_n(&concurrent_hold, 1U, __ATOMIC_RELEASE);
	CHECK(!pthread_create(&thread, NULL, concurrent_prepare, &owner));
	while (!__atomic_load_n(&concurrent_entered, __ATOMIC_ACQUIRE))
		sched_yield();
	CHECK(starbook_mtl_mor_clear_x86_prepare(&contender.reservations,
		&contender.snapshot, false, &contender.plan, &contender.binding) ==
		CB_ERR);
	expect_zero(&contender.plan, sizeof(contender.plan));
	expect_zero(&contender.binding, sizeof(contender.binding));
	__atomic_store_n(&concurrent_release, 1U, __ATOMIC_RELEASE);
	CHECK(!pthread_join(thread, NULL));
	CHECK(owner.result == CB_ERR);
	expect_zero(&owner.plan, sizeof(owner.plan));
	expect_zero(&owner.binding, sizeof(owner.binding));
	CHECK(starbook_mtl_mor_clear_x86_scratch_zero_test() &&
		!starbook_mtl_mor_clear_x86_scratch_idle_test());
}

static void concurrent_callback_rejects_contender(void)
{
	struct starbook_mtl_mor_clear_x86_reservations reservations = registered();
	struct starbook_mtl_dma_guard_snapshot snapshot = guard();
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct starbook_mtl_mor_clear_x86_binding binding = { 0 };
	struct payload_mm_authvar_mor_clear_dma_snapshot dma = { 0 };
	struct concurrent_inventory_call owner = {
		.binding = &binding,
		.plan = &plan,
	};
	pthread_t thread;

	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	__atomic_store_n(&concurrent_hold, 1U, __ATOMIC_RELEASE);
	CHECK(!pthread_create(&thread, NULL, concurrent_inventory, &owner));
	while (!__atomic_load_n(&concurrent_entered, __ATOMIC_ACQUIRE))
		sched_yield();
	CHECK(binding.ops.dma_snapshot(binding.ops.context, &dma) != CB_SUCCESS);
	__atomic_store_n(&concurrent_release, 1U, __ATOMIC_RELEASE);
	CHECK(!pthread_join(thread, NULL));
	CHECK(owner.result != CB_SUCCESS);
	CHECK(starbook_mtl_mor_clear_x86_scratch_zero_test() &&
		!starbook_mtl_mor_clear_x86_scratch_idle_test());
}

static void success_and_repeat(void)
{
	struct starbook_mtl_mor_clear_x86_reservations reservations = registered();
	struct starbook_mtl_dma_guard_snapshot snapshot = guard();
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct starbook_mtl_mor_clear_x86_binding binding = { 0 };

	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	CHECK(query_calls == 2 && compose_calls == 1 && backend_calls == 1 &&
		guard_calls == 1 && binding.backend.prepared &&
		binding.ops.context == &binding.backend &&
		binding.ops.context_size == sizeof(binding.backend) &&
		binding.ops.dma_snapshot &&
		binding.ops.inventory_context == &binding.backend &&
		binding.ops.inventory_context_size == sizeof(binding.backend) &&
		binding.ops.executable_owner && binding.ops.executable_owner_size &&
		binding.ops.stack_owner && binding.ops.stack_owner_size &&
		binding.ops.inventory_validate);
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) != CB_SUCCESS);
	expect_zero(&plan, sizeof(plan));
	expect_zero(&binding, sizeof(binding));
}

static void callback_sequence(void)
{
	struct starbook_mtl_mor_clear_x86_reservations reservations = registered();
	struct starbook_mtl_dma_guard_snapshot snapshot = guard();
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct starbook_mtl_mor_clear_x86_binding binding = { 0 };
	struct payload_mm_authvar_mor_clear_dma_snapshot dma = { 0 };

	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) == CB_SUCCESS && compose_calls == 2 && guard_calls == 2);
	CHECK(binding.ops.dma_snapshot(binding.ops.context, &dma) == CB_SUCCESS &&
		dma.generation == 9 && dma.identity[0] == 0x99 && guard_calls == 3);
	memset(&dma, 0, sizeof(dma));
	CHECK(binding.ops.dma_snapshot(binding.ops.context, &dma) == CB_SUCCESS &&
		dma.generation == 9 && dma.identity[0] == 0x99 && guard_calls == 4);
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) == CB_SUCCESS && compose_calls == 3 && guard_calls == 5);
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) != CB_SUCCESS);
}

static void callback_mutations_fail_closed(void)
{
	struct starbook_mtl_mor_clear_x86_reservations reservations = registered();
	struct starbook_mtl_dma_guard_snapshot snapshot = guard();
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct starbook_mtl_mor_clear_x86_binding binding = { 0 };
	struct payload_mm_authvar_mor_clear_dma_snapshot dma = { .generation = 1 };
	const struct payload_mm_authvar_mor_clear_dma_snapshot dma_original = dma;

	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	mutate = &binding.authority.seal;
	mutate_second = &binding.authority_mirror.seal;
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) != CB_SUCCESS);

	reset();
	reservations = registered();
	snapshot = guard();
	memset(&plan, 0, sizeof(plan));
	memset(&binding, 0, sizeof(binding));
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) == CB_SUCCESS);
	mutate = &binding.ops;
	CHECK(binding.ops.dma_snapshot(binding.ops.context, &dma) != CB_SUCCESS);
	CHECK(!memcmp(&dma, &dma_original, sizeof(dma)));

	reset();
	reservations = registered();
	snapshot = guard();
	memset(&plan, 0, sizeof(plan));
	memset(&binding, 0, sizeof(binding));
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) == CB_SUCCESS);
	fail_guard_call = guard_calls + 1U;
	CHECK(binding.ops.dma_snapshot(binding.ops.context, &dma) != CB_SUCCESS);
	CHECK(!memcmp(&dma, &dma_original, sizeof(dma)));

	reset();
	reservations = registered();
	snapshot = guard();
	memset(&plan, 0, sizeof(plan));
	memset(&binding, 0, sizeof(binding));
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	binding.authority_mirror.seal++;
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) != CB_SUCCESS);

	reset();
	reservations = registered();
	snapshot = guard();
	memset(&plan, 0, sizeof(plan));
	memset(&binding, 0, sizeof(binding));
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	reenter_binding = &binding;
	reenter_plan = &plan;
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) != CB_SUCCESS);

	reset();
	reservations = registered();
	snapshot = guard();
	memset(&plan, 0, sizeof(plan));
	memset(&binding, 0, sizeof(binding));
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) == CB_SUCCESS);
	CHECK(binding.ops.dma_snapshot(binding.ops.context,
		(struct payload_mm_authvar_mor_clear_dma_snapshot *)
		&binding.authority) != CB_SUCCESS);

	reset();
	reservations = registered();
	snapshot = guard();
	memset(&plan, 0, sizeof(plan));
	memset(&binding, 0, sizeof(binding));
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	plan.inventory_generation++;
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) != CB_SUCCESS);
}

static void callback_outputs_cannot_alias_protected_storage(void)
{
	struct starbook_mtl_mor_clear_x86_reservations reservations = registered();
	struct starbook_mtl_dma_guard_snapshot snapshot = guard();
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct starbook_mtl_mor_clear_x86_binding binding = { 0 };
	uint8_t owner_original;
	uintptr_t scratch_end;
	uintptr_t lifecycle_alias;

	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) == CB_SUCCESS);
	owner_original = *(const uint8_t *)binding.ops.executable_owner;
	CHECK(binding.ops.dma_snapshot(binding.ops.context,
		(struct payload_mm_authvar_mor_clear_dma_snapshot *)
		binding.ops.executable_owner) != CB_SUCCESS);
	CHECK(*(const uint8_t *)binding.ops.executable_owner == owner_original);

	reset();
	reservations = registered();
	snapshot = guard();
	memset(&plan, 0, sizeof(plan));
	memset(&binding, 0, sizeof(binding));
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) == CB_SUCCESS);
	scratch_end = binding.authority.overlays[4].base +
		binding.authority.overlays[4].size;
	lifecycle_alias = (scratch_end -
		sizeof(struct payload_mm_authvar_mor_clear_dma_snapshot)) &
		~(uintptr_t)(_Alignof(struct payload_mm_authvar_mor_clear_dma_snapshot) -
			1U);
	CHECK(binding.ops.dma_snapshot(binding.ops.context,
		(struct payload_mm_authvar_mor_clear_dma_snapshot *)lifecycle_alias) !=
		CB_SUCCESS);

	reset();
	reservations = registered();
	snapshot = guard();
	memset(&plan, 0, sizeof(plan));
	memset(&binding, 0, sizeof(binding));
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		(const struct payload_mm_authvar_mor_clear_plan *)
		binding.authority.overlays[4].base) != CB_SUCCESS);
}

static void restored_binding_cannot_replay(void)
{
	struct starbook_mtl_mor_clear_x86_reservations reservations = registered();
	struct starbook_mtl_dma_guard_snapshot snapshot = guard();
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct starbook_mtl_mor_clear_x86_binding binding = { 0 };
	struct starbook_mtl_mor_clear_x86_binding saved;
	struct payload_mm_authvar_mor_clear_dma_snapshot dma = { 0 };

	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	saved = binding;
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) == CB_SUCCESS);
	CHECK(binding.ops.dma_snapshot(binding.ops.context, &dma) == CB_SUCCESS);
	memset(&dma, 0, sizeof(dma));
	CHECK(binding.ops.dma_snapshot(binding.ops.context, &dma) == CB_SUCCESS);
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) == CB_SUCCESS);
	binding = saved;
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) != CB_SUCCESS);

	reset();
	reservations = registered();
	snapshot = guard();
	memset(&plan, 0, sizeof(plan));
	memset(&binding, 0, sizeof(binding));
	memset(&dma, 0, sizeof(dma));
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	saved = binding;
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) == CB_SUCCESS);
	fail_guard_call = guard_calls + 1U;
	CHECK(binding.ops.dma_snapshot(binding.ops.context, &dma) != CB_SUCCESS);
	binding = saved;
	CHECK(binding.ops.inventory_validate(binding.ops.inventory_context,
		&plan) != CB_SUCCESS);
}

static void hostile_callback_contexts(void)
{
	struct starbook_mtl_mor_clear_x86_reservations reservations = registered();
	struct starbook_mtl_dma_guard_snapshot snapshot = guard();
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct starbook_mtl_mor_clear_x86_binding binding = { 0 };
	struct payload_mm_authvar_mor_clear_dma_snapshot dma = { 0 };
	_Alignas(struct starbook_mtl_mor_clear_x86_binding)
		uint8_t truncated[sizeof(binding.backend)] = { 0 };
	enum cb_err (*dma_callback)(void *context,
		struct payload_mm_authvar_mor_clear_dma_snapshot *output);

	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	dma_callback = binding.ops.dma_snapshot;
	CHECK(dma_callback(truncated, &dma) != CB_SUCCESS);

	reset();
	reservations = registered();
	snapshot = guard();
	memset(&plan, 0, sizeof(plan));
	memset(&binding, 0, sizeof(binding));
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	CHECK(dma_callback((void *)(UINTPTR_MAX &
		~(uintptr_t)(_Alignof(binding) - 1U)), &dma) != CB_SUCCESS);
}

static void invalid_output_clears_valid_peer(void)
{
	struct starbook_mtl_mor_clear_x86_reservations reservations = registered();
	struct starbook_mtl_dma_guard_snapshot snapshot = guard();
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct starbook_mtl_mor_clear_x86_binding binding = { 0 };
	struct payload_mm_authvar_mor_clear_plan plan_original;
	struct starbook_mtl_mor_clear_x86_binding binding_original;

	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	plan_original = plan;
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, NULL) == CB_ERR_ARG);
	CHECK(!memcmp(&plan, &plan_original, sizeof(plan)));

	reset();
	reservations = registered();
	memset(&plan, 0, sizeof(plan));
	memset(&binding, 0, sizeof(binding));
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	binding_original = binding;
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		NULL, &binding) == CB_ERR_ARG);
	CHECK(!memcmp(&binding, &binding_original, sizeof(binding)));
}

static void registration_failures(void)
{
	struct starbook_mtl_mor_clear_x86_reservations reservations = { 0 };

	fail_register_call = 1;
	CHECK(starbook_mtl_mor_clear_x86_register(&reservations) != CB_SUCCESS);
	expect_zero(&reservations, sizeof(reservations));
	reset();
	reservations.registered = 1;
	CHECK(starbook_mtl_mor_clear_x86_register(&reservations) == CB_ERR_ARG &&
		!register_calls);
}

static void prepare_failure(
	void (*break_result)(struct starbook_mtl_mor_clear_x86_reservations *,
		struct starbook_mtl_dma_guard_snapshot *))
{
	struct starbook_mtl_mor_clear_x86_reservations reservations = registered();
	struct starbook_mtl_dma_guard_snapshot snapshot = guard();
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct starbook_mtl_mor_clear_x86_binding binding = { 0 };

	break_result(&reservations, &snapshot);
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) != CB_SUCCESS);
	expect_zero(&plan, sizeof(plan));
	expect_zero(&binding, sizeof(binding));
}

static void bad_page_size(struct starbook_mtl_mor_clear_x86_reservations *state,
	struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	(void)state;
	(void)snapshot;
	page_result.size -= 4096;
}

static void bad_page_alignment(
	struct starbook_mtl_mor_clear_x86_reservations *state,
	struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	(void)state;
	(void)snapshot;
	page_result.base++;
}

static void bad_aperture_type(
	struct starbook_mtl_mor_clear_x86_reservations *state,
	struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	(void)state;
	(void)snapshot;
	aperture_result.tag = BM_MEM_TABLE;
}

static void bad_limit(struct starbook_mtl_mor_clear_x86_reservations *state,
	struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	(void)state;
	(void)snapshot;
	aperture_result.base = 1ULL << 32;
}

static void overlap(struct starbook_mtl_mor_clear_x86_reservations *state,
	struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	(void)state;
	(void)snapshot;
	page_result.base = aperture_result.base;
}

static void mutated_state(struct starbook_mtl_mor_clear_x86_reservations *state,
	struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	(void)snapshot;
	mutate = state;
}

static void mutated_guard(struct starbook_mtl_mor_clear_x86_reservations *state,
	struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	(void)state;
	mutate = snapshot;
}

static void failures(void)
{
	void (*const cases[])(struct starbook_mtl_mor_clear_x86_reservations *,
		struct starbook_mtl_dma_guard_snapshot *) = {
		bad_page_size, bad_page_alignment, bad_aperture_type, bad_limit,
		overlap, mutated_state, mutated_guard,
	};

	for (size_t index = 0; index < ARRAY_SIZE(cases); index++) {
		reset();
		prepare_failure(cases[index]);
	}
	reset();
	struct starbook_mtl_mor_clear_x86_reservations reservations = registered();
	struct starbook_mtl_dma_guard_snapshot snapshot = guard();
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct starbook_mtl_mor_clear_x86_binding binding = { 0 };
	fail_query_call = 2;
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) != CB_SUCCESS && !compose_calls && !backend_calls);
	reset();
	reservations = registered();
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, true,
		&plan, &binding) != CB_SUCCESS && !query_calls && !compose_calls &&
		!backend_calls);
	reset();
	reservations = registered();
	snapshot = guard();
	fail_guard_call = 1;
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) != CB_SUCCESS);
	expect_zero(&plan, sizeof(plan));
	expect_zero(&binding, sizeof(binding));
}

static void aliases(void)
{
	union {
		struct starbook_mtl_mor_clear_x86_reservations reservations;
		struct payload_mm_authvar_mor_clear_plan plan;
	} shared = { 0 };
	struct starbook_mtl_dma_guard_snapshot snapshot = guard();
	struct starbook_mtl_mor_clear_x86_binding binding = { 0 };

	shared.reservations = registered();
	CHECK(starbook_mtl_mor_clear_x86_prepare(&shared.reservations, &snapshot,
		false, &shared.plan, &binding) != CB_SUCCESS);
	expect_zero(&binding, sizeof(binding));
}

int main(void)
{
	reset();
	concurrent_workspace_contention();
	reset();
	concurrent_callback_rejects_contender();
	reset();
	success_and_repeat();
	reset();
	callback_sequence();
	reset();
	callback_mutations_fail_closed();
	reset();
	callback_outputs_cannot_alias_protected_storage();
	reset();
	restored_binding_cannot_replay();
	reset();
	hostile_callback_contexts();
	reset();
	invalid_output_clears_valid_peer();
	reset();
	registration_failures();
	reset();
	failures();
	reset();
	aliases();
	return 0;
}
