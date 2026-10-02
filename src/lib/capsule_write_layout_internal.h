/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_CAPSULE_WRITE_LAYOUT_INTERNAL_H
#define LIB_CAPSULE_WRITE_LAYOUT_INTERNAL_H

#include <boot/capsule_update.h>
#include <types.h>

#define CAPSULE_WRITE_LAYOUT_REVISION 1U
#define CAPSULE_WRITE_LAYOUT_MAX_METADATA 2U

struct capsule_write_range {
	u64 offset;
	u64 size;
};

/*
 * Physical write exclusions, not an owner-journal storage format or authority.
 * The trusted installer must cover its actual owner metadata. An authenticated
 * variable owner uses SMMSTORE; a raw-journal owner also excludes its journals.
 */
struct capsule_write_layout {
	u32 revision;
	u32 size;
	u64 media_size;
	u32 erase_size;
	u32 route_count;
	u32 metadata_count;
	u32 reserved;
	struct capsule_write_range smmstore;
	struct capsule_write_range metadata[CAPSULE_WRITE_LAYOUT_MAX_METADATA];
	struct lb_capsule_update_region route[CAPSULE_UPDATE_MAX_REGIONS];
} __aligned(8);

bool capsule_write_layout_valid(const struct capsule_write_layout *layout);

#endif
