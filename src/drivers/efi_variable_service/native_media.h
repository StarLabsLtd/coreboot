/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EFI_VARIABLE_SERVICE_NATIVE_MEDIA_H
#define EFI_VARIABLE_SERVICE_NATIVE_MEDIA_H

#include "private_store.h"
#include <boot/authvar_media.h>
#include <boot/authvar_owner.h>


struct ass_native_media_binding {
	struct efi_private_store store;
	bool (*write_enabled)(void);
};

/* The caller supplies a trusted flash region and a private block of scratch. */
bool ass_native_media_bind(struct ass_native_media_binding *binding,
	struct authvar_media_io *io, const struct region_device *region,
	void *scratch, size_t block_size, bool (*write_enabled)(void));

struct ass_native_store {
	struct authvar_media media;
	struct authvar_media_io io;
	struct ass_native_media_binding binding;
};

struct ass_native_store_memory {
	struct authvar_owner *owner;
	void *workspace;
	size_t workspace_size;
	void *scratch;
};

/* The caller owns the aligned SMRAM arena for the service's entire lifetime. */
bool ass_native_store_layout(struct ass_native_store *store,
	struct ass_native_store_memory *memory, void *arena, size_t arena_size,
	size_t image_size, uint32_t block_size);
bool ass_native_store_resume(struct ass_native_store *store, struct authvar_owner *owner,
	const struct region_device *region, void *scratch, bool (*write_enabled)(void));

/* Recover the on-flash store before publishing the owner to any caller. */
uint64_t ass_native_store_open(struct ass_native_store *store, struct authvar_owner *owner,
	const struct region_device *region, void *scratch, bool (*write_enabled)(void),
	void *workspace, size_t workspace_size, size_t maximum_data_size, bool secure_boot,
	const struct authvar_fixed_policy *fixed);

#endif
