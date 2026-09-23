/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_authority.h>
#include <boot/payload_mm_authvar_format.h>
#include <boot/payload_mm_authvar_signature_db.h>

#include <commonlib/helpers.h>
#include <string.h>

#include "payload_mm_crypto/crypto.h"

struct protected_digest {
	uint8_t name[PAYLOAD_MM_SHA256_SIZE];
	uint8_t data[PAYLOAD_MM_SHA256_SIZE];
	uint8_t store[PAYLOAD_MM_SHA256_SIZE];
	uint8_t entries[PAYLOAD_MM_SHA256_SIZE];
	uint8_t workspace[PAYLOAD_MM_SHA256_SIZE];
};

static bool range_valid(const void *data, size_t size)
{
	return size == 0U ||
		(data && (uintptr_t)data <= UINTPTR_MAX - size);
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

static int timestamp_compare(const uint8_t left[16], const uint8_t right[16])
{
	static const uint8_t fields[] = { 0U, 2U, 3U, 4U, 5U, 6U };
	uint16_t left_year = (uint16_t)left[0] | (uint16_t)left[1] << 8;
	uint16_t right_year = (uint16_t)right[0] | (uint16_t)right[1] << 8;

	if (left_year != right_year)
		return left_year < right_year ? -1 : 1;
	for (size_t i = 1U; i < sizeof(fields); i++) {
		uint8_t offset = fields[i];

		if (left[offset] != right[offset])
			return left[offset] < right[offset] ? -1 : 1;
	}
	return 0;
}

static bool bytes_are_zero(const uint8_t *bytes, size_t size)
{
	uint8_t combined = 0U;

	while (size--)
		combined |= *bytes++;
	return combined == 0U;
}

static enum payload_mm_verify_status protected_digest(
	const struct payload_mm_authvar_policy_request *request,
	const struct payload_mm_authvar_store_index *index,
	const void *workspace, size_t workspace_size,
	struct protected_digest *digest)
{
	enum payload_mm_verify_status status;
	size_t entries_size = (size_t)index->entry_count *
		sizeof(index->entries[0]);

	status = payload_mm_sha256(request->name, request->name_size, digest->name);
	if (status == PAYLOAD_MM_VERIFY_OK)
		status = payload_mm_sha256(request->data, request->data_size,
			digest->data);
	if (status == PAYLOAD_MM_VERIFY_OK)
		status = payload_mm_sha256(index->store, index->store_size,
			digest->store);
	if (status == PAYLOAD_MM_VERIFY_OK)
		status = payload_mm_sha256(index->entries, entries_size,
			digest->entries);
	if (status == PAYLOAD_MM_VERIFY_OK)
		status = payload_mm_sha256(workspace, workspace_size,
			digest->workspace);
	return status;
}

static bool inputs_valid(const struct payload_mm_authvar_authority_snapshot *snapshot,
	const struct payload_mm_authvar_authority_decision *decision)
{
	const struct payload_mm_authvar_policy_request *request;
	const void *parts[10];
	size_t sizes[10];
	size_t entries_size;

	if (!snapshot || !decision || (uintptr_t)snapshot % _Alignof(*snapshot) ||
	    (uintptr_t)decision % _Alignof(*decision) ||
	    !range_valid(snapshot, sizeof(*snapshot)) ||
	    !range_valid(decision, sizeof(*decision)) ||
	    ranges_overlap(snapshot, sizeof(*snapshot), decision, sizeof(*decision)))
		return false;
	request = snapshot->request;
	if (!request || !payload_mm_authvar_store_index_valid(snapshot->index) ||
	    !snapshot->verify ||
	    (uintptr_t)request % _Alignof(*request) ||
	    (uintptr_t)snapshot->owner % _Alignof(*snapshot->owner) ||
	    !range_valid(request, sizeof(*request)) ||
	    !range_valid(snapshot->owner, sizeof(*snapshot->owner)) ||
	    !range_valid(request->name, request->name_size) ||
	    !range_valid(request->data, request->data_size) ||
	    !range_valid(snapshot->append_workspace, snapshot->append_workspace_size))
		return false;
	entries_size = (size_t)snapshot->index->entry_count *
		sizeof(snapshot->index->entries[0]);
	parts[0] = snapshot;
	sizes[0] = sizeof(*snapshot);
	parts[1] = decision;
	sizes[1] = sizeof(*decision);
	parts[2] = request;
	sizes[2] = sizeof(*request);
	parts[3] = snapshot->index;
	sizes[3] = sizeof(*snapshot->index);
	parts[4] = snapshot->owner;
	sizes[4] = sizeof(*snapshot->owner);
	parts[5] = request->name;
	sizes[5] = request->name_size;
	parts[6] = request->data;
	sizes[6] = request->data_size;
	parts[7] = snapshot->index->store;
	sizes[7] = snapshot->index->store_size;
	parts[8] = snapshot->index->entries;
	sizes[8] = entries_size;
	parts[9] = snapshot->append_workspace;
	sizes[9] = snapshot->append_workspace_size;
	for (size_t left = 0U; left < ARRAY_SIZE(parts); left++)
		for (size_t right = left + 1U;
		     right < ARRAY_SIZE(parts); right++)
			if (ranges_overlap(parts[left], sizes[left], parts[right],
				sizes[right]))
				return false;
	return request->operation == PAYLOAD_MM_AUTHVAR_SERVICE_SET &&
		request->name_size >= sizeof(uint16_t) &&
		request->name_size <= PAYLOAD_MM_AUTHVAR_ROUTE_MAX_NAME_SIZE +
			sizeof(uint16_t) &&
		request->data_size <= PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_DATA_SIZE;
}

static bool existing_timestamp(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *entry, const uint8_t **timestamp)
{
	if (!entry) {
		*timestamp = NULL;
		return true;
	}
	if (!index || !index->store || entry->record_offset > index->used_size ||
	    index->used_size - entry->record_offset <
		PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE)
		return false;
	*timestamp = index->store + entry->record_offset + 16U;
	return true;
}

static uint32_t route_intents(const struct payload_mm_authvar_route_plan *route,
	bool deleting)
{
	uint32_t intents = 0U;

	if (route->enter_user_mode && !deleting)
		intents |= PAYLOAD_MM_AUTHVAR_INTENT_ENTER_USER_MODE;
	if (route->enter_setup_mode && deleting)
		intents |= PAYLOAD_MM_AUTHVAR_INTENT_ENTER_SETUP_MODE;
	if (route->mark_vendor_keys_modified)
		intents |= PAYLOAD_MM_AUTHVAR_INTENT_MARK_VENDOR_KEYS;
	return intents;
}

enum payload_mm_verify_status payload_mm_authvar_authority_decide(
	const struct payload_mm_authvar_authority_snapshot *snapshot,
	struct payload_mm_authvar_authority_decision *decision)
{
	struct payload_mm_authvar_authority_decision draft = { 0 };
	struct payload_mm_authvar_authority_snapshot snapshot_copy;
	struct payload_mm_authvar_authority_snapshot snapshot_descriptor;
	struct payload_mm_authvar_policy_request request_copy;
	struct payload_mm_authvar_store_index index_copy;
	struct payload_mm_authvar_auth2_view auth2;
	struct payload_mm_authvar_route_request route_request;
	struct payload_mm_authvar_route_plan route;
	struct payload_mm_authvar_route_plan route_descriptor;
	struct payload_mm_authvar_authority_verify_request verify_request;
	struct payload_mm_authvar_authority_verify_request verify_descriptor;
	struct payload_mm_crypto_span content[PAYLOAD_MM_AUTHVAR_AUTHORITY_SIGNED_SPANS];
	struct payload_mm_crypto_span content_descriptors[
		PAYLOAD_MM_AUTHVAR_AUTHORITY_SIGNED_SPANS];
	struct payload_mm_crypto_span pkcs7;
	struct payload_mm_crypto_span pkcs7_descriptor;
	struct payload_mm_crypto_span new_payload;
	struct payload_mm_crypto_span new_payload_descriptor;
	struct payload_mm_authvar_auth2_view auth2_descriptor;
	struct protected_digest before_digest;
	struct protected_digest after_digest;
	const struct payload_mm_authvar_policy_request *request;
	const struct payload_mm_authvar_store_entry *existing;
	const uint8_t *old_timestamp;
	uint8_t attributes_le[4];
	uint8_t attributes_descriptor[4];
	uint8_t zero_timestamp[16] = { 0 };
	size_t filtered_size = 0U;
	bool append;
	bool deleting;
	enum payload_mm_authvar_authority accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE;
	enum payload_mm_verify_status status;
	enum payload_mm_verify_status callback_status;
	enum payload_mm_authvar_route_result route_status;
	const struct payload_mm_authvar_authority_snapshot *original_snapshot =
		snapshot;
	const struct payload_mm_authvar_policy_request *original_request;
	const struct payload_mm_authvar_store_index *original_index;

	if (!inputs_valid(snapshot, decision))
		return PAYLOAD_MM_VERIFY_INVALID;
	memset(decision, 0, sizeof(*decision));
	snapshot_descriptor = *snapshot;
	snapshot_copy = snapshot_descriptor;
	original_request = snapshot_copy.request;
	original_index = snapshot_copy.index;
	request_copy = *original_request;
	index_copy = *original_index;
	snapshot_copy.request = &request_copy;
	snapshot_copy.index = &index_copy;
	snapshot = &snapshot_copy;
	request = &request_copy;
	if (!(request->attributes & PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH) ||
	    request->attributes & PAYLOAD_MM_AUTHVAR_ATTRIBUTE_AUTH_WRITE)
		return PAYLOAD_MM_VERIFY_UNSUPPORTED;
	if (request->name_size < sizeof(uint16_t) ||
	    memcmp((const uint8_t *)request->name + request->name_size - 2U,
		zero_timestamp, 2U))
		return PAYLOAD_MM_VERIFY_INVALID;
	if (payload_mm_authvar_auth2_parse(request->data, request->data_size,
		&auth2) != PAYLOAD_MM_AUTHVAR_FORMAT_OK ||
	    !payload_mm_authvar_auth2_metadata_valid(&auth2) ||
	    !payload_mm_authvar_timestamp_store_valid(auth2.timestamp) ||
	    bytes_are_zero(auth2.timestamp, sizeof(auth2.timestamp)))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	existing = payload_mm_authvar_store_find(snapshot->index,
		request->vendor_guid, request->name, request->name_size);
	if (!existing_timestamp(snapshot->index, existing, &old_timestamp))
		return PAYLOAD_MM_VERIFY_INVALID;
	route_request = (struct payload_mm_authvar_route_request) {
		.name = request->name,
		.name_size = request->name_size - sizeof(uint16_t),
		.attributes = request->attributes,
		.existing_attributes = existing ? existing->attributes : 0U,
		.payload_size = auth2.payload.size,
		.target_exists = existing != NULL,
		.setup_mode = snapshot->facts.setup_mode,
		.custom_mode = snapshot->facts.custom_mode,
		.trusted_physical_presence =
			snapshot->facts.trusted_physical_presence,
		.require_self_signed_pk = snapshot->facts.require_self_signed_pk,
	};
	memcpy(route_request.vendor_guid, request->vendor_guid,
		sizeof(route_request.vendor_guid));
	route_status = payload_mm_authvar_route_plan(&route_request, &route);
	if (route_status != PAYLOAD_MM_AUTHVAR_ROUTE_OK)
		return route_status == PAYLOAD_MM_AUTHVAR_ROUTE_UNSUPPORTED ?
			PAYLOAD_MM_VERIFY_UNSUPPORTED :
			route_status == PAYLOAD_MM_AUTHVAR_ROUTE_INVALID ?
			PAYLOAD_MM_VERIFY_MALFORMED : PAYLOAD_MM_VERIFY_REJECTED;
	append = request->attributes & PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND;
	if (existing && existing->attributes !=
	    (request->attributes & ~PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND))
		return PAYLOAD_MM_VERIFY_REJECTED;
	if (append && route.target == PAYLOAD_MM_AUTHVAR_TARGET_PK)
		return PAYLOAD_MM_VERIFY_REJECTED;
	if (old_timestamp && !append &&
	    timestamp_compare(auth2.timestamp, old_timestamp) <= 0)
		return PAYLOAD_MM_VERIFY_REJECTED;
	deleting = existing && !append && !auth2.payload.size;
	attributes_le[0] = request->attributes;
	attributes_le[1] = request->attributes >> 8;
	attributes_le[2] = request->attributes >> 16;
	attributes_le[3] = request->attributes >> 24;
	content[0] = (struct payload_mm_crypto_span) {
		request->name, request->name_size - sizeof(uint16_t),
	};
	content[1] = (struct payload_mm_crypto_span) {
		request->vendor_guid, sizeof(request->vendor_guid),
	};
	content[2] = (struct payload_mm_crypto_span) {
		attributes_le, sizeof(attributes_le),
	};
	content[3] = (struct payload_mm_crypto_span) {
		auth2.timestamp, sizeof(auth2.timestamp),
	};
	content[4] = (struct payload_mm_crypto_span) {
		auth2.payload.data, auth2.payload.size,
	};
	pkcs7 = (struct payload_mm_crypto_span) {
		auth2.pkcs7.data, auth2.pkcs7.size,
	};
	new_payload = (struct payload_mm_crypto_span) {
		auth2.payload.data, auth2.payload.size,
	};
	verify_request = (struct payload_mm_authvar_authority_verify_request) {
		.owner = snapshot->owner,
		.pkcs7 = &pkcs7,
		.content = content,
		.content_count = PAYLOAD_MM_AUTHVAR_AUTHORITY_SIGNED_SPANS,
		.index = snapshot->index,
		.route = &route,
		.new_payload = &new_payload,
	};
	route_descriptor = route;
	verify_descriptor = verify_request;
	memcpy(content_descriptors, content, sizeof(content_descriptors));
	pkcs7_descriptor = pkcs7;
	new_payload_descriptor = new_payload;
	auth2_descriptor = auth2;
	memcpy(attributes_descriptor, attributes_le, sizeof(attributes_descriptor));
	if (route.authorities[0] != PAYLOAD_MM_AUTHVAR_AUTHORITY_BYPASS) {
		status = protected_digest(request, snapshot->index,
			snapshot->append_workspace, snapshot->append_workspace_size,
			&before_digest);
		if (status != PAYLOAD_MM_VERIFY_OK)
			return status;
		callback_status = snapshot->verify(snapshot->verify_context, &verify_request,
			&accepted_authority);
		status = protected_digest(request, snapshot->index,
			snapshot->append_workspace, snapshot->append_workspace_size,
			&after_digest);
		if (status != PAYLOAD_MM_VERIFY_OK)
			return status;
		if (memcmp(original_snapshot, &snapshot_descriptor,
			sizeof(snapshot_descriptor)) ||
		    memcmp(original_request, &request_copy, sizeof(request_copy)) ||
		    memcmp(original_index, &index_copy, sizeof(index_copy)) ||
		    memcmp(&before_digest, &after_digest, sizeof(before_digest)) ||
		    memcmp(&route, &route_descriptor, sizeof(route)) ||
		    memcmp(&verify_request, &verify_descriptor,
			sizeof(verify_request)) ||
		    memcmp(content, content_descriptors, sizeof(content)) ||
		    memcmp(&pkcs7, &pkcs7_descriptor, sizeof(pkcs7)) ||
		    memcmp(&new_payload, &new_payload_descriptor,
			sizeof(new_payload)) ||
		    memcmp(&auth2, &auth2_descriptor, sizeof(auth2)) ||
		    memcmp(attributes_le, attributes_descriptor,
			sizeof(attributes_le)) ||
		    !bytes_are_zero((const uint8_t *)decision, sizeof(*decision)))
			return PAYLOAD_MM_VERIFY_CHANGED;
		if (callback_status != PAYLOAD_MM_VERIFY_OK)
			return callback_status;
		for (uint8_t i = 0U; i < route.authority_count; i++)
			if (accepted_authority == route.authorities[i])
				goto authority_verified;
		return PAYLOAD_MM_VERIFY_REJECTED;
	}
	accepted_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_BYPASS;

authority_verified:
	if (route.require_signature_list && auth2.payload.size) {
		enum payload_mm_authvar_signature_db_profile profile =
			route.target == PAYLOAD_MM_AUTHVAR_TARGET_PK ?
			PAYLOAD_MM_AUTHVAR_PLATFORM_KEY :
			PAYLOAD_MM_AUTHVAR_SIGNATURE_DB;

		status = payload_mm_authvar_signature_db_validate(snapshot->owner,
			auth2.payload.data, auth2.payload.size, profile,
			PAYLOAD_MM_AUTHVAR_SIGNATURE_DB_MAX_X509, NULL);
		if (status != PAYLOAD_MM_VERIFY_OK)
			return status;
	}

	draft.target = route.target;
	draft.accepted_authority = accepted_authority;
	/* EDK2 authenticates this request, then maps absence to EFI_NOT_FOUND. */
	if (!append && !existing && !auth2.payload.size) {
		draft.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND;
		*decision = draft;
		return PAYLOAD_MM_VERIFY_OK;
	}
	/* Empty APPEND to an absent target creates nothing and changes no state. */
	if (append && !existing && !auth2.payload.size) {
		draft.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP;
		*decision = draft;
		return PAYLOAD_MM_VERIFY_OK;
	}
	draft.intents = route_intents(&route, deleting);
	draft.mutation.attributes = request->attributes &
		~PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND;
	memcpy(draft.mutation.timestamp, auth2.timestamp,
		sizeof(draft.mutation.timestamp));
	if (append && old_timestamp &&
	    timestamp_compare(draft.mutation.timestamp, old_timestamp) < 0)
		memcpy(draft.mutation.timestamp, old_timestamp,
			sizeof(draft.mutation.timestamp));
	if (deleting) {
		memset(&draft.mutation, 0, sizeof(draft.mutation));
		draft.mutation.kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE;
		draft.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION;
		if (route.target == PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE)
			draft.intents |= PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING;
		*decision = draft;
		return PAYLOAD_MM_VERIFY_OK;
	}
	draft.mutation.kind = PAYLOAD_MM_AUTHVAR_MUTATION_WRITE;
	draft.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION;
	draft.data = auth2.payload.data;
	draft.data_size = auth2.payload.size;
	if (append && existing) {
		const void *old_data = payload_mm_authvar_store_data(snapshot->index,
			existing);
		size_t appended_size = auth2.payload.size;

		if (!old_data || existing->data_size > snapshot->append_workspace_size)
			return PAYLOAD_MM_VERIFY_NO_MEMORY;
		if (route.require_signature_list && auth2.payload.size) {
			status = payload_mm_authvar_signature_db_filter_append(
				snapshot->owner, old_data, existing->data_size,
				auth2.payload.data, auth2.payload.size,
				(uint8_t *)snapshot->append_workspace +
					existing->data_size,
				snapshot->append_workspace_size - existing->data_size,
				&filtered_size);
			if (status != PAYLOAD_MM_VERIFY_OK)
				return status;
			appended_size = filtered_size;
		}
		if (appended_size > snapshot->append_workspace_size -
		    existing->data_size)
			return PAYLOAD_MM_VERIFY_NO_MEMORY;
		if (!route.require_signature_list && appended_size) {
			memcpy((uint8_t *)snapshot->append_workspace + existing->data_size,
				auth2.payload.data, appended_size);
		}
		memcpy(snapshot->append_workspace, old_data, existing->data_size);
		draft.data = snapshot->append_workspace;
		draft.data_size = existing->data_size + appended_size;
	}
	if (!existing && route.target == PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE)
		draft.intents |= PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
	if (draft.data_size > UINT32_MAX)
		return PAYLOAD_MM_VERIFY_UNSUPPORTED;
	draft.mutation.data_size = draft.data_size;
	*decision = draft;
	return PAYLOAD_MM_VERIFY_OK;
}
