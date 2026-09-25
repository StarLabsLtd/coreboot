/* SPDX-License-Identifier: GPL-2.0-only */

#include "mor_live_inventory.h"

#include <boot/payload_mm_authvar_mor_live_inventory.h>
#include <commonlib/helpers.h>
#include <string.h>

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= (uintptr_t)-1 - (size - 1U);
}

static bool objects_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

#if ENV_TEST
enum cb_err starbook_mtl_mor_live_inventory_compose_with_overlays(
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	const struct payload_mm_authvar_mor_live_inventory_overlay *overlays,
	size_t overlay_count, struct payload_mm_authvar_mor_clear_plan *plan)
{
	struct starbook_mtl_mor_live_inventory_workspace workspace;

	return starbook_mtl_mor_live_inventory_compose_with_overlays_owned(prepared,
		overlays, overlay_count, plan, &workspace);
}
#endif

enum cb_err starbook_mtl_mor_live_inventory_compose_with_overlays_owned(
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	const struct payload_mm_authvar_mor_live_inventory_overlay *overlays,
	size_t overlay_count, struct payload_mm_authvar_mor_clear_plan *plan,
	struct starbook_mtl_mor_live_inventory_workspace *workspace)
{
	struct payload_mm_authvar_mor_live_inventory_request *request;

	if (!object_valid(workspace, sizeof(*workspace), _Alignof(*workspace)) ||
	    !object_valid(plan, sizeof(*plan), _Alignof(*plan)) ||
	    !object_valid(prepared, sizeof(*prepared), _Alignof(*prepared)) ||
	    overlay_count > ARRAY_SIZE(workspace->overlay_snapshot) ||
	    (overlay_count && !object_valid(overlays,
		overlay_count * sizeof(*overlays), _Alignof(*overlays))) ||
	    objects_overlap(workspace, sizeof(*workspace), plan, sizeof(*plan)) ||
	    objects_overlap(workspace, sizeof(*workspace), prepared,
		sizeof(*prepared)) ||
	    objects_overlap(prepared, sizeof(*prepared), plan, sizeof(*plan)) ||
	    (overlay_count &&
	     (objects_overlap(workspace, sizeof(*workspace), overlays,
		overlay_count * sizeof(*overlays)) ||
	      objects_overlap(overlays, overlay_count * sizeof(*overlays), plan,
		sizeof(*plan)) ||
	      objects_overlap(overlays, overlay_count * sizeof(*overlays), prepared,
		sizeof(*prepared)))))
		return CB_ERR_ARG;
	memset(workspace, 0, sizeof(*workspace));
	request = &workspace->request;
	*request = (struct payload_mm_authvar_mor_live_inventory_request) {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_REVISION,
		.size = sizeof(*request),
		.overlay_count = 2U + STARBOOK_MTL_DMA_GUARD_ARENAS + overlay_count,
	};

	memset(plan, 0, sizeof(*plan));
	memcpy(&workspace->snapshot, prepared, sizeof(workspace->snapshot));
	if (overlay_count)
		memcpy(workspace->overlay_snapshot, overlays,
			overlay_count * sizeof(*overlays));
	request->generation = workspace->snapshot.generation;
	memcpy(request->identity, workspace->snapshot.identity,
		sizeof(request->identity));
	request->overlays[0] = (struct payload_mm_authvar_mor_live_inventory_overlay) {
		.base = workspace->snapshot.handoff.base,
		/* The validated handoff and table are adjacent and share policy. */
		.size = workspace->snapshot.handoff.size +
			workspace->snapshot.table.size,
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	request->overlays[1] = (struct payload_mm_authvar_mor_live_inventory_overlay) {
		.base = workspace->snapshot.table_mirror.base,
		.size = workspace->snapshot.table_mirror.size,
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	for (size_t index = 0; index < STARBOOK_MTL_DMA_GUARD_ARENAS; index++)
		request->overlays[2U + index] =
			(struct payload_mm_authvar_mor_live_inventory_overlay) {
				.base = workspace->snapshot.arenas[index].base,
				.size = workspace->snapshot.arenas[index].size,
				.exclusion_reason =
					PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED,
			};
	for (size_t index = 0; index < overlay_count; index++)
		request->overlays[2U + STARBOOK_MTL_DMA_GUARD_ARENAS + index] =
			workspace->overlay_snapshot[index];
	if (payload_mm_authvar_mor_live_inventory_compose_owned(request,
		&workspace->candidate, &workspace->inventory) != CB_SUCCESS ||
	    starbook_mtl_dma_guard_policy_validate_owned(&workspace->candidate,
		&workspace->snapshot, &workspace->policy) !=
		CB_SUCCESS || memcmp(&workspace->snapshot, prepared,
			sizeof(workspace->snapshot)) ||
	    (overlay_count && memcmp(workspace->overlay_snapshot, overlays,
		overlay_count * sizeof(*overlays))) ||
	    memcmp(plan, &(const struct payload_mm_authvar_mor_clear_plan) { 0 },
		sizeof(*plan)))
		goto fail;
	*plan = workspace->candidate;
	return CB_SUCCESS;

fail:
	memset(plan, 0, sizeof(*plan));
	return CB_ERR;
}

#if ENV_TEST
enum cb_err starbook_mtl_mor_live_inventory_compose(
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	struct payload_mm_authvar_mor_clear_plan *plan)
{
	return starbook_mtl_mor_live_inventory_compose_with_overlays(prepared,
		NULL, 0, plan);
}

enum cb_err starbook_mtl_mor_live_inventory_validate(void *context,
	const struct payload_mm_authvar_mor_clear_plan *plan)
{
	const struct starbook_mtl_dma_guard_snapshot *prepared = context;
	struct payload_mm_authvar_mor_clear_plan expected;
	struct payload_mm_authvar_mor_clear_plan snapshot;

	if (!plan || (uintptr_t)plan % _Alignof(*plan) ||
	    (uintptr_t)plan > (uintptr_t)-1 - (sizeof(*plan) - 1U))
		return CB_ERR_ARG;
	memcpy(&snapshot, plan, sizeof(snapshot));
	if (starbook_mtl_mor_live_inventory_compose(prepared, &expected) !=
		CB_SUCCESS || memcmp(&snapshot, &expected, sizeof(snapshot)) ||
	    memcmp(&snapshot, plan, sizeof(snapshot)))
		return CB_ERR;
	return CB_SUCCESS;
}
#endif
