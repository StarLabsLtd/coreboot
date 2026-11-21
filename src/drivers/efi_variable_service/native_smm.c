/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/authvar_policy.h>
#include <boot/payload_mm_authvar_service.h>
#include <commonlib/helpers.h>
#include <efi_variable_service.h>
#include <string.h>

#include "native_media.h"
#include "native_capsule.h"
#include "native_smm.h"
#include "native_variables.h"

struct ass_smm_owner {
	struct ass_native_store store;
	struct authvar_policy policy;
	struct authvar_owner *owner;
	const struct authvar_fixed_policy *fixed;
	enum ass_mm_phase phase;
	struct ass_capsule_transaction capsule;
	_Alignas(16) uint8_t message[EFI_VARIABLE_SERVICE_MESSAGE_CAPACITY];
};

uint64_t ass_native_smm_open(void *arena, size_t arena_size,
	const struct region_device *region, uint32_t block_size, size_t maximum_data_size,
	bool (*write_enabled)(void), bool secure_boot, const struct authvar_fixed_policy *fixed,
	bool *restart_required)
{
	struct ass_smm_owner *service = arena;
	const size_t header_size = ALIGN_UP(sizeof(*service), 16);
	struct ass_native_store_memory memory;

	if (!restart_required)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	*restart_required = false;
	if (!arena || (uintptr_t)arena % 16 || !region || arena_size < header_size ||
	    arena_size > UINTPTR_MAX - (uintptr_t)arena)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	memset(service, 0, sizeof(*service));
	if (!ass_native_store_layout(&service->store, &memory, (uint8_t *)arena + header_size,
	    arena_size - header_size, region_device_sz(region), block_size))
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	uint64_t status = ass_native_store_open(&service->store, memory.owner, region,
		memory.scratch, write_enabled, memory.workspace, memory.workspace_size,
		maximum_data_size, secure_boot, fixed);
	if (status)
		return status;
	if (memory.owner->restart_required) {
		*restart_required = true;
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
	service->fixed = fixed;
	service->phase = ASS_MM_WAIT_END_OF_DXE;
	service->owner = memory.owner;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint32_t ass_native_smm_communicate(void *arena, void *mailbox, size_t mailbox_size,
	const struct ass_mm_capsule *capsule)
{
	struct ass_smm_owner *service = arena;

	if (!service || !service->owner)
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	return ass_native_mailbox_dispatch(service->owner, &service->policy, service->fixed,
		&service->phase, capsule, mailbox, mailbox_size, service->message,
		sizeof(service->message));
}

bool ass_native_smm_resume(void *arena, size_t arena_size,
	const struct region_device *region, uint32_t block_size,
	bool (*write_enabled)(void), const struct authvar_fixed_policy *fixed)
{
	struct ass_smm_owner *service = arena;
	const size_t header_size = ALIGN_UP(sizeof(*service), 16);
	struct ass_native_store layout;
	struct ass_native_store_memory memory;

	if (!arena || !region || (uintptr_t)arena % 16 || arena_size <= header_size ||
	    arena_size > UINTPTR_MAX - (uintptr_t)arena ||
	    !ass_native_store_layout(&layout, &memory, (uint8_t *)arena + header_size,
		arena_size - header_size, region_device_sz(region), block_size))
		return false;
	if (service->phase != ASS_MM_RUNTIME || service->owner != memory.owner ||
	    !service->owner->runtime || service->owner->failed ||
	    service->owner->fixed_keys != (fixed != NULL) ||
	    service->owner->restart_required || service->store.media.failed ||
	    service->store.media.block_size != block_size ||
	    service->store.media.image_size != layout.media.image_size ||
	    service->store.media.image != layout.media.image ||
	    service->store.media.candidate != layout.media.candidate ||
	    service->store.media.verify != layout.media.verify ||
	    service->store.media.workspace != layout.media.workspace)
		return false;

	/* Resume must not reopen boot access or replay store initialization. */
	if (!ass_native_store_resume(&service->store, service->owner, region,
	    memory.scratch, write_enabled))
		return false;
	service->fixed = fixed;
	return true;
}

bool ass_native_smm_boot_access(void *arena, bool *allowed)
{
	struct ass_smm_owner *service = arena;
	struct payload_mm_authvar_view view;

	if (!service || !service->owner || !allowed ||
	    authvar_owner_view(service->owner, &view))
		return false;
	*allowed = service->phase <= ASS_MM_WAIT_READY_TO_BOOT && !service->owner->ready &&
		!service->owner->runtime;
	return true;
}

bool ass_native_smm_variable(void *arena, void *message, size_t size)
{
	struct ass_smm_owner *service = arena;

	return service && service->owner &&
		ass_native_variable_dispatch(service->owner, &service->policy, service->fixed,
			message, size);
}

struct payload_mm_crypto_owner *ass_native_smm_crypto(void *arena)
{
	struct ass_smm_owner *service = arena;

	return service && service->owner ? &service->owner->crypto : NULL;
}

struct ass_capsule_transaction *ass_native_smm_capsule_transaction(void *arena,
	const struct ass_capsule_platform *platform, bool resume)
{
	struct ass_smm_owner *service = arena;

	if (!service || !service->owner || !platform)
		return NULL;
	if (resume)
		return service->capsule.active ? NULL : &service->capsule;
	if (ass_native_capsule_floor(service->owner, platform->geometry.image_guid,
	    platform->geometry.compiled_floor, &service->capsule.floor))
		return NULL;
	return &service->capsule;
}
