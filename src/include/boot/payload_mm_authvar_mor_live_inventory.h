/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_H
#define BOOT_PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_H

#include <boot/payload_mm_authvar_mor_clear.h>

#define PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_MAX_OVERLAYS 8U

struct payload_mm_authvar_mor_live_inventory_overlay {
	uint64_t base;
	uint64_t size;
	uint32_t exclusion_reason;
	uint32_t reserved;
} __aligned(8);

struct payload_mm_authvar_mor_live_inventory_request {
	uint32_t revision;
	uint32_t size;
	uint64_t generation;
	uint8_t identity[32];
	uint32_t overlay_count;
	uint32_t reserved;
	struct payload_mm_authvar_mor_live_inventory_overlay overlays[
		PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_MAX_OVERLAYS];
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_authvar_mor_live_inventory_overlay) == 24,
	"MOR live-inventory overlay ABI changed");
_Static_assert(sizeof(struct payload_mm_authvar_mor_live_inventory_request) == 248,
	"MOR live-inventory request ABI changed");

enum cb_err payload_mm_authvar_mor_live_inventory_compose(
	const struct payload_mm_authvar_mor_live_inventory_request *request,
	struct payload_mm_authvar_mor_clear_plan *plan);

#endif /* BOOT_PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_H */
