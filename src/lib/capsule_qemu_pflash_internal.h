/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_CAPSULE_QEMU_PFLASH_INTERNAL_H
#define LIB_CAPSULE_QEMU_PFLASH_INTERNAL_H

#include "capsule_broker_internal.h"
#include "capsule_write_layout_internal.h"

/* Transport only: the caller must independently establish current MM ownership. */
enum cb_err capsule_qemu_pflash_backend_build(const struct capsule_write_layout *layout,
	uintptr_t staging_base, size_t staging_size,
	capsule_broker_protected_storage_fn storage_is_protected, void *storage_context,
	struct capsule_media_backend *media, size_t *context_size);

#endif
