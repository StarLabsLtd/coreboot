/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_PAYLOAD_MM_FMP_OWNER_LAYOUT_INTERNAL_H
#define LIB_PAYLOAD_MM_FMP_OWNER_LAYOUT_INTERNAL_H

#include <boot/capsule_update.h>
#include <stddef.h>
#include <types.h>

#define PAYLOAD_MM_FMP_OWNER_LAYOUT_REVISION 1U
#define PAYLOAD_MM_FMP_OWNER_LAYOUT_DOMAINS 2U
#define PAYLOAD_MM_FMP_OWNER_LAYOUT_MAX_ROUTES CAPSULE_UPDATE_MAX_REGIONS

struct fmp_owner_range {
	u64 offset;
	u64 size;
};

/*
 * Canonical, pointer-free description of every boot-media authority that can
 * reach the protected owner journal. Platform code must fill this from its
 * immutable FMAP and update-route policy, never from payload input.
 */
struct fmp_owner_layout {
	u32 revision;
	u32 size;
	u64 media_size;
	u32 erase_size;
	u32 slot_size;
	u32 route_count;
	u32 reserved;
	struct fmp_owner_range
		state[PAYLOAD_MM_FMP_OWNER_LAYOUT_DOMAINS];
	struct fmp_owner_range smmstore;
	struct lb_capsule_update_region
		route[PAYLOAD_MM_FMP_OWNER_LAYOUT_MAX_ROUTES];
} __aligned(8);

bool payload_mm_fmp_layout_valid(const struct fmp_owner_layout *layout);

#endif
