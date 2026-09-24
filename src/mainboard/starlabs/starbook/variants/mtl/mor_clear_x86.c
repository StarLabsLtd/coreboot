/* SPDX-License-Identifier: GPL-2.0-only */

#include "mor_clear_x86.h"

#include "mor_live_inventory.h"

#include <commonlib/helpers.h>
#include <string.h>

#define MTL_MOR_CLEAR_LIMIT_EXCLUSIVE (1ULL << 32)

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
	memset(plan, 0, sizeof(*plan));
	memset(binding, 0, sizeof(*binding));
	return CB_ERR;
}

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
	struct payload_mm_authvar_mor_live_inventory_overlay overlays[4];
	struct payload_mm_authvar_mor_clear_plan candidate;
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
		.base = (uintptr_t)&binding->backend,
		.size = sizeof(binding->backend),
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	overlays[3] = (struct payload_mm_authvar_mor_live_inventory_overlay) {
		.base = (uintptr_t)&binding->ops,
		.size = sizeof(binding->ops),
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
	*plan = candidate;
	return CB_SUCCESS;
}
