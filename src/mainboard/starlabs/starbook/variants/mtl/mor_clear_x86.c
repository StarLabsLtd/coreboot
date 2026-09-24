/* SPDX-License-Identifier: GPL-2.0-only */

#include "mor_clear_x86.h"

#include "mor_live_inventory.h"

#include <commonlib/helpers.h>
#include <string.h>

#define MTL_MOR_CLEAR_LIMIT_EXCLUSIVE (1ULL << 32)
#define MTL_MOR_CLEAR_AUTHORITY_DOMAIN 0x6d746c2d6d6f7265ULL

enum mtl_mor_clear_phase {
	MTL_MOR_CLEAR_EMPTY,
	MTL_MOR_CLEAR_BOUND,
	MTL_MOR_CLEAR_INVENTORY_BEFORE,
	MTL_MOR_CLEAR_DMA_BEFORE,
	MTL_MOR_CLEAR_DMA_AFTER,
	MTL_MOR_CLEAR_COMPLETE,
	MTL_MOR_CLEAR_BUSY,
	MTL_MOR_CLEAR_POISONED,
};

static struct {
	uintptr_t owner;
	uint64_t generation;
	enum mtl_mor_clear_phase phase;
} executor_lifecycle;

static enum cb_err executor_dma_snapshot(void *context,
	struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot);
static enum cb_err executor_inventory_validate(void *context,
	const struct payload_mm_authvar_mor_clear_plan *plan);

static const struct bootmem_aligned_reservation_request requests[] = {
	{
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
		.size = sizeof(struct bootmem_aligned_reservation_request),
		.bytes = PAE_PGTL_SIZE,
		.alignment = PAE_PGTL_ALIGN,
		.limit_exclusive = MTL_MOR_CLEAR_LIMIT_EXCLUSIVE,
		.tag = BM_MEM_TABLE,
	},
	{
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
		.size = sizeof(struct bootmem_aligned_reservation_request),
		.bytes = PAE_VMEM_SIZE,
		.alignment = PAE_VMEM_ALIGN,
		.limit_exclusive = MTL_MOR_CLEAR_LIMIT_EXCLUSIVE,
		.tag = BM_MEM_RESERVED,
	},
};

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return !combined;
}

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(uint64_t first_base, uint64_t first_size,
	uint64_t second_base, uint64_t second_size)
{
	if (!first_size || !second_size || first_base > UINT64_MAX - first_size ||
	    second_base > UINT64_MAX - second_size)
		return true;
	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

static uint64_t mix_bytes(const void *buffer, size_t size, uint64_t value)
{
	const uint8_t *bytes = buffer;

	for (size_t index = 0; index < size; index++) {
		value ^= bytes[index];
		value *= 0x100000001b3ULL;
		value ^= value >> 32;
	}
	return value;
}

static uint64_t authority_seal(
	const struct starbook_mtl_mor_clear_x86_authority *authority)
{
	return mix_bytes(authority,
		offsetof(struct starbook_mtl_mor_clear_x86_authority, seal),
		MTL_MOR_CLEAR_AUTHORITY_DOMAIN);
}

static struct starbook_mtl_mor_clear_x86_binding *binding_from_context(
	void *context)
{
	if (!object_valid(context,
		sizeof(struct starbook_mtl_mor_clear_x86_binding),
		_Alignof(struct starbook_mtl_mor_clear_x86_binding)) ||
	    (uintptr_t)context != executor_lifecycle.owner)
		return NULL;
	return context;
}

static bool lifecycle_claim(struct starbook_mtl_mor_clear_x86_binding *binding,
	uint64_t generation)
{
	if (executor_lifecycle.phase != MTL_MOR_CLEAR_EMPTY || !generation)
		return false;
	executor_lifecycle.owner = (uintptr_t)binding;
	executor_lifecycle.generation = generation;
	executor_lifecycle.phase = MTL_MOR_CLEAR_BOUND;
	return true;
}

static bool lifecycle_advance(
	struct starbook_mtl_mor_clear_x86_binding *binding,
	uint64_t generation, enum mtl_mor_clear_phase expected,
	enum mtl_mor_clear_phase next)
{
	if (executor_lifecycle.owner != (uintptr_t)binding ||
	    executor_lifecycle.generation != generation ||
	    executor_lifecycle.phase != expected || binding->phase != expected)
		return false;
	executor_lifecycle.phase = next;
	binding->phase = MTL_MOR_CLEAR_BUSY;
	return true;
}

static bool authority_fields_valid(
	const struct starbook_mtl_mor_clear_x86_binding *binding,
	const struct starbook_mtl_mor_clear_x86_authority *authority)
{
	const struct payload_mm_authvar_mor_clear_executor_ops *ops = &binding->ops;

	return authority->revision == STARBOOK_MTL_MOR_CLEAR_X86_REVISION &&
		authority->size == sizeof(*authority) &&
		authority->owner == (uintptr_t)binding &&
		object_valid((const void *)authority->plan_address,
			sizeof(authority->plan), _Alignof(authority->plan)) &&
		!ranges_overlap(authority->plan_address, sizeof(authority->plan),
			(uintptr_t)binding, sizeof(*binding)) &&
		authority->page_tables == binding->backend.page_tables &&
		authority->aperture == binding->backend.aperture &&
		authority->prepared.generation &&
		authority->prepared.generation == authority->bound.generation &&
		authority->prepared.generation == authority->dma.generation &&
		authority->prepared.generation ==
			authority->plan.inventory_generation &&
		!memcmp(authority->prepared.identity, authority->bound.identity,
			sizeof(authority->prepared.identity)) &&
		!memcmp(authority->prepared.identity, authority->dma.identity,
			sizeof(authority->prepared.identity)) &&
		!memcmp(authority->prepared.identity,
			authority->plan.inventory_identity,
			sizeof(authority->prepared.identity)) &&
		payload_mm_authvar_mor_clear_plan_validate(&authority->plan) ==
			CB_SUCCESS &&
		!memcmp(&authority->backend, &binding->backend,
			sizeof(authority->backend)) &&
		!memcmp(&authority->ops, ops, sizeof(authority->ops)) &&
		ops->context == &binding->backend &&
		ops->inventory_context == &binding->backend &&
		ops->dma_snapshot == executor_dma_snapshot &&
		ops->inventory_validate == executor_inventory_validate &&
		authority->overlays[0].base == authority->page_tables &&
		authority->overlays[0].size == PAE_PGTL_SIZE &&
		authority->overlays[0].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE &&
		!authority->overlays[0].reserved &&
		authority->overlays[1].base == authority->aperture &&
		authority->overlays[1].size == PAE_VMEM_SIZE &&
		authority->overlays[1].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED &&
		!authority->overlays[1].reserved &&
		authority->overlays[2].base == authority->plan_address &&
		authority->overlays[2].size == sizeof(authority->plan) &&
		authority->overlays[2].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE &&
		!authority->overlays[2].reserved &&
		authority->overlays[3].base == (uintptr_t)binding &&
		authority->overlays[3].size == sizeof(*binding) &&
		authority->overlays[3].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE &&
		!authority->overlays[3].reserved;
}

static bool authority_capture(
	struct starbook_mtl_mor_clear_x86_binding *binding,
	enum mtl_mor_clear_phase phase,
	struct starbook_mtl_mor_clear_x86_authority *authority)
{
	if (!object_valid(binding, sizeof(*binding), _Alignof(*binding)) ||
	    binding->phase != phase || binding->reserved ||
	    memcmp(&binding->authority, &binding->authority_mirror,
		sizeof(binding->authority)))
		return false;
	memcpy(authority, &binding->authority, sizeof(*authority));
	return authority->seal == authority_seal(authority) &&
		authority_fields_valid(binding, authority) &&
		!memcmp(authority, &binding->authority, sizeof(*authority)) &&
		!memcmp(authority, &binding->authority_mirror,
			sizeof(*authority)) && binding->phase == phase &&
		!binding->reserved;
}

static enum cb_err poison_binding(
	struct starbook_mtl_mor_clear_x86_binding *binding)
{
	executor_lifecycle.phase = MTL_MOR_CLEAR_POISONED;
	if (binding && executor_lifecycle.owner == (uintptr_t)binding)
		binding->phase = MTL_MOR_CLEAR_POISONED;
	return CB_ERR;
}

static bool guard_revalidate(
	struct starbook_mtl_mor_clear_x86_binding *binding,
	enum mtl_mor_clear_phase phase,
	const struct starbook_mtl_mor_clear_x86_authority *authority)
{
	struct starbook_mtl_dma_guard_snapshot bound = { 0 };
	struct payload_mm_authvar_mor_clear_dma_snapshot dma = { 0 };
	struct starbook_mtl_mor_clear_x86_authority recheck;

	if (starbook_mtl_dma_guard_bind(&authority->plan, &authority->prepared,
		&bound, &dma) != CB_SUCCESS ||
	    !authority_capture(binding, phase, &recheck) ||
	    memcmp(authority, &recheck, sizeof(recheck)) ||
	    memcmp(&bound, &authority->bound, sizeof(bound)) ||
	    memcmp(&dma, &authority->dma, sizeof(dma)))
		return false;
	return true;
}

static enum cb_err executor_dma_snapshot(void *context,
	struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot)
{
	struct starbook_mtl_mor_clear_x86_binding *binding =
		binding_from_context(context);
	struct starbook_mtl_mor_clear_x86_authority authority;
	enum mtl_mor_clear_phase next;

	if (!object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)))
		return poison_binding(binding);
	if (!binding || ranges_overlap((uintptr_t)snapshot, sizeof(*snapshot),
		(uintptr_t)binding, sizeof(*binding)))
		return poison_binding(binding);
	if (binding->phase == MTL_MOR_CLEAR_INVENTORY_BEFORE)
		next = MTL_MOR_CLEAR_DMA_BEFORE;
	else if (binding->phase == MTL_MOR_CLEAR_DMA_BEFORE)
		next = MTL_MOR_CLEAR_DMA_AFTER;
	else
		return poison_binding(binding);
	if (!authority_capture(binding, binding->phase, &authority) ||
	    ranges_overlap((uintptr_t)snapshot, sizeof(*snapshot),
		authority.plan_address, sizeof(authority.plan)))
		return poison_binding(binding);
	memset(snapshot, 0, sizeof(*snapshot));
	if (!lifecycle_advance(binding, authority.prepared.generation,
		binding->phase, next))
		return poison_binding(binding);
	if (!guard_revalidate(binding, MTL_MOR_CLEAR_BUSY, &authority))
		return poison_binding(binding);
	*snapshot = authority.dma;
	binding->phase = next;
	return CB_SUCCESS;
}

static enum cb_err executor_inventory_validate(void *context,
	const struct payload_mm_authvar_mor_clear_plan *plan)
{
	struct starbook_mtl_mor_clear_x86_binding *binding =
		binding_from_context(context);
	struct starbook_mtl_mor_clear_x86_authority authority;
	struct starbook_mtl_mor_clear_x86_authority recheck;
	struct payload_mm_authvar_mor_clear_plan expected = { 0 };
	enum mtl_mor_clear_phase phase;
	enum mtl_mor_clear_phase next;

	if (!binding)
		return poison_binding(NULL);
	phase = binding->phase;
	if (phase == MTL_MOR_CLEAR_BOUND)
		next = MTL_MOR_CLEAR_INVENTORY_BEFORE;
	else if (phase == MTL_MOR_CLEAR_DMA_AFTER)
		next = MTL_MOR_CLEAR_COMPLETE;
	else
		return poison_binding(binding);
	if (!object_valid(plan, sizeof(*plan), _Alignof(*plan)) ||
	    !authority_capture(binding, phase, &authority) ||
	    (uintptr_t)plan != authority.plan_address ||
	    memcmp(plan, &authority.plan, sizeof(*plan)))
		return poison_binding(binding);
	if (!lifecycle_advance(binding, authority.prepared.generation, phase, next))
		return poison_binding(binding);
	if (starbook_mtl_mor_live_inventory_compose_with_overlays(
		&authority.prepared, authority.overlays,
		ARRAY_SIZE(authority.overlays), &expected) != CB_SUCCESS ||
	    !authority_capture(binding, MTL_MOR_CLEAR_BUSY, &recheck) ||
	    memcmp(&authority, &recheck, sizeof(authority)) ||
	    memcmp(&expected, &authority.plan, sizeof(expected)) ||
	    memcmp(plan, &authority.plan, sizeof(*plan)) ||
	    !guard_revalidate(binding, MTL_MOR_CLEAR_BUSY, &authority))
		return poison_binding(binding);
	binding->phase = next;
	return CB_SUCCESS;
}

static bool reservation_valid(
	const struct bootmem_aligned_reservation *reservation,
	const struct bootmem_aligned_reservation_request *request)
{
	return reservation->size == request->bytes &&
		reservation->tag == request->tag && !reservation->reserved &&
		!(reservation->base % request->alignment) &&
		reservation->base <= request->limit_exclusive - request->bytes;
}

static bool reservations_valid(
	const struct starbook_mtl_mor_clear_x86_reservations *reservations)
{
	return reservations->revision == STARBOOK_MTL_MOR_CLEAR_X86_REVISION &&
		reservations->size == sizeof(*reservations) &&
		reservations->registered == 1U && !reservations->reserved;
}

enum cb_err starbook_mtl_mor_clear_x86_register(
	struct starbook_mtl_mor_clear_x86_reservations *reservations)
{
	struct starbook_mtl_mor_clear_x86_reservations candidate = {
		.revision = STARBOOK_MTL_MOR_CLEAR_X86_REVISION,
		.size = sizeof(candidate),
	};

	if (!object_valid(reservations, sizeof(*reservations),
		_Alignof(*reservations)) ||
	    !bytes_zero(reservations, sizeof(*reservations)))
		return CB_ERR_ARG;
	if (bootmem_aligned_reservations_register(requests, ARRAY_SIZE(requests),
		candidate.handles))
		return CB_ERR;
	candidate.registered = 1U;
	*reservations = candidate;
	return CB_SUCCESS;
}

static enum cb_err fail(struct payload_mm_authvar_mor_clear_plan *plan,
	struct starbook_mtl_mor_clear_x86_binding *binding)
{
	if (executor_lifecycle.owner == (uintptr_t)binding)
		(void)poison_binding(binding);
	memset(plan, 0, sizeof(*plan));
	memset(binding, 0, sizeof(*binding));
	return CB_ERR;
}

#if ENV_TEST
void starbook_mtl_mor_clear_x86_lifecycle_reset_test(void)
{
	memset(&executor_lifecycle, 0, sizeof(executor_lifecycle));
}
#endif

enum cb_err starbook_mtl_mor_clear_x86_prepare(
	const struct starbook_mtl_mor_clear_x86_reservations *reservations,
	const struct starbook_mtl_dma_guard_snapshot *dma_guard,
	bool resume_from_s3, struct payload_mm_authvar_mor_clear_plan *plan,
	struct starbook_mtl_mor_clear_x86_binding *binding)
{
	struct starbook_mtl_mor_clear_x86_reservations state;
	struct starbook_mtl_dma_guard_snapshot guard_snapshot;
	struct bootmem_aligned_reservation page_tables;
	struct bootmem_aligned_reservation aperture;
	struct payload_mm_authvar_mor_live_inventory_overlay overlays[
		STARBOOK_MTL_MOR_CLEAR_X86_OVERLAYS];
	struct payload_mm_authvar_mor_clear_plan candidate;
	struct starbook_mtl_dma_guard_snapshot bound;
	struct payload_mm_authvar_mor_clear_dma_snapshot dma;
	struct starbook_mtl_mor_clear_x86_authority authority;
	const bool plan_valid = object_valid(plan, sizeof(*plan), _Alignof(*plan));
	const bool binding_valid = object_valid(binding, sizeof(*binding),
		_Alignof(*binding));

	if (!plan_valid || !binding_valid) {
		if (plan_valid)
			memset(plan, 0, sizeof(*plan));
		if (binding_valid)
			memset(binding, 0, sizeof(*binding));
		return CB_ERR_ARG;
	}
	if (!bytes_zero(plan, sizeof(*plan)) ||
	    !bytes_zero(binding, sizeof(*binding)))
		return fail(plan, binding);
	if (!object_valid(reservations, sizeof(*reservations),
		_Alignof(*reservations)) ||
	    !object_valid(dma_guard, sizeof(*dma_guard), _Alignof(*dma_guard)) ||
	    resume_from_s3)
		return fail(plan, binding);
	if (ranges_overlap((uintptr_t)plan, sizeof(*plan), (uintptr_t)binding,
		sizeof(*binding)) ||
	    ranges_overlap((uintptr_t)plan, sizeof(*plan), (uintptr_t)reservations,
		sizeof(*reservations)) ||
	    ranges_overlap((uintptr_t)plan, sizeof(*plan), (uintptr_t)dma_guard,
		sizeof(*dma_guard)) ||
	    ranges_overlap((uintptr_t)binding, sizeof(*binding),
		(uintptr_t)reservations, sizeof(*reservations)) ||
	    ranges_overlap((uintptr_t)binding, sizeof(*binding),
		(uintptr_t)dma_guard, sizeof(*dma_guard)) ||
	    ranges_overlap((uintptr_t)reservations, sizeof(*reservations),
		(uintptr_t)dma_guard, sizeof(*dma_guard)))
		return fail(plan, binding);
	memcpy(&state, reservations, sizeof(state));
	memcpy(&guard_snapshot, dma_guard, sizeof(guard_snapshot));
	if (!reservations_valid(&state) ||
	    bootmem_aligned_reservation_query(&state.handles[0], &page_tables) ||
	    bootmem_aligned_reservation_query(&state.handles[1], &aperture) ||
	    !reservation_valid(&page_tables, &requests[0]) ||
	    !reservation_valid(&aperture, &requests[1]) ||
	    ranges_overlap(page_tables.base, page_tables.size, aperture.base,
		aperture.size) ||
	    ranges_overlap(page_tables.base, page_tables.size, (uintptr_t)plan,
		sizeof(*plan)) ||
	    ranges_overlap(page_tables.base, page_tables.size, (uintptr_t)binding,
		sizeof(*binding)) ||
	    ranges_overlap(page_tables.base, page_tables.size,
		(uintptr_t)reservations, sizeof(*reservations)) ||
	    ranges_overlap(page_tables.base, page_tables.size, (uintptr_t)dma_guard,
		sizeof(*dma_guard)) ||
	    ranges_overlap(aperture.base, aperture.size, (uintptr_t)plan,
		sizeof(*plan)) ||
	    ranges_overlap(aperture.base, aperture.size, (uintptr_t)binding,
		sizeof(*binding)) ||
	    ranges_overlap(aperture.base, aperture.size, (uintptr_t)reservations,
		sizeof(*reservations)) ||
	    ranges_overlap(aperture.base, aperture.size, (uintptr_t)dma_guard,
		sizeof(*dma_guard)) ||
	    memcmp(&state, reservations, sizeof(state)))
		return fail(plan, binding);
	overlays[0] = (struct payload_mm_authvar_mor_live_inventory_overlay) {
		.base = page_tables.base,
		.size = page_tables.size,
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	overlays[1] = (struct payload_mm_authvar_mor_live_inventory_overlay) {
		.base = aperture.base,
		.size = aperture.size,
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED,
	};
	overlays[2] = (struct payload_mm_authvar_mor_live_inventory_overlay) {
		.base = (uintptr_t)plan,
		.size = sizeof(*plan),
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	overlays[3] = (struct payload_mm_authvar_mor_live_inventory_overlay) {
		.base = (uintptr_t)binding,
		.size = sizeof(*binding),
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	memset(&candidate, 0, sizeof(candidate));
	if (starbook_mtl_mor_live_inventory_compose_with_overlays(&guard_snapshot,
		overlays, ARRAY_SIZE(overlays), &candidate) != CB_SUCCESS ||
	    memcmp(&state, reservations, sizeof(state)) ||
	    memcmp(&guard_snapshot, dma_guard, sizeof(guard_snapshot)) ||
	    payload_mm_authvar_mor_clear_x86_prepare(&candidate,
		(void *)(uintptr_t)page_tables.base,
		(void *)(uintptr_t)aperture.base, &binding->backend,
		&binding->ops) != CB_SUCCESS ||
	    memcmp(&state, reservations, sizeof(state)) ||
	    memcmp(&guard_snapshot, dma_guard, sizeof(guard_snapshot)))
		return fail(plan, binding);
	memset(&bound, 0, sizeof(bound));
	memset(&dma, 0, sizeof(dma));
	if (!lifecycle_claim(binding, guard_snapshot.generation)) {
		(void)poison_binding(NULL);
		return fail(plan, binding);
	}
	if (starbook_mtl_dma_guard_bind(&candidate, &guard_snapshot, &bound,
		&dma) != CB_SUCCESS ||
	    memcmp(&state, reservations, sizeof(state)) ||
	    memcmp(&guard_snapshot, dma_guard, sizeof(guard_snapshot)) ||
	    !bytes_zero(plan, sizeof(*plan)))
		return fail(plan, binding);
	binding->ops.dma_snapshot = executor_dma_snapshot;
	binding->ops.inventory_context = &binding->backend;
	binding->ops.inventory_validate = executor_inventory_validate;
	binding->authority = (struct starbook_mtl_mor_clear_x86_authority) {
		.revision = STARBOOK_MTL_MOR_CLEAR_X86_REVISION,
		.size = sizeof(binding->authority),
		.owner = (uintptr_t)binding,
		.plan_address = (uintptr_t)plan,
		.page_tables = page_tables.base,
		.aperture = aperture.base,
		.prepared = guard_snapshot,
		.bound = bound,
		.dma = dma,
		.plan = candidate,
		.backend = binding->backend,
		.ops = binding->ops,
	};
	memcpy(binding->authority.overlays, overlays, sizeof(overlays));
	binding->authority.seal = authority_seal(&binding->authority);
	binding->authority_mirror = binding->authority;
	binding->phase = MTL_MOR_CLEAR_BOUND;
	if (!authority_capture(binding, MTL_MOR_CLEAR_BOUND, &authority) ||
	    memcmp(&state, reservations, sizeof(state)) ||
	    memcmp(&guard_snapshot, dma_guard, sizeof(guard_snapshot)) ||
	    !bytes_zero(plan, sizeof(*plan)))
		return fail(plan, binding);
	*plan = candidate;
	return CB_SUCCESS;
}
