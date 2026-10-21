/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_bundle.h>
#include <boot/payload_mm_authvar_controlled_mode.h>
#include <boot/payload_mm_authvar_format.h>
#include <boot/payload_mm_authvar_route.h>
#include <stdint.h>
#include <string.h>

#include "payload_mm_authvar_set_preflight.h"

static bool range_valid(const void *buffer, size_t size)
{
	return size == 0U || (buffer && (uintptr_t)buffer <= UINTPTR_MAX - size);
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_address = (uintptr_t)left;
	uintptr_t right_address = (uintptr_t)right;

	if (!range_valid(left, left_size) || !range_valid(right, right_size))
		return true;
	if (!left_size || !right_size)
		return false;
	if (left_address <= right_address)
		return right_address - left_address < left_size;
	return left_address - right_address < right_size;
}

uint64_t payload_mm_authvar_set_preflight(
	const struct payload_mm_authvar_set_snapshot *snapshot,
	struct payload_mm_authvar_set_plan *plan)
{
	struct payload_mm_authvar_route_request route_request;
	struct payload_mm_authvar_route_plan route;
	struct payload_mm_authvar_auth2_view auth2;
	const struct payload_mm_authvar_policy_request *request;
	const struct payload_mm_authvar_store_index *index;
	const struct payload_mm_authvar_store_entry *existing;
	size_t entry_bytes;
	uint32_t stored_attributes;
	enum payload_mm_authvar_controlled_mode controlled_mode;
	size_t payload_size;
	uint64_t policy_status;
	uint8_t combined;
	bool append;

	if (!plan || (uintptr_t)plan % _Alignof(*plan) ||
	    !range_valid(plan, sizeof(*plan)))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (!snapshot || (uintptr_t)snapshot % _Alignof(*snapshot) ||
	    !range_valid(snapshot, sizeof(*snapshot)) ||
	    ranges_overlap(snapshot, sizeof(*snapshot), plan, sizeof(*plan)))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	request = snapshot->request;
	index = snapshot->index;
	if (!request || (uintptr_t)request % _Alignof(*request) ||
	    !range_valid(request, sizeof(*request)) || !index ||
	    (uintptr_t)index % _Alignof(*index) ||
	    !range_valid(index, sizeof(*index))) {
		memset(plan, 0, sizeof(*plan));
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	}
	if (!payload_mm_authvar_store_index_valid(index) ||
	    __builtin_mul_overflow((size_t)index->entry_count,
		sizeof(index->entries[0]), &entry_bytes)) {
		memset(plan, 0, sizeof(*plan));
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	}
	if (!range_valid(request->name, request->name_size) ||
	    !range_valid(request->data, request->data_size) ||
	    ranges_overlap(plan, sizeof(*plan), request, sizeof(*request)) ||
	    ranges_overlap(plan, sizeof(*plan), request->name, request->name_size) ||
	    ranges_overlap(plan, sizeof(*plan), request->data, request->data_size) ||
	    ranges_overlap(plan, sizeof(*plan), index, sizeof(*index)) ||
	    ranges_overlap(plan, sizeof(*plan), index->store, index->store_size) ||
	    ranges_overlap(plan, sizeof(*plan), index->entries, entry_bytes))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	memset(plan, 0, sizeof(*plan));
	if (request->name_size < 4U ||
	    request->name_size > PAYLOAD_MM_AUTHVAR_ROUTE_MAX_NAME_SIZE + 2U ||
	    (request->name_size & 1U) ||
	    ((const uint8_t *)request->name)[request->name_size - 2U] ||
	    ((const uint8_t *)request->name)[request->name_size - 1U] ||
	    request->attributes & ~PAYLOAD_MM_AUTHVAR_SET_REQUEST_ATTRIBUTES)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	for (size_t offset = 0U; offset + 2U < request->name_size; offset += 2U)
		if (!((const uint8_t *)request->name)[offset] &&
		    !((const uint8_t *)request->name)[offset + 1U])
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if ((request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS) &&
	    !(request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS))
		return request->attributes &
			PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE ?
			PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED :
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if ((request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED) ==
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE)
		return request->attributes &
			PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE ?
			PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED :
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if ((request->attributes &
		(PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE |
		 PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED)) ==
		(PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE |
		 PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED))
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	if (request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE &&
	    request->data_size != PAYLOAD_MM_AUTHVAR_COUNTER_AUTH_SIZE)
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	if (request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED &&
	    payload_mm_authvar_auth2_parse(request->data, request->data_size,
		&auth2) != PAYLOAD_MM_AUTHVAR_FORMAT_OK)
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	if (request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED)
		payload_size = auth2.payload.size;
	else if (request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE)
		payload_size = 0U;
	else
		payload_size = request->data_size;
	controlled_mode = payload_mm_authvar_controlled_mode_classify(
		request->vendor_guid, request->name, request->name_size);
	policy_status = payload_mm_authvar_controlled_mode_property(controlled_mode,
		request->attributes, payload_size);
	if (policy_status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return policy_status;
	existing = payload_mm_authvar_store_find(index, request->vendor_guid,
		request->name, request->name_size);
	if (snapshot->at_runtime && existing &&
	    !(existing->attributes & PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS))
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	stored_attributes = request->attributes &
		~PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	if (existing && request->attributes &&
	    stored_attributes != existing->attributes)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (existing && !(request->attributes &
		(PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE |
		 PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED)) &&
	    existing->attributes &
		(PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE |
		 PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED))
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	policy_status = payload_mm_authvar_controlled_mode_authorize(controlled_mode,
		snapshot->trusted_physical_presence);
	if (policy_status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return policy_status;
	if (request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE)
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	if (request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED) {
		combined = 0U;
		for (size_t i = 0U; i < sizeof(auth2.timestamp); i++)
			combined |= auth2.timestamp[i];
		if (!payload_mm_authvar_auth2_metadata_valid(&auth2) ||
		    !payload_mm_authvar_timestamp_store_valid(auth2.timestamp) ||
		    !combined)
			return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		if (controlled_mode == PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE &&
		    payload_mm_authvar_bundle_key_reserved(request->vendor_guid,
			request->name, request->name_size))
			return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
		plan->kind = PAYLOAD_MM_AUTHVAR_SET_AUTH2;
		plan->post_auth_status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		if (!(request->attributes &
			(PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			 PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)))
			plan->post_auth_status = PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
		else if (snapshot->at_runtime &&
		    (request->attributes &
			(PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			 PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)) !=
			(PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			 PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS))
			plan->post_auth_status =
				PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		else if (!(request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE))
			plan->post_auth_status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
	if (controlled_mode == PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE &&
	    payload_mm_authvar_bundle_key_reserved(request->vendor_guid,
		request->name, request->name_size))
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	memset(&route_request, 0, sizeof(route_request));
	memcpy(route_request.vendor_guid, request->vendor_guid,
		sizeof(route_request.vendor_guid));
	route_request.name = request->name;
	route_request.name_size = request->name_size - 2U;
	route_request.attributes = request->attributes;
	route_request.existing_attributes = existing ? existing->attributes : 0U;
	route_request.payload_size = request->data_size;
	route_request.target_exists = existing != NULL;
	if (payload_mm_authvar_route_plan(&route_request, &route) !=
		PAYLOAD_MM_AUTHVAR_ROUTE_OK ||
	    route.target != PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE ||
	    route.authority_count != 1U ||
	    route.authorities[0] != PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	append = request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	if (append && !request->data_size) {
		plan->kind = PAYLOAD_MM_AUTHVAR_SET_NOOP;
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
	if (!(request->attributes &
		(PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		 PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)) || !request->data_size) {
		if (!existing)
			return PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
		plan->kind = PAYLOAD_MM_AUTHVAR_SET_ORDINARY_DELETE;
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
	if (snapshot->at_runtime && request->attributes &&
	    (request->attributes &
		(PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		 PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)) !=
		(PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		 PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (request->attributes &&
	    !(request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE))
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	if (!request->attributes)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	plan->kind = PAYLOAD_MM_AUTHVAR_SET_ORDINARY_WRITE;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}
