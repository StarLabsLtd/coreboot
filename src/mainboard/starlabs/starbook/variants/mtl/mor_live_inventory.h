/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_MOR_LIVE_INVENTORY_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_MOR_LIVE_INVENTORY_H

#include "dma_guard.h"

enum cb_err starbook_mtl_mor_live_inventory_compose(
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	struct payload_mm_authvar_mor_clear_plan *plan);
enum cb_err starbook_mtl_mor_live_inventory_validate(void *context,
	const struct payload_mm_authvar_mor_clear_plan *plan);

#endif /* MAINBOARD_STARLABS_STARBOOK_MTL_MOR_LIVE_INVENTORY_H */
