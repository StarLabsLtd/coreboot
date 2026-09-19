/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_CAPSULE_UPDATE_INTERNAL_H
#define LIB_CAPSULE_UPDATE_INTERNAL_H

#include <boot/capsule_update.h>
#include "payload_mm_fmp_owner_layout_internal.h"

typedef enum cb_err capsule_media_read_fn(void *, u64, void *, size_t);
typedef enum cb_err capsule_media_erase_fn(void *, u64, size_t);
typedef enum cb_err capsule_media_write_fn(void *, u64, const void *, size_t);

struct capsule_media_backend {
	void *context;
	u64 size;
	u32 erase_size;
	capsule_media_read_fn *read;
	capsule_media_erase_fn *erase;
	capsule_media_write_fn *write;
};

struct capsule_media_policy {
	u64 media_size;
	u32 erase_size;
	u64 smmstore_offset;
	u64 smmstore_size;
	const struct lb_capsule_update_region *regions;
	size_t region_count;
	const struct fmp_owner_layout *owner_layout;
};

enum cb_err capsule_apply_policy_verified(const struct capsule_update_plan *plan,
					  const struct capsule_media_policy *policy,
					  const struct capsule_media_backend *media,
					  void *scratch, size_t scratch_bytes);

#endif
