/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_MOR_LIVE_INVENTORY_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_MOR_LIVE_INVENTORY_H

#include "dma_guard.h"

#include <boot/payload_mm_authvar_mor_live_inventory.h>

struct starbook_mtl_mor_live_inventory_workspace {
	struct starbook_mtl_dma_guard_snapshot snapshot;
	struct payload_mm_authvar_mor_live_inventory_overlay overlay_snapshot[5];
	struct payload_mm_authvar_mor_live_inventory_request request;
	struct payload_mm_authvar_mor_clear_plan candidate;
	struct payload_mm_authvar_mor_live_inventory_workspace inventory;
	struct starbook_mtl_dma_guard_policy_workspace policy;
};

#if ENV_TEST
enum cb_err starbook_mtl_mor_live_inventory_compose(
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	struct payload_mm_authvar_mor_clear_plan *plan);
enum cb_err starbook_mtl_mor_live_inventory_compose_with_overlays(
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	const struct payload_mm_authvar_mor_live_inventory_overlay *overlays,
	size_t overlay_count, struct payload_mm_authvar_mor_clear_plan *plan);
#endif
enum cb_err starbook_mtl_mor_live_inventory_compose_with_overlays_owned(
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	const struct payload_mm_authvar_mor_live_inventory_overlay *overlays,
	size_t overlay_count, struct payload_mm_authvar_mor_clear_plan *plan,
	struct starbook_mtl_mor_live_inventory_workspace *workspace);
#if ENV_TEST
enum cb_err starbook_mtl_mor_live_inventory_validate(void *context,
	const struct payload_mm_authvar_mor_clear_plan *plan);
#endif

#endif /* MAINBOARD_STARLABS_STARBOOK_MTL_MOR_LIVE_INVENTORY_H */
