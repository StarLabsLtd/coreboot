/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_private_trust.h>
#include <boot/payload_mm_authvar_trust_store.h>
#include <commonlib/helpers.h>
#include <string.h>

#include "payload_mm_authvar_authority_provider.h"

#if CONFIG(PAYLOAD_MM_AUTHVAR_AUTHORITY_PROVIDER) && \
	CONFIG(PAYLOAD_MM_AUTHVAR_REQUIRE_SELF_SIGNED_PK)
#error "The authvar authority provider lacks NEW_PAYLOAD_CERT trust"
#endif

struct provider_snapshot {
	struct payload_mm_authvar_authority_verify_request request;
	struct payload_mm_crypto_span pkcs7;
	struct payload_mm_crypto_span content[
		PAYLOAD_MM_AUTHVAR_AUTHORITY_SIGNED_SPANS];
	struct payload_mm_crypto_span new_payload;
	struct payload_mm_authvar_store_index index;
	struct payload_mm_authvar_route_plan route;
};

static bool range_valid(const void *data, size_t size)
{
	return size == 0U ||
		(data && (uintptr_t)data <= UINTPTR_MAX - (size - 1U));
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	const uintptr_t left_address = (uintptr_t)left;
	const uintptr_t right_address = (uintptr_t)right;

	if (!range_valid(left, left_size) || !range_valid(right, right_size))
		return true;
	if (!left_size || !right_size)
		return false;
	return left_address <= right_address + right_size - 1U &&
		right_address <= left_address + left_size - 1U;
}

static bool outer_spans_disjoint(
	const struct payload_mm_authvar_authority_verify_request *request,
	const struct payload_mm_authvar_authority_verification *verification,
	const struct provider_snapshot *snapshot)
{
	const void *descriptors[] = {
		request, snapshot->request.owner, snapshot->request.pkcs7,
		snapshot->request.content, snapshot->request.index,
		snapshot->request.route, snapshot->request.new_payload, verification,
	};
	const size_t descriptor_sizes[] = {
		sizeof(*request), sizeof(*snapshot->request.owner),
		sizeof(*snapshot->request.pkcs7), sizeof(snapshot->content),
		sizeof(*snapshot->request.index), sizeof(*snapshot->request.route),
		sizeof(*snapshot->request.new_payload), sizeof(*verification),
	};
	const void *data[PAYLOAD_MM_AUTHVAR_AUTHORITY_SIGNED_SPANS + 3U];
	size_t data_sizes[PAYLOAD_MM_AUTHVAR_AUTHORITY_SIGNED_SPANS + 3U];
	size_t entries_size;

	if (__builtin_mul_overflow((size_t)snapshot->index.entry_count,
		sizeof(snapshot->index.entries[0]), &entries_size))
		return false;
	data[0] = snapshot->pkcs7.data;
	data_sizes[0] = snapshot->pkcs7.size;
	data[1] = snapshot->index.store;
	data_sizes[1] = snapshot->index.store_size;
	data[2] = snapshot->index.entries;
	data_sizes[2] = entries_size;
	for (size_t i = 0U; i < ARRAY_SIZE(snapshot->content); i++) {
		data[i + 3U] = snapshot->content[i].data;
		data_sizes[i + 3U] = snapshot->content[i].size;
	}
	for (size_t descriptor = 0U; descriptor < ARRAY_SIZE(descriptors);
	     descriptor++) {
		if (!range_valid(descriptors[descriptor],
			descriptor_sizes[descriptor]))
			return false;
		for (size_t other = descriptor + 1U;
		     other < ARRAY_SIZE(descriptors); other++)
			if (ranges_overlap(descriptors[descriptor],
				descriptor_sizes[descriptor], descriptors[other],
				descriptor_sizes[other]))
				return false;
		for (size_t bytes = 0U; bytes < ARRAY_SIZE(data); bytes++)
			if (ranges_overlap(descriptors[descriptor],
				descriptor_sizes[descriptor], data[bytes],
				data_sizes[bytes]))
				return false;
	}
	for (size_t left = 0U; left < ARRAY_SIZE(data); left++)
		for (size_t right = left + 1U; right < ARRAY_SIZE(data); right++)
			if (ranges_overlap(data[left], data_sizes[left], data[right],
				data_sizes[right]))
				return false;
	return true;
}

static bool trust_route(const struct provider_snapshot *snapshot)
{
	const struct payload_mm_authvar_route_plan *route = &snapshot->route;

	if (!route || route->authority_count == 0U ||
	    route->authority_count > PAYLOAD_MM_AUTHVAR_ROUTE_MAX_AUTHORITIES ||
	    !route->require_signature_list || route->enter_user_mode ||
	    route->mark_vendor_keys_modified)
		return false;
	if (route->target == PAYLOAD_MM_AUTHVAR_TARGET_PK)
		return route->authority_count == 1U &&
			route->authorities[0] ==
				PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK &&
			route->authorities[1] == PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE &&
			route->enter_setup_mode == !snapshot->new_payload.size;
	if (route->target == PAYLOAD_MM_AUTHVAR_TARGET_KEK)
		return route->authority_count == 1U &&
			route->authorities[0] ==
				PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK &&
			route->authorities[1] == PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE &&
			!route->enter_setup_mode;
	if (route->target == PAYLOAD_MM_AUTHVAR_TARGET_DB ||
	    route->target == PAYLOAD_MM_AUTHVAR_TARGET_DBX ||
	    route->target == PAYLOAD_MM_AUTHVAR_TARGET_DBT)
		return route->authority_count == 2U &&
			route->authorities[0] ==
				PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK &&
			route->authorities[1] ==
				PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK &&
			!route->enter_setup_mode;
	return false;
}

static bool payload_cert_route(
	const struct payload_mm_authvar_route_plan *route)
{
	return route && route->target == PAYLOAD_MM_AUTHVAR_TARGET_PK &&
		route->authority_count == 1U &&
		route->authorities[0] ==
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PAYLOAD_CERT &&
		route->authorities[1] == PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE &&
		route->require_signature_list && route->enter_user_mode &&
		!route->enter_setup_mode && !route->mark_vendor_keys_modified;
}

static bool inputs_valid(
	const struct payload_mm_authvar_authority_verify_request *request,
	const struct payload_mm_authvar_authority_verification *verification,
	struct provider_snapshot *snapshot)
{
	if (!request || !verification ||
	    (uintptr_t)request % _Alignof(*request) ||
	    (uintptr_t)verification % _Alignof(*verification) ||
	    !range_valid(request, sizeof(*request)) ||
	    ranges_overlap(request, sizeof(*request), verification,
		sizeof(*verification)))
		return false;
	snapshot->request = *request;
	if (!snapshot->request.owner || !snapshot->request.pkcs7 ||
	    !snapshot->request.content || !snapshot->request.index ||
	    !snapshot->request.route || !snapshot->request.new_payload ||
	    (uintptr_t)snapshot->request.owner %
		_Alignof(*snapshot->request.owner) ||
	    (uintptr_t)snapshot->request.pkcs7 %
		_Alignof(*snapshot->request.pkcs7) ||
	    (uintptr_t)snapshot->request.content %
		_Alignof(*snapshot->request.content) ||
	    (uintptr_t)snapshot->request.index %
		_Alignof(*snapshot->request.index) ||
	    (uintptr_t)snapshot->request.route %
		_Alignof(*snapshot->request.route) ||
	    (uintptr_t)snapshot->request.new_payload %
		_Alignof(*snapshot->request.new_payload) ||
	    snapshot->request.content_count !=
		PAYLOAD_MM_AUTHVAR_AUTHORITY_SIGNED_SPANS ||
	    !range_valid(snapshot->request.owner,
		sizeof(*snapshot->request.owner)) ||
	    !range_valid(snapshot->request.pkcs7,
		sizeof(*snapshot->request.pkcs7)) ||
	    !range_valid(snapshot->request.content, sizeof(snapshot->content)) ||
	    !range_valid(snapshot->request.index,
		sizeof(*snapshot->request.index)) ||
	    !range_valid(snapshot->request.route,
		sizeof(*snapshot->request.route)) ||
	    !range_valid(snapshot->request.new_payload,
		sizeof(*snapshot->request.new_payload)))
		return false;
	snapshot->pkcs7 = *snapshot->request.pkcs7;
	memcpy(snapshot->content, snapshot->request.content,
		sizeof(snapshot->content));
	snapshot->index = *snapshot->request.index;
	snapshot->route = *snapshot->request.route;
	snapshot->new_payload = *snapshot->request.new_payload;
	if (snapshot->content[4].data != snapshot->new_payload.data ||
	    snapshot->content[4].size != snapshot->new_payload.size)
		return false;
	return outer_spans_disjoint(request, verification, snapshot);
}

static bool private_route(const struct payload_mm_authvar_route_plan *route)
{
	if (!route || route->target != PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE ||
	    route->authority_count != 1U ||
	    route->authorities[1] != PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE ||
	    route->require_signature_list || route->enter_user_mode ||
	    route->enter_setup_mode || route->mark_vendor_keys_modified)
		return false;
	return route->authorities[0] ==
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER ||
		route->authorities[0] ==
			PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
}

static enum payload_mm_verify_status private_provider_status(
	enum payload_mm_verify_status status)
{
	switch (status) {
	case PAYLOAD_MM_VERIFY_OK:
	case PAYLOAD_MM_VERIFY_INVALID:
	case PAYLOAD_MM_VERIFY_BUSY:
	case PAYLOAD_MM_VERIFY_INTERNAL:
	case PAYLOAD_MM_VERIFY_CHANGED:
		return status;
	case PAYLOAD_MM_VERIFY_MALFORMED:
	case PAYLOAD_MM_VERIFY_REJECTED:
	case PAYLOAD_MM_VERIFY_NO_MEMORY:
	case PAYLOAD_MM_VERIFY_UNSUPPORTED:
		return PAYLOAD_MM_VERIFY_REJECTED;
	default:
		return PAYLOAD_MM_VERIFY_INTERNAL;
	}
}

static bool bytes_zero(const uint8_t *bytes, size_t size)
{
	uint8_t value = 0U;

	while (size--)
		value |= *bytes++;
	return value == 0U;
}

static bool snapshot_unchanged(
	const struct payload_mm_authvar_authority_verify_request *request,
	const struct provider_snapshot *snapshot)
{
	return !memcmp(request, &snapshot->request, sizeof(snapshot->request)) &&
		!memcmp(request->pkcs7, &snapshot->pkcs7,
			sizeof(snapshot->pkcs7)) &&
		!memcmp(request->content, snapshot->content,
			sizeof(snapshot->content)) &&
		!memcmp(request->index, &snapshot->index,
			sizeof(snapshot->index)) &&
		!memcmp(request->route, &snapshot->route,
			sizeof(snapshot->route)) &&
		!memcmp(request->new_payload, &snapshot->new_payload,
			sizeof(snapshot->new_payload));
}

static enum payload_mm_verify_status private_profile(
	const struct provider_snapshot *snapshot)
{
	const uint8_t *attributes;
	uint32_t value;

	if (snapshot->content[2].size != sizeof(value))
		return PAYLOAD_MM_VERIFY_INVALID;
	attributes = snapshot->content[2].data;
	value = (uint32_t)attributes[0] | (uint32_t)attributes[1] << 8 |
		(uint32_t)attributes[2] << 16 | (uint32_t)attributes[3] << 24;
	return value & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE ?
		PAYLOAD_MM_VERIFY_OK : PAYLOAD_MM_VERIFY_REJECTED;
}

static bool private_result_valid(
	const struct payload_mm_authvar_authority_verify_request *request,
	const struct payload_mm_authvar_authority_verification *verification)
{
	size_t binding_size = verification->new_binding_size;
	bool new_signer = request->route->authorities[0] ==
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;

	if (verification->accepted_authority != request->route->authorities[0] ||
	    (binding_size != 0U && binding_size != 32U && binding_size != 48U &&
	     binding_size != 64U) ||
	    !bytes_zero(verification->new_binding + binding_size,
		sizeof(verification->new_binding) - binding_size))
		return false;
	if (!new_signer)
		return binding_size == 0U;
	return request->new_payload->size ? binding_size != 0U :
		binding_size == 0U;
}

enum payload_mm_verify_status payload_mm_authvar_authority_provider_verify(
	void *context,
	const struct payload_mm_authvar_authority_verify_request *request,
	struct payload_mm_authvar_authority_verification *verification)
{
	enum payload_mm_authvar_authority accepted =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE;
	struct payload_mm_authvar_authority_verification draft = { 0 };
	struct provider_snapshot snapshot;
	enum payload_mm_verify_status status;

	if (!verification ||
	    (uintptr_t)verification % _Alignof(*verification) ||
	    !range_valid(verification, sizeof(*verification)))
		return PAYLOAD_MM_VERIFY_INVALID;
	if (!request) {
		memset(verification, 0, sizeof(*verification));
		return PAYLOAD_MM_VERIFY_INVALID;
	}
	if (!inputs_valid(request, verification, &snapshot))
		return PAYLOAD_MM_VERIFY_INVALID;
	memset(verification, 0, sizeof(*verification));
	if (context)
		return PAYLOAD_MM_VERIFY_INVALID;
	if (!payload_mm_authvar_store_index_valid(request->index))
		return PAYLOAD_MM_VERIFY_INVALID;
	if (private_route(&snapshot.route)) {
		status = private_profile(&snapshot);
		if (status != PAYLOAD_MM_VERIFY_OK)
			return status;
		status = payload_mm_authvar_private_trust_verify(request->owner,
			request->pkcs7, request->content, request->content_count,
			request->index, request->route, &draft);
		status = private_provider_status(status);
		if (!snapshot_unchanged(request, &snapshot))
			status = PAYLOAD_MM_VERIFY_CHANGED;
		if (status == PAYLOAD_MM_VERIFY_OK &&
		    !private_result_valid(request, &draft))
			status = PAYLOAD_MM_VERIFY_INTERNAL;
		if (status == PAYLOAD_MM_VERIFY_OK)
			*verification = draft;
		memset(&draft, 0, sizeof(draft));
		return status;
	}
	if (!trust_route(&snapshot))
		return payload_cert_route(&snapshot.route) ?
			PAYLOAD_MM_VERIFY_UNSUPPORTED : PAYLOAD_MM_VERIFY_INVALID;
	status = payload_mm_authvar_trust_store_verify(request->owner,
		request->pkcs7, request->content, request->content_count,
		request->index, request->route, &accepted);
	if (!snapshot_unchanged(request, &snapshot))
		status = PAYLOAD_MM_VERIFY_CHANGED;
	if (status == PAYLOAD_MM_VERIFY_OK && accepted !=
		request->route->authorities[0] &&
	    (request->route->authority_count != 2U || accepted !=
		request->route->authorities[1]))
		status = PAYLOAD_MM_VERIFY_INTERNAL;
	if (status == PAYLOAD_MM_VERIFY_OK)
		verification->accepted_authority = accepted;
	return status;
}
