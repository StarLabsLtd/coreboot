/* SPDX-License-Identifier: GPL-2.0-only */
#include <boot/authvar_owner.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_writer.h>
#include <boot/payload_mm_authvar_private_trust.h>
#include "payload_mm_authvar_coordinator.h"
#include "payload_mm_authvar_authority_provider.h"
#include "payload_mm_authvar_set_preflight.h"
#include "payload_mm_authvar_internal.h"
#include <string.h>

static bool status_error(uint64_t status)
{
	return (status & PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT) != 0;
}

enum local_buffer {
	LOCAL_NV_FIRST,
	LOCAL_NV_SECOND,
	LOCAL_VOLATILE_FIRST,
	LOCAL_VOLATILE_SECOND,
	LOCAL_RECORD,
	LOCAL_BUFFER_COUNT,
};

struct local_workspace {
	size_t entries_offset, copies_offset, size;
	uint32_t records;
};

static struct local_workspace workspace_layout(size_t size)
{
	struct local_workspace layout = { 0 };
	const size_t entry_alignment = _Alignof(struct payload_mm_authvar_store_entry);
	const size_t copy_alignment = _Alignof(struct payload_mm_authvar_reclaim_copy);

	if (size > AUTHVAR_OWNER_MAX_STORE_SIZE ||
	    size < PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + 4U)
		return layout;
	/* Every serialized record has a header and a nonempty UTF-16 name. */
	layout.records = (size - PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE) /
		(PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + 4U);
	if (layout.records > PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_RECORDS)
		layout.records = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_RECORDS;
	layout.entries_offset = (LOCAL_BUFFER_COUNT * size + entry_alignment - 1U) &
		~(entry_alignment - 1U);
	layout.copies_offset = (layout.entries_offset + 4U * layout.records *
		sizeof(struct payload_mm_authvar_store_entry) + copy_alignment - 1U) &
		~(copy_alignment - 1U);
	layout.size = layout.copies_offset + layout.records *
		sizeof(struct payload_mm_authvar_reclaim_copy);
	return layout;
}

size_t authvar_owner_workspace_size(size_t store_size)
{
	return workspace_layout(store_size).size;
}

/* Backing storage travels with the owner allocation across virtual mapping. */
static uint8_t *local_buffer(struct authvar_owner *local, enum local_buffer buffer)
{
	return (uint8_t *)local + local->storage_offset +
		(size_t)buffer * local->limits.maximum_store_size;
}

static struct payload_mm_authvar_store_entry *local_entries(struct authvar_owner *local,
	enum local_buffer bank)
{
	return (void *)((uint8_t *)local + local->entries_offset +
		(size_t)bank * local->limits.maximum_records *
		sizeof(struct payload_mm_authvar_store_entry));
}

static const uint8_t certdb_guid[16] = {
	0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
	0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
};
static const uint16_t certdbv_name[] = { 'c', 'e', 'r', 't', 'd', 'b', 'v', 0 };

static bool is_certdbv(const void *guid, const void *name, size_t size)
{
	return size == sizeof(certdbv_name) &&
		!memcmp(guid, certdb_guid, sizeof(certdb_guid)) &&
		!memcmp(name, certdbv_name, sizeof(certdbv_name));
}

static bool append_record(uint8_t *store, size_t capacity, size_t *offset,
	const uint8_t guid[16], const void *name, size_t name_size, uint32_t attributes,
	const uint8_t timestamp[16], const void *data, size_t data_size)
{
	struct payload_mm_authvar_record_descriptor descriptor = {
		.name = name, .name_size = name_size, .attributes = attributes,
	};
	struct payload_mm_authvar_record_span span = { data, data_size };
	uint32_t encoded_size;

	memcpy(descriptor.vendor_guid, guid, sizeof(descriptor.vendor_guid));
	memcpy(descriptor.timestamp, timestamp, sizeof(descriptor.timestamp));
	if (*offset > capacity || !payload_mm_authvar_record_encode(&descriptor,
		&span, 1, attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED &&
		!memcmp(timestamp, (uint8_t[16]) { 0 }, 16) ?
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO :
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED,
		store + *offset, capacity - *offset, &encoded_size))
		return false;
	store[*offset + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	*offset += encoded_size;
	return true;
}

static size_t name_bytes(const uint16_t *name, size_t capacity)
{
	if (!name || capacity < sizeof(*name))
		return 0;
	for (size_t i = 0; i < capacity / sizeof(*name); i++)
		if (!name[i])
			return (i + 1U) * sizeof(*name);
	return 0;
}

uint64_t authvar_owner_view(struct authvar_owner *local,
	struct payload_mm_authvar_view *view)
{
	if (!local || local->failed)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	/* These pointers are allocation-relative, including after SetVirtualAddressMap. */
	local->index.store = local_buffer(local, LOCAL_NV_FIRST + local->active);
	local->index.entries = local_entries(local, LOCAL_NV_FIRST + local->active);
	local->volatile_index.store = local_buffer(local, LOCAL_VOLATILE_FIRST + local->volatile_active);
	local->volatile_index.entries = local_entries(local, LOCAL_VOLATILE_FIRST + local->volatile_active);
	uint8_t modes = local->modes;

	/* Retain enrolled keys without advertising an omitted image verifier. */
	if (!local->secure_boot)
		modes &= ~PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;
	return payload_mm_authvar_view_init(view, &local->index, modes,
		local->runtime) == CB_SUCCESS ? PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS :
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
}

uint64_t authvar_owner_init(struct authvar_owner *local,
	const void *store, size_t size, authvar_owner_commit_fn *commit, void *context,
	void *storage, size_t storage_size, bool secure_boot,
	const struct authvar_fixed_policy *fixed, struct authvar_owner_initialization *report)
{
	struct local_workspace workspace = workspace_layout(size);
#if CONFIG_CDK2_SECURE_BOOT_FIXED_KEYS
	const uint8_t *fixed_update = NULL;
	size_t fixed_update_size = 0;
	uint64_t fixed_status;
#endif

	if (report)
		*report = (struct authvar_owner_initialization) { 0 };
#if !CONFIG_CDK2_SECURE_BOOT_FIXED_KEYS
	if (fixed)
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
#endif
	if (!local || !store || !commit || !workspace.size ||
	    (fixed && !secure_boot) ||
	    !storage || storage_size < workspace.size ||
	    (uintptr_t)storage % _Alignof(struct payload_mm_authvar_store_entry) ||
	    (uintptr_t)storage % _Alignof(struct payload_mm_authvar_reclaim_copy) ||
	    (uintptr_t)local > UINTPTR_MAX - sizeof(*local) ||
	    (uintptr_t)storage < (uintptr_t)local + sizeof(*local) ||
	    (uintptr_t)storage > UINTPTR_MAX - storage_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	memset(local, 0, sizeof(*local));
	local->secure_boot = secure_boot;
	local->fixed_keys = fixed != NULL;
	local->storage_offset = (uintptr_t)storage - (uintptr_t)local;
	local->entries_offset = local->storage_offset + workspace.entries_offset;
	local->copies_offset = local->storage_offset + workspace.copies_offset;
	memset((uint8_t *)storage + workspace.entries_offset, 0,
		workspace.size - workspace.entries_offset);
	local->failed = true;
	local->limits = (struct payload_mm_authvar_store_limits) {
		.maximum_store_size = (uint32_t)size,
		.maximum_name_size = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE,
		.maximum_data_size = (uint32_t)size - PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE -
			PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE,
		.maximum_records = workspace.records,
	};
	local->policy = (struct payload_mm_authvar_write_policy) {
		.maximum_name_size = local->limits.maximum_name_size,
		.maximum_record_size = (uint32_t)size - PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		.maximum_data_size = local->limits.maximum_data_size,
		.maximum_records = local->limits.maximum_records,
	};
	memcpy(local_buffer(local, LOCAL_NV_FIRST), store, size);
	local->index.entries = local_entries(local, LOCAL_NV_FIRST);
	local->index.entry_capacity = local->limits.maximum_records;
	if (payload_mm_authvar_store_scan(&local->index, local_buffer(local, LOCAL_NV_FIRST), size,
		&local->limits) != CB_SUCCESS ||
	    !payload_mm_authvar_coordinator_source_modes(&local->index, false,
		false, 0, &local->modes))
		return PAYLOAD_MM_AUTHVAR_STATUS_VOLUME_CORRUPTED;
#if CONFIG_CDK2_SECURE_BOOT_FIXED_KEYS
	if (fixed) {
		if ((local->modes & (PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		    PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT)) != PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT)
			return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		uint8_t fixed_custom_mode = 0;
		if (payload_mm_authvar_mode_find(&local->index,
		    PAYLOAD_MM_AUTHVAR_MODE_KEY_CUSTOM_MODE) &&
		    (!payload_mm_authvar_mode_value(&local->index,
		    PAYLOAD_MM_AUTHVAR_MODE_KEY_CUSTOM_MODE, 0x03U, &fixed_custom_mode) ||
		    fixed_custom_mode != 0U))
			return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		fixed_status = authvar_fixed_prepare_dbx_update(&local->index, fixed,
			&fixed_update, &fixed_update_size);
		if (status_error(fixed_status))
			return fixed_status;
	}
#endif
	for (size_t i = 0; i < local->index.entry_count; i++)
		if (!(local->index.entries[i].attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE))
			return PAYLOAD_MM_AUTHVAR_STATUS_VOLUME_CORRUPTED;
	if (local->index.dirty_tail_offset != 0U) {
		if (report)
			report->interrupted_offset = local->index.dirty_tail_offset;
		/* Reclaim an interrupted append before runtime disables reclamation. */
		memset(local_buffer(local, LOCAL_NV_FIRST) + local->index.dirty_tail_offset, 0xff,
			size - local->index.dirty_tail_offset);
		if (payload_mm_authvar_store_scan(&local->index, local_buffer(local, LOCAL_NV_FIRST), size,
			&local->limits) != CB_SUCCESS)
			return PAYLOAD_MM_AUTHVAR_STATUS_VOLUME_CORRUPTED;
		uint64_t status = commit(context, local_buffer(local, LOCAL_NV_FIRST), size, NULL, NULL);

		if (report) {
			report->repair_attempted = true;
			report->repair_status = status;
		}
		if (status_error(status))
			return status;
	}
	local->commit = commit;
	local->commit_context = context;
	local->generation = 1;
	memset(local_buffer(local, LOCAL_VOLATILE_FIRST), 0xff, size);
	memcpy(local_buffer(local, LOCAL_VOLATILE_FIRST), store, PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE);
	size_t offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	if (!append_record(local_buffer(local, LOCAL_VOLATILE_FIRST), size, &offset, certdb_guid,
		certdbv_name, sizeof(certdbv_name), 0x26U, (uint8_t[16]) { 0 },
		(uint8_t[4]) { 4, 0, 0, 0 }, 4))
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	local->volatile_index.entries = local_entries(local, LOCAL_VOLATILE_FIRST);
	local->volatile_index.entry_capacity = local->limits.maximum_records;
	if (payload_mm_authvar_store_scan(&local->volatile_index, local_buffer(local, LOCAL_VOLATILE_FIRST),
		size, &local->limits) != CB_SUCCESS)
		return PAYLOAD_MM_AUTHVAR_STATUS_VOLUME_CORRUPTED;
	local->failed = false;
#if CONFIG_CDK2_SECURE_BOOT_FIXED_KEYS
	if (fixed) {
		if (fixed_update) {
			static const uint8_t dbx_guid[16] = {
				0xcb, 0xb2, 0x19, 0xd7, 0x3a, 0x3d, 0x96, 0x45,
				0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f,
			};
			static const uint16_t dbx[] = { 'd', 'b', 'x', 0 };

			fixed_status = authvar_owner_set(local, dbx, dbx_guid, 0x67U,
				fixed_update_size, fixed_update, false, fixed, NULL);
			if (report) {
				report->dbx_update_size = fixed_update_size;
				report->dbx_update_status = fixed_status;
			}
		}
		if (!status_error(fixed_status))
			fixed_status = authvar_fixed_validate(&local->index, fixed);
		if (status_error(fixed_status)) {
			local->failed = true;
			return fixed_status;
		}
		local->restart_required = fixed_update != NULL;
	}
#endif
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint64_t authvar_owner_get(struct authvar_owner *local,
	const uint16_t *name, const void *guid, uint32_t *attributes,
	size_t *data_size, void *data)
{
	struct payload_mm_authvar_view view;
	struct payload_mm_authvar_view_value value = { 0 };
	size_t size = name_bytes(name, PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE);
	uint64_t status;

	if (!guid || !data_size || !size || !*name || (*data_size && !data))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	status = authvar_owner_view(local, &view);
	if (status_error(status))
		return status;
	status = is_certdbv(guid, name, size) ? PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND :
		payload_mm_authvar_view_get(&view, (const void *)guid, name, size,
		*data_size > UINT32_MAX ? UINT32_MAX : (uint32_t)*data_size, &value);
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND) {
		struct payload_mm_authvar_get_result result;
		status = payload_mm_authvar_store_get(&local->volatile_index,
			(const void *)guid, name, size,
			*data_size > UINT32_MAX ? UINT32_MAX : (uint32_t)*data_size,
			local->runtime, &result);
		value.data_size = result.required_data_size;
		value.attributes = result.attributes;
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
			value.data = payload_mm_authvar_store_data(&local->volatile_index, result.entry);
	}
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS || status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL) {
		*data_size = value.data_size;
		if (attributes)
			*attributes = value.attributes;
	}
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && value.data_size)
		memcpy(data, value.data, value.data_size);
	return status;
}

uint64_t authvar_owner_next(struct authvar_owner *local,
	size_t *name_size, uint16_t *name, void *guid)
{
	struct payload_mm_authvar_view view;
	struct payload_mm_authvar_view_value value = { 0 };
	size_t size;
	uint64_t status;

	if (!name_size || !name || !guid)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	size = name_bytes(name, *name_size);
	if (!size)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	status = authvar_owner_view(local, &view);
	if (status_error(status))
		return status;
	bool volatile_cursor = payload_mm_authvar_store_find(&local->volatile_index,
		(const void *)guid, name, size) != NULL;
	status = volatile_cursor ? PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND :
		payload_mm_authvar_view_get_next(&view, *name ? (const void *)guid :
			(uint8_t[16]) { 0 }, *name ? name : NULL, *name ? size : 0,
			UINT32_MAX, &value);
	/* The real RAM certdbv belongs to the volatile suffix, not the native view. */
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
	    is_certdbv(value.vendor_guid, value.name, value.name_size))
		status = payload_mm_authvar_view_get_next(&view, value.vendor_guid,
			value.name, value.name_size, UINT32_MAX, &value);
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && value.name_size > *name_size)
		status = PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL;
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND) {
		struct payload_mm_authvar_next_result result;
		status = payload_mm_authvar_store_get_next(&local->volatile_index,
			volatile_cursor ? (const void *)guid : (uint8_t[16]) { 0 },
			volatile_cursor ? name : NULL, volatile_cursor ? size : 0,
			*name_size > UINT32_MAX ? UINT32_MAX : (uint32_t)*name_size,
			local->runtime, &result);
		value.name_size = result.required_name_size;
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			value.name = payload_mm_authvar_store_name(&local->volatile_index, result.entry);
			value.vendor_guid = result.entry->vendor_guid;
		}
	}
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS || status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL)
		*name_size = value.name_size;
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
		memcpy(name, value.name, value.name_size);
		memcpy(guid, value.vendor_guid, 16);
	}
	return status;
}

uint64_t authvar_owner_classify(struct authvar_owner *local,
	const uint16_t *name, const void *guid,
	struct payload_mm_authvar_key_classification *classification)
{
	struct payload_mm_authvar_view view;
	size_t size = name_bytes(name, PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE);
	uint64_t status;

	if (!guid || !size || !*name || !classification)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	status = authvar_owner_view(local, &view);
	if (status_error(status))
		return status;
	status = is_certdbv(guid, name, size) ? PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND :
		payload_mm_authvar_view_classify(&view, (const void *)guid, name, size,
		classification);
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND) {
		const struct payload_mm_authvar_store_entry *entry =
			payload_mm_authvar_store_find(&local->volatile_index, (const void *)guid, name, size);
		if (entry) {
			*classification = (struct payload_mm_authvar_key_classification) {
				.kind = PAYLOAD_MM_AUTHVAR_KEY_VOLATILE_PINNED,
				.attributes = entry->attributes,
				.flags = !local->runtime || (entry->attributes &
					PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS) ? PAYLOAD_MM_AUTHVAR_KEY_VISIBLE : 0,
			};
			return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		}
	}
	return status;
}

static uint64_t ordinary_candidate(struct authvar_owner *local,
	const struct payload_mm_authvar_policy_request *request,
	enum payload_mm_authvar_set_kind kind, uint8_t *candidate, bool *changed,
	struct payload_mm_authvar_write_plan *append, size_t *reclaimed_records)
{
	const struct payload_mm_authvar_store_entry *replaced =
		payload_mm_authvar_store_find(&local->index, request->vendor_guid,
			request->name, request->name_size);
	struct payload_mm_authvar_record_source source = {
		.name = request->name, .name_size = request->name_size,
		.data = request->data, .data_size = request->data_size,
		.attributes = request->attributes,
	};
	struct payload_mm_authvar_reclaim_plan reclaim = {
		.copies = (void *)((uint8_t *)local + local->copies_offset),
		.copy_capacity = local->limits.maximum_records,
	};
	struct payload_mm_authvar_write_plan plan;
	uint64_t status;

	*changed = false;
	memcpy(source.vendor_guid, request->vendor_guid, sizeof(source.vendor_guid));
	status = payload_mm_authvar_write_plan_build(&local->index, replaced,
		kind == PAYLOAD_MM_AUTHVAR_SET_ORDINARY_DELETE ? NULL : &source,
		&local->policy, local->runtime, local_buffer(local, LOCAL_RECORD), local->limits.maximum_store_size,
		&reclaim, &plan);
	if (status_error(status))
		return status;
	if (plan.action == PAYLOAD_MM_AUTHVAR_WRITE_NOOP)
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	if (plan.action == PAYLOAD_MM_AUTHVAR_WRITE_RECLAIM) {
		if (reclaimed_records)
			*reclaimed_records = reclaim.copy_count;
		memset(candidate, 0xff, local->index.store_size);
		memcpy(candidate, local->index.store, PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE);
		for (size_t i = 0; i < reclaim.copy_count; i++) {
			const struct payload_mm_authvar_reclaim_copy *copy = &reclaim.copies[i];
			memcpy(candidate + copy->destination_offset,
				local->index.store + copy->source_offset, copy->size);
			if (copy->promote_transition)
				candidate[copy->destination_offset + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
		}
		memcpy(candidate + plan.record_offset, local_buffer(local, LOCAL_RECORD), plan.record_size);
		candidate[plan.record_offset + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	} else {
		memcpy(candidate, local->index.store, local->index.store_size);
		for (size_t i = 0; i < plan.step_count; i++) {
			const struct payload_mm_authvar_write_step *step = &plan.steps[i];
			if (step->kind == PAYLOAD_MM_AUTHVAR_WRITE_RECORD)
				memcpy(candidate + step->offset, local_buffer(local, LOCAL_RECORD), step->size);
			else
				candidate[step->offset] = step->new_state;
		}
	}
	/* Clean ordinary mutations follow the planner's recoverable NOR state changes. */
	if (replaced && plan.action == PAYLOAD_MM_AUTHVAR_WRITE_DELETE &&
	    plan.step_count == 1U &&
	    local->index.store[replaced->record_offset + 2U] == PAYLOAD_MM_AUTHVAR_STATE_ADDED)
		*append = plan;
	if (plan.action == PAYLOAD_MM_AUTHVAR_WRITE_TAIL_APPEND &&
	    ((!replaced && plan.step_count == 3U) ||
	     (replaced && plan.step_count == 5U &&
	      local->index.store[replaced->record_offset + 2U] == PAYLOAD_MM_AUTHVAR_STATE_ADDED)))
		*append = plan;
	*changed = true;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static enum payload_mm_verify_status volatile_verify(void *context,
	const struct payload_mm_authvar_authority_verify_request *request,
	struct payload_mm_authvar_authority_verification *verification)
{
	if (context || request->route->target != PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE)
		return PAYLOAD_MM_VERIFY_REJECTED;
	return payload_mm_authvar_private_trust_verify(request->owner, request->pkcs7,
		request->content, request->content_count, request->index, request->route,
		verification);
}

static uint64_t volatile_set(struct authvar_owner *local,
	const struct payload_mm_authvar_policy_request *request)
{
	struct payload_mm_authvar_set_snapshot preflight = {
		.request = request, .index = &local->volatile_index, .at_runtime = local->runtime,
	};
	struct payload_mm_authvar_set_plan plan;
	struct payload_mm_authvar_authority_snapshot snapshot = {
		.request = request, .index = &local->volatile_index, .owner = &local->crypto,
		.verify = volatile_verify,
		.append_workspace = local_buffer(local, LOCAL_NV_FIRST + (local->active ^ 1U)),
		.append_workspace_size = local->limits.maximum_store_size,
	};
	struct payload_mm_authvar_authority_decision decision;
	struct payload_mm_authvar_store_index next = { 0 };
	const struct payload_mm_authvar_store_entry *certdb;
	const void *certdb_data;
	size_t certdb_size, offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	uint8_t bank = local->volatile_active ^ 1U;
	uint8_t *candidate = local_buffer(local, LOCAL_VOLATILE_FIRST + bank);
	uint64_t status;
	enum payload_mm_verify_status verified;

	/* Calls are serialized: the inactive NV bank is scratch until publication. */

	if (!memcmp(request->vendor_guid, certdb_guid, 16) &&
	    request->name_size == sizeof(certdbv_name) &&
	    !memcmp(request->name, certdbv_name, sizeof(certdbv_name)))
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	if (payload_mm_authvar_store_find(&local->index, request->vendor_guid,
		request->name, request->name_size))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	status = payload_mm_authvar_set_preflight(&preflight, &plan);
	if (status_error(status))
		return status;
	if (plan.kind != PAYLOAD_MM_AUTHVAR_SET_AUTH2)
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	verified = payload_mm_authvar_authority_decide(&snapshot, &decision);
	switch (verified) {
	case PAYLOAD_MM_VERIFY_OK:
		break;
	case PAYLOAD_MM_VERIFY_INVALID:
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	case PAYLOAD_MM_VERIFY_BUSY:
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	case PAYLOAD_MM_VERIFY_MALFORMED:
	case PAYLOAD_MM_VERIFY_REJECTED:
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	case PAYLOAD_MM_VERIFY_NO_MEMORY:
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	case PAYLOAD_MM_VERIFY_UNSUPPORTED:
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	default:
		local->failed = true;
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	/* Native preflight deliberately excludes volatile SET from its persistent executor. */
	if (plan.post_auth_status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
	    plan.post_auth_status != PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED)
		return plan.post_auth_status;
	if (decision.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND)
		return PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
	if (decision.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP)
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	if (decision.outcome != PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION ||
	    decision.target != PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	certdb = payload_mm_authvar_store_find(&local->volatile_index, certdb_guid,
		certdbv_name, sizeof(certdbv_name));
	certdb_data = payload_mm_authvar_store_data(&local->volatile_index, certdb);
	if (!certdb || !certdb_data)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	certdb_size = certdb->data_size;
	if (decision.intents & (PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING |
		PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING)) {
		bool add = decision.intents & PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
		enum payload_mm_authvar_certdb_result result = payload_mm_authvar_certdb_compose(
			certdb_data, certdb_size, add ? PAYLOAD_MM_AUTHVAR_CERTDB_ADD :
			PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE, request->vendor_guid,
			request->name, request->name_size - sizeof(uint16_t),
			add ? decision.new_binding : NULL, add ? decision.new_binding_size : 0,
			local_buffer(local, LOCAL_RECORD), local->limits.maximum_store_size, &certdb_size);
		if (result != PAYLOAD_MM_AUTHVAR_CERTDB_OK)
			return result == PAYLOAD_MM_AUTHVAR_CERTDB_NO_SPACE ?
				PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES : PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		certdb_data = local_buffer(local, LOCAL_RECORD);
	}
	memset(candidate, 0xff, local->volatile_index.store_size);
	memcpy(candidate, local->volatile_index.store, PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE);
	for (size_t i = 0; i < local->volatile_index.entry_count; i++) {
		const struct payload_mm_authvar_store_entry *entry = &local->volatile_index.entries[i];
		const void *name = payload_mm_authvar_store_name(&local->volatile_index, entry);
		if (entry == certdb || (!memcmp(entry->vendor_guid, request->vendor_guid, 16) &&
		    entry->name_size == request->name_size && !memcmp(name, request->name, entry->name_size)))
			continue;
		if (!append_record(candidate, local->volatile_index.store_size, &offset,
			entry->vendor_guid, name, entry->name_size, entry->attributes,
			local->volatile_index.store + entry->record_offset + 16U,
			payload_mm_authvar_store_data(&local->volatile_index, entry), entry->data_size))
			return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	}
	if (!append_record(candidate, local->volatile_index.store_size, &offset,
		certdb_guid, certdbv_name, sizeof(certdbv_name), 0x26U,
		(uint8_t[16]) { 0 }, certdb_data, certdb_size))
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	if (decision.mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_WRITE &&
	    !append_record(candidate, local->volatile_index.store_size, &offset,
		request->vendor_guid, request->name, request->name_size,
		decision.mutation.attributes, decision.mutation.timestamp, decision.data, decision.data_size))
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	next.entries = local_entries(local, LOCAL_VOLATILE_FIRST + bank);
	next.entry_capacity = local->limits.maximum_records;
	if (payload_mm_authvar_store_scan(&next, candidate, local->volatile_index.store_size,
		&local->limits) != CB_SUCCESS)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	/* Target and its signer binding switch together; this bank never reaches media. */
	local->volatile_active = bank;
	local->volatile_index = next;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static uint64_t publish_candidate(struct authvar_owner *local, uint8_t bank, uint8_t modes,
	const struct payload_mm_authvar_write_plan *append, bool policy_changed,
	const struct authvar_fixed_policy *fixed)
{
	struct payload_mm_authvar_store_index next = {
		.entries = local_entries(local, LOCAL_NV_FIRST + bank),
		.entry_capacity = local->limits.maximum_records,
	};
	if (local->generation == UINT64_MAX)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (payload_mm_authvar_store_scan(&next, local_buffer(local, LOCAL_NV_FIRST + bank),
		local->index.store_size,
		&local->limits) != CB_SUCCESS ||
	    !payload_mm_authvar_coordinator_source_modes(&next, local->runtime,
		true, modes, &modes)) {
		local->failed = true;
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
#if CONFIG_CDK2_SECURE_BOOT_FIXED_KEYS
	if (local->fixed_keys) {
		if (!fixed)
			return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		if ((modes & (PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		    PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT)) != PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT)
			return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		if (policy_changed) {
			uint64_t fixed_status = authvar_fixed_validate(&next, fixed);
			if (status_error(fixed_status))
				return fixed_status;
		}
	}
#else
	(void)policy_changed;
	(void)fixed;
#endif
	/* Neither the new value nor mode/certdb intents become visible before durability. */
	uint64_t status = local->commit(local->commit_context,
		local_buffer(local, LOCAL_NV_FIRST + bank), next.store_size,
		append, append ? local_buffer(local, LOCAL_RECORD) : NULL);
	if (status_error(status)) {
		/* The media callback may have failed after programming: require recovery. */
		local->failed = true;
		return status;
	}
	local->active = bank;
	local->index = next;
	local->modes = modes;
	local->generation++;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint64_t authvar_owner_set(struct authvar_owner *local,
	const uint16_t *name, const void *guid, uint32_t attributes,
	size_t data_size, const void *data, bool trusted_presence,
	const struct authvar_fixed_policy *fixed, size_t *reclaimed_records)
{
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET, .attributes = attributes,
		.name = name, .name_size = name_bytes(name,
			PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE),
		.data = data, .data_size = data_size,
	};
	struct payload_mm_authvar_view view;
	struct payload_mm_authvar_set_snapshot snapshot;
	struct payload_mm_authvar_set_plan plan;
	struct payload_mm_authvar_write_plan append = {0};
	uint8_t modes, bank;
	uint64_t status;

	if (reclaimed_records)
		*reclaimed_records = SIZE_MAX;
	if (!local || !guid || !request.name_size || !*name || (data_size && !data))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (trusted_presence && (local->ready || local->runtime))
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	if (local->fixed_keys != (fixed != NULL))
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
#if CONFIG_CDK2_SECURE_BOOT_FIXED_KEYS
	if (local->fixed_keys) {
		if (authvar_fixed_classify(guid, name, request.name_size) == AUTHVAR_FIXED_LOCKED)
			return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
		/* Physical confirmation cannot replace dbx signature authorization. */
		trusted_presence = false;
	}
#endif
	status = authvar_owner_view(local, &view);
	if (status_error(status))
		return status;
	memcpy(request.vendor_guid, guid, sizeof(request.vendor_guid));
	if (!(attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE) &&
	    (attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED))
		return volatile_set(local, &request);
	if (payload_mm_authvar_store_find(&local->volatile_index, request.vendor_guid,
		request.name, request.name_size))
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	snapshot = (struct payload_mm_authvar_set_snapshot) {
		.request = &request, .index = &local->index, .at_runtime = local->runtime,
		.trusted_physical_presence = trusted_presence,
	};
	status = payload_mm_authvar_set_preflight(&snapshot, &plan);
	if (status_error(status) || plan.kind == PAYLOAD_MM_AUTHVAR_SET_NOOP)
		return status;
	if (local->generation == UINT64_MAX)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	bank = local->active ^ 1U;
	modes = local->modes;
	if (plan.kind == PAYLOAD_MM_AUTHVAR_SET_AUTH2) {
		struct payload_mm_authvar_candidate_binding binding = {
			.generation = local->generation, .token = 1,
			.source_volatile_modes = local->modes, .at_runtime = local->runtime,
		};
		struct payload_mm_authvar_coordinator_policy coordinator = {
			.request = &request, .owner = &local->crypto,
			.verify = payload_mm_authvar_authority_provider_verify,
			.trusted_physical_presence = trusted_presence,
		};
		struct payload_mm_authvar_coordinator_result result;
		bool invariant_failure = false;

		status = payload_mm_authvar_coordinator_prepare(&coordinator, &local->index,
			&local->policy, &binding, local->ready,
			local_buffer(local, LOCAL_VOLATILE_FIRST + (local->volatile_active ^ 1U)),
			local->limits.maximum_store_size,
			local_buffer(local, LOCAL_NV_FIRST + bank), local->index.store_size,
			local_entries(local, LOCAL_NV_FIRST + bank), local->limits.maximum_records,
			&result, &invariant_failure);
		if (invariant_failure)
			local->failed = true;
		if (status_error(status) || result.outcome != PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION)
			return status;
		modes = result.volatile_modes;
	} else {
		bool changed;
		status = ordinary_candidate(local, &request, plan.kind,
			local_buffer(local, LOCAL_NV_FIRST + bank), &changed,
			&append, reclaimed_records);
		if (status_error(status) || !changed)
			return status;
	}
	bool policy_changed = false;
#if CONFIG_CDK2_SECURE_BOOT_FIXED_KEYS
	policy_changed = local->fixed_keys &&
		authvar_fixed_classify(guid, name, request.name_size) == AUTHVAR_FIXED_REVOCATIONS;
#endif
	return publish_candidate(local, bank, modes, append.step_count ? &append : NULL,
		policy_changed, fixed);
}

uint64_t authvar_owner_delete_pk(struct authvar_owner *local)
{
	if (local && local->fixed_keys)
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	struct payload_mm_authvar_view view;
	struct payload_mm_authvar_coordinator_result result;
	bool invariant_failure = false;
	uint64_t status = authvar_owner_view(local, &view);
	if (status_error(status))
		return status;
	if (local->ready || local->runtime)
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	struct payload_mm_authvar_candidate_binding binding = {
		.generation = local->generation, .token = 1,
		.source_volatile_modes = local->modes,
	};
	uint8_t bank = local->active ^ 1U;
	status = payload_mm_authvar_presence_prepare(&local->index, &local->policy,
		&binding, local->ready, local_buffer(local, LOCAL_NV_FIRST + bank), local->index.store_size,
		local_entries(local, LOCAL_NV_FIRST + bank), local->limits.maximum_records,
		&result, &invariant_failure);
	if (invariant_failure)
		local->failed = true;
	if (status_error(status) || result.outcome != PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION)
		return status;
	return publish_candidate(local, bank, result.volatile_modes, NULL, true, NULL);
}

uint64_t authvar_owner_confirmed_key(struct authvar_owner *local,
	uint32_t key, uint32_t mutation, const void *data, size_t size)
{
	if (local && local->fixed_keys)
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	struct payload_mm_authvar_policy_request request;
	struct payload_mm_authvar_view view;
	struct payload_mm_authvar_coordinator_result result;
	bool invariant_failure = false;
	uint64_t status = authvar_owner_view(local, &view);
	if (status_error(status))
		return status;
	if (local->ready || local->runtime)
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	if (!payload_mm_authvar_confirmed_key_request(key, mutation, data, size, &request))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (local->generation == UINT64_MAX)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	struct payload_mm_authvar_confirmed_operation operation = { key, mutation };
	struct payload_mm_authvar_coordinator_policy coordinator = {
		.confirmed_operation = &operation, .request = &request, .owner = &local->crypto,
		.verify = payload_mm_authvar_authority_provider_verify,
		.trusted_physical_presence = true,
	};
	struct payload_mm_authvar_candidate_binding binding = {
		.generation = local->generation, .token = 1,
		.source_volatile_modes = local->modes,
	};
	uint8_t bank = local->active ^ 1U;
	status = payload_mm_authvar_coordinator_prepare(&coordinator, &local->index,
		&local->policy, &binding, local->ready,
		local_buffer(local, LOCAL_VOLATILE_FIRST + (local->volatile_active ^ 1U)),
		local->limits.maximum_store_size,
		local_buffer(local, LOCAL_NV_FIRST + bank), local->index.store_size,
		local_entries(local, LOCAL_NV_FIRST + bank), local->limits.maximum_records,
		&result, &invariant_failure);
	if (invariant_failure)
		local->failed = true;
	if (status_error(status) || result.outcome != PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION)
		return status;
	return publish_candidate(local, bank, result.volatile_modes, NULL, true, NULL);
}

uint64_t authvar_owner_query(struct authvar_owner *local,
	uint32_t attributes, uint64_t *maximum_storage, uint64_t *remaining_storage,
	uint64_t *maximum_variable)
{
	struct payload_mm_authvar_view view;
	struct payload_mm_authvar_query_result result;
	struct payload_mm_authvar_store_policy policy;
	uint64_t status;

	if (!maximum_storage || !remaining_storage || !maximum_variable)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	status = authvar_owner_view(local, &view);
	if (status_error(status))
		return status;
	if (!(attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE)) {
		if (!attributes || attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED ||
		    attributes & PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR ||
		    !(attributes & PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS) ||
		    (local->runtime && !(attributes & PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		if (attributes & PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE)
			return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
		*maximum_storage = local->volatile_index.store_size - PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
		*remaining_storage = local->volatile_index.store_size - local->volatile_index.used_size;
		*maximum_variable = *remaining_storage > PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE ?
			*remaining_storage - PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE : 0;
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
	policy = (struct payload_mm_authvar_store_policy) {
		.maximum_storage = local->index.store_size - PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		.maximum_record_size = local->policy.maximum_record_size,
	};
	status = payload_mm_authvar_view_query(&view, &policy, attributes, &result);
	if (!status_error(status)) {
		*maximum_storage = result.maximum_storage;
		*remaining_storage = result.remaining_storage;
		*maximum_variable = result.maximum_variable;
	}
	return status;
}

void authvar_owner_ready(struct authvar_owner *local)
{
	local->ready = true;
}

void authvar_owner_enter_runtime(struct authvar_owner *local)
{
	local->ready = true;
	local->runtime = true;
}
