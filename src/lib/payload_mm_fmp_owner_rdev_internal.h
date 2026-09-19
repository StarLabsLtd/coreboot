/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_PAYLOAD_MM_FMP_OWNER_RDEV_INTERNAL_H
#define LIB_PAYLOAD_MM_FMP_OWNER_RDEV_INTERNAL_H

#include <commonlib/region.h>

#include "payload_mm_fmp_owner_journal_internal.h"
#include "payload_mm_fmp_owner_layout_internal.h"

#define PAYLOAD_MM_FMP_OWNER_RDEV_REVISION 1U
#define PAYLOAD_MM_FMP_OWNER_RDEV_SYNC_CONTEXT_SIZE 64U

typedef enum cb_err payload_mm_fmp_owner_rdev_sync_fn(const void *context);

struct payload_mm_fmp_owner_rdev_policy {
	u32 revision;
	u32 size;
	struct fmp_owner_layout layout;
	struct region_device state[PAYLOAD_MM_FMP_OWNER_LAYOUT_DOMAINS];
	const struct region_device *root_device;
	struct region_device root;
	struct region_device_ops ops;
	payload_mm_fmp_owner_rdev_sync_fn *sync;
	size_t sync_context_size;
	u8 sync_context[PAYLOAD_MM_FMP_OWNER_RDEV_SYNC_CONTEXT_SIZE] __aligned(8);
};

/*
 * A selected platform must zero-initialize this object, then keep it in
 * protected, writable memory for the lifetime of the journal. Initialization
 * is one-shot. The adapter copies and seals all policy bytes; it never retains
 * caller-owned descriptor or synchronization-context bytes.
 */
struct payload_mm_fmp_owner_rdev {
	struct payload_mm_fmp_owner_rdev_policy policy;
	struct payload_mm_fmp_owner_rdev_policy sealed_policy;
	bool initialized;
	bool busy;
	bool poisoned;
};

/* Small enough to be copied into the journal's sealed callback context. */
struct payload_mm_fmp_owner_rdev_context {
	u32 revision;
	u32 size;
	struct payload_mm_fmp_owner_rdev *adapter;
	u64 media_size;
	u32 erase_size;
	u32 slot_size;
	struct fmp_owner_range state[PAYLOAD_MM_FMP_OWNER_LAYOUT_DOMAINS];
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_fmp_owner_rdev_context) <=
	PAYLOAD_MM_FMP_OWNER_JOURNAL_CONTEXT_SIZE,
	"Owner rdev context must fit the sealed journal context");

enum cb_err payload_mm_fmp_owner_rdev_init(
	struct payload_mm_fmp_owner_rdev *adapter,
	struct payload_mm_fmp_owner_rdev_context *context,
	const struct fmp_owner_layout *layout,
	const struct region_device state[PAYLOAD_MM_FMP_OWNER_LAYOUT_DOMAINS],
	payload_mm_fmp_owner_rdev_sync_fn *sync, const void *sync_context,
	size_t sync_context_size);
enum cb_err payload_mm_fmp_owner_rdev_read(const void *context, u64 offset,
	void *buffer, size_t size);
enum cb_err payload_mm_fmp_owner_rdev_program(const void *context, u64 offset,
	const void *buffer, size_t size);
enum cb_err payload_mm_fmp_owner_rdev_erase(const void *context, u64 offset,
	size_t size);
enum cb_err payload_mm_fmp_owner_rdev_sync(const void *context);

#endif
