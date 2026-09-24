/* SPDX-License-Identifier: GPL-2.0-only */

#include "../../src/mainboard/starlabs/starbook/variants/mtl/mor_clear_x86.h"

#include <boot/payload_mm_authvar_mor_live_inventory.h>
#include <commonlib/helpers.h>
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
static unsigned int fail_register_call;
static unsigned int fail_query_call;
static void *mutate;

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
	if (mutate) {
		((uint8_t *)mutate)[0] ^= 1;
		mutate = NULL;
	}
	return 0;
}

enum cb_err starbook_mtl_mor_live_inventory_compose_with_overlays(
	const struct starbook_mtl_dma_guard_snapshot *dma_guard,
	const struct payload_mm_authvar_mor_live_inventory_overlay *overlays,
	size_t overlay_count, struct payload_mm_authvar_mor_clear_plan *plan)
{
	static const uint32_t reasons[] = {
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};

	compose_calls++;
	CHECK(dma_guard->generation == 9 && overlay_count == 4);
	CHECK(overlays[0].base == page_result.base &&
		overlays[0].size == page_result.size);
	CHECK(overlays[1].base == aperture_result.base &&
		overlays[1].size == aperture_result.size);
	for (size_t index = 0; index < overlay_count; index++)
		CHECK(overlays[index].exclusion_reason == reasons[index] &&
			!overlays[index].reserved);
	if (mutate) {
		((uint8_t *)mutate)[0] ^= 1;
		mutate = NULL;
	}
	memset(plan, 0, sizeof(*plan));
	plan->revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	plan->size = sizeof(*plan);
	plan->inventory_generation = dma_guard->generation;
	plan->span_count = overlay_count;
	for (size_t index = 0; index < overlay_count; index++) {
		plan->spans[index].base = overlays[index].base;
		plan->spans[index].size = overlays[index].size;
		plan->spans[index].span_class =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED;
		plan->spans[index].exclusion_reason = reasons[index];
	}
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_clear_x86_prepare(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	void *page_tables, void *aperture,
	struct payload_mm_authvar_mor_clear_x86_backend *backend,
	struct payload_mm_authvar_mor_clear_executor_ops *ops)
{
	backend_calls++;
	CHECK(plan->span_count == 4 &&
		(uintptr_t)page_tables == page_result.base &&
		(uintptr_t)aperture == aperture_result.base);
	memset(backend, 0, sizeof(*backend));
	backend->revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_REVISION;
	backend->size = sizeof(*backend);
	backend->prepared = true;
	memset(ops, 0, sizeof(*ops));
	ops->context = backend;
	return CB_SUCCESS;
}

static void reset(void)
{
	page_result = (struct bootmem_aligned_reservation) {
		.base = 0x200000,
		.size = PAE_PGTL_SIZE,
		.tag = BM_MEM_TABLE,
	};
	aperture_result = (struct bootmem_aligned_reservation) {
		.base = 0x400000,
		.size = PAE_VMEM_SIZE,
		.tag = BM_MEM_RESERVED,
	};
	register_calls = 0;
	query_calls = 0;
	compose_calls = 0;
	backend_calls = 0;
	fail_register_call = 0;
	fail_query_call = 0;
	mutate = NULL;
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
	};
}

static void expect_zero(const void *object, size_t size)
{
	const uint8_t *bytes = object;

	for (size_t index = 0; index < size; index++)
		CHECK(!bytes[index]);
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
		binding.backend.prepared && binding.ops.context == &binding.backend);
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) != CB_SUCCESS);
	expect_zero(&plan, sizeof(plan));
	expect_zero(&binding, sizeof(binding));
}

static void invalid_output_clears_valid_peer(void)
{
	struct starbook_mtl_mor_clear_x86_reservations reservations = registered();
	struct starbook_mtl_dma_guard_snapshot snapshot = guard();
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct starbook_mtl_mor_clear_x86_binding binding = { 0 };

	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, NULL) == CB_ERR_ARG);
	expect_zero(&plan, sizeof(plan));

	reset();
	reservations = registered();
	memset(&plan, 0, sizeof(plan));
	memset(&binding, 0, sizeof(binding));
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		&plan, &binding) == CB_SUCCESS);
	CHECK(starbook_mtl_mor_clear_x86_prepare(&reservations, &snapshot, false,
		NULL, &binding) == CB_ERR_ARG);
	expect_zero(&binding, sizeof(binding));
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
	success_and_repeat();
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
