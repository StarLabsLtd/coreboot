/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/authvar_media.h>
#include <boot/authvar_fixed_policy.h>
#include <boot/payload_mm_authvar_default_store.h>
#include <boot/payload_mm_authvar_service.h>
#include <commonlib/helpers.h>
#include <commonlib/payload_mm_authvar_fv.h>
#include <string.h>

#include "native_media.h"

static uint64_t media_transfer(struct ass_native_media_binding *binding,
	uint32_t offset, const void *source, void *destination, size_t size)
{
	struct efi_private_store *store = &binding->store;
	uint8_t *scratch = (void *)store->buffer_start;
	const size_t length = region_device_sz(store->device);
	size_t completed = 0;
	uint64_t status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;

	if (offset > length || size > length - offset ||
	    (source && !binding->write_enabled()))
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;

	/* Media recovery reads full images; the private adapter admits one block at a time. */
	while (completed < size) {
		const size_t position = offset + completed;
		const size_t chunk = MIN(size - completed,
			store->block_size - position % store->block_size);
		int ret;

		if (source) {
			memcpy(scratch, (const uint8_t *)source + completed, chunk);
			ret = efi_private_store_write(store, position, scratch, chunk);
		} else {
			ret = efi_private_store_read(store, position, scratch, chunk);
			if (!ret)
				memcpy((uint8_t *)destination + completed, scratch, chunk);
		}
		if (ret) {
			status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
			break;
		}
		completed += chunk;
	}
	for (volatile uint8_t *p = scratch; p < scratch + store->block_size; p++)
		*p = 0;
	return status;
}

static uint64_t native_read(void *context, uint32_t offset, void *buffer, size_t size)
{
	if (!buffer)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	return media_transfer(context, offset, NULL, buffer, size);
}

static uint64_t native_write(void *context, uint32_t offset, const void *buffer, size_t size)
{
	if (!buffer)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	return media_transfer(context, offset, buffer, NULL, size);
}

static uint64_t native_erase(void *context, uint32_t offset, size_t size)
{
	struct ass_native_media_binding *binding = context;
	struct efi_private_store *store = &binding->store;
	const size_t length = region_device_sz(store->device);
	const size_t block = store->block_size;

	if (!binding->write_enabled() || offset > length || size > length - offset ||
	    offset % block || size % block)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	for (size_t i = 0; i < size; i += block)
		if (efi_private_store_erase(store, offset + i, block))
			return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

bool ass_native_media_bind(struct ass_native_media_binding *binding,
	struct authvar_media_io *io, const struct region_device *region,
	void *scratch, size_t block_size, bool (*write_enabled)(void))
{
	if (!binding || !io || !write_enabled ||
	    efi_private_store_init(&binding->store, region, block_size,
		(uintptr_t)scratch, block_size))
		return false;
	binding->write_enabled = write_enabled;
	*io = (struct authvar_media_io) {
		.context = binding,
		.read = native_read,
		.write = native_write,
		.erase = native_erase,
	};
	return true;
}

bool ass_native_store_layout(struct ass_native_store *store,
	struct ass_native_store_memory *memory, void *arena, size_t arena_size,
	size_t image_size, uint32_t block_size)
{
	struct payload_mm_authvar_fv_geometry geometry;
	const size_t owner_size = ALIGN_UP(sizeof(struct authvar_owner), 16);
	uint8_t *base = arena;

	if (!store || !memory || !arena || (uintptr_t)arena % 16 ||
	    arena_size > UINTPTR_MAX - (uintptr_t)arena || owner_size > arena_size ||
	    !payload_mm_authvar_fv_geometry(&geometry, image_size, block_size))
		return false;
	const size_t workspace = authvar_owner_workspace_size(geometry.variable_size -
		PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE);
	if (!workspace || workspace > arena_size - owner_size)
		return false;
	size_t left = arena_size - owner_size - workspace;
	if (image_size > left / 2)
		return false;
	left -= 2 * image_size;
	if (block_size > left / 3)
		return false;

	uint8_t *image = base + owner_size + workspace;
	uint8_t *candidate = image + image_size;
	uint8_t *verify = candidate + image_size;
	uint8_t *journal = verify + block_size;
	*memory = (struct ass_native_store_memory) {
		.owner = arena, .workspace = base + owner_size,
		.workspace_size = workspace, .scratch = journal + block_size,
	};
	store->media = (struct authvar_media) {
		.image = image, .candidate = candidate, .verify = verify, .workspace = journal,
		.image_size = image_size, .candidate_size = image_size, .block_size = block_size,
	};
	return true;
}

static uint64_t commit_native_store(void *context, const void *data, size_t size,
	const struct payload_mm_authvar_write_plan *append, const void *record)
{
	struct ass_native_store *store = context;

	return authvar_media_commit(&store->media, &store->io, data, size, append, record);
}

#if CONFIG(ASS_FIXED_KEYS)
static uint64_t reject_seed_write(void *context, const void *data, size_t size,
	const struct payload_mm_authvar_write_plan *append, const void *record)
{
	return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
}
#endif

bool ass_native_store_resume(struct ass_native_store *store, struct authvar_owner *owner,
	const struct region_device *region, void *scratch, bool (*write_enabled)(void))
{
	if (owner->commit_context != store ||
	    !ass_native_media_bind(&store->binding, &store->io, region, scratch,
		store->media.block_size, write_enabled))
		return false;
	owner->commit = commit_native_store;
	return true;
}

uint64_t ass_native_store_open(struct ass_native_store *store, struct authvar_owner *owner,
	const struct region_device *region, void *scratch, bool (*write_enabled)(void),
	void *workspace, size_t workspace_size, size_t maximum_data_size, bool secure_boot,
	const struct authvar_fixed_policy *fixed)
{
	if (!store || !owner || !region || region_device_sz(region) != store->media.image_size ||
	    !ass_native_media_bind(&store->binding, &store->io, region, scratch,
		store->media.block_size, write_enabled))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	uint64_t status = authvar_media_initialize(&store->media, &store->io);
	if (status)
		return status;
#if CONFIG(ASS_FIXED_KEYS)
	if (fixed) {
		struct authvar_media *media = &store->media;
		uint8_t *candidate = media->candidate + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;

		if (payload_mm_authvar_default_store_compose(media->image, media->candidate,
		    media->image_size, media->block_size) ==
		    PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID)
			return PAYLOAD_MM_AUTHVAR_STATUS_VOLUME_CORRUPTED;
		/* Never turn an existing mutable installation into a factory store. */
		if (media->plan.fv_header_size == PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE &&
		    !memcmp(candidate, media->store, media->store_size)) {
			status = authvar_fixed_seed(fixed, candidate, media->store_size,
				payload_mm_authvar_default_store_records_end());
			if (status)
				return status;
			status = authvar_owner_init(owner, candidate, media->store_size,
				maximum_data_size, reject_seed_write, NULL, workspace,
				workspace_size, secure_boot, fixed, NULL);
			if (status || owner->restart_required)
				return status ? status : PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
			status = authvar_media_commit(media, &store->io, candidate,
				media->store_size, NULL, NULL);
			if (status)
				return status;
		}
	}
#endif
	return authvar_owner_init(owner, store->media.store, store->media.store_size,
		maximum_data_size, commit_native_store, store, workspace, workspace_size,
		secure_boot, fixed, NULL);
}
