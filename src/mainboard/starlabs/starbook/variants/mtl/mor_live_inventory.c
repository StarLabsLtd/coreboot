/* SPDX-License-Identifier: GPL-2.0-only */

#include "mor_live_inventory.h"

#include <boot/payload_mm_authvar_mor_live_inventory.h>
#include <string.h>

enum cb_err starbook_mtl_mor_live_inventory_compose(
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	struct payload_mm_authvar_mor_clear_plan *plan)
{
	struct starbook_mtl_dma_guard_snapshot snapshot;
	struct payload_mm_authvar_mor_live_inventory_request request = {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_REVISION,
		.size = sizeof(request),
		.overlay_count = 3U + STARBOOK_MTL_DMA_GUARD_ARENAS,
	};
	struct payload_mm_authvar_mor_clear_plan candidate;

	if (!plan || (uintptr_t)plan % _Alignof(*plan) ||
	    (uintptr_t)plan > (uintptr_t)-1 - (sizeof(*plan) - 1U))
		return CB_ERR_ARG;
	memset(plan, 0, sizeof(*plan));
	if (!prepared || (uintptr_t)prepared % _Alignof(*prepared) ||
	    (uintptr_t)prepared > (uintptr_t)-1 - (sizeof(*prepared) - 1U) ||
	    ((uintptr_t)prepared <= (uintptr_t)plan + sizeof(*plan) - 1U &&
	     (uintptr_t)plan <= (uintptr_t)prepared + sizeof(*prepared) - 1U))
		return CB_ERR_ARG;
	memcpy(&snapshot, prepared, sizeof(snapshot));
	request.generation = snapshot.generation;
	memcpy(request.identity, snapshot.identity, sizeof(request.identity));
	request.overlays[0] = (struct payload_mm_authvar_mor_live_inventory_overlay) {
		.base = snapshot.handoff.base,
		.size = snapshot.handoff.size,
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	request.overlays[1] = (struct payload_mm_authvar_mor_live_inventory_overlay) {
		.base = snapshot.table.base,
		.size = snapshot.table.size,
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	request.overlays[2] = (struct payload_mm_authvar_mor_live_inventory_overlay) {
		.base = snapshot.table_mirror.base,
		.size = snapshot.table_mirror.size,
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	for (size_t index = 0; index < STARBOOK_MTL_DMA_GUARD_ARENAS; index++)
		request.overlays[3U + index] =
			(struct payload_mm_authvar_mor_live_inventory_overlay) {
				.base = snapshot.arenas[index].base,
				.size = snapshot.arenas[index].size,
				.exclusion_reason =
					PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED,
			};
	if (payload_mm_authvar_mor_live_inventory_compose(&request, &candidate) !=
		CB_SUCCESS ||
	    starbook_mtl_dma_guard_policy_validate(&candidate, &snapshot) !=
		CB_SUCCESS || memcmp(&snapshot, prepared, sizeof(snapshot)) ||
	    memcmp(plan, &(const struct payload_mm_authvar_mor_clear_plan) { 0 },
		sizeof(*plan)))
		goto fail;
	*plan = candidate;
	return CB_SUCCESS;

fail:
	memset(plan, 0, sizeof(*plan));
	return CB_ERR;
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
