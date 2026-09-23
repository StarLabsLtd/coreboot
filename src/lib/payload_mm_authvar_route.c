/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_route.h>
#include <stdint.h>
#include <string.h>

static const uint8_t global_variable_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};

static const uint8_t image_security_database_guid[16] = {
	0xcb, 0xb2, 0x19, 0xd7, 0x3a, 0x3d, 0x96, 0x45,
	0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f,
};

static const uint8_t pk_name[] = { 'P', 0, 'K', 0 };
static const uint8_t kek_name[] = { 'K', 0, 'E', 0, 'K', 0 };
static const uint8_t db_name[] = { 'd', 0, 'b', 0 };
static const uint8_t dbx_name[] = { 'd', 0, 'b', 0, 'x', 0 };
static const uint8_t dbt_name[] = { 'd', 0, 'b', 0, 't', 0 };

static bool range_valid(const void *data, size_t size)
{
	return size == 0U ||
		(data && (uintptr_t)data <= (uintptr_t)-1 - size);
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
	return left_address < right_address + right_size &&
		right_address < left_address + left_size;
}

static bool name_is(const struct payload_mm_authvar_route_request *request,
	const uint8_t *name, size_t name_size)
{
	return request->name_size == name_size &&
		!memcmp(request->name, name, name_size);
}

static enum payload_mm_authvar_target classify_target(
	const struct payload_mm_authvar_route_request *request)
{
	if (!memcmp(request->vendor_guid, global_variable_guid,
		sizeof(global_variable_guid))) {
		if (name_is(request, pk_name, sizeof(pk_name)))
			return PAYLOAD_MM_AUTHVAR_TARGET_PK;
		if (name_is(request, kek_name, sizeof(kek_name)))
			return PAYLOAD_MM_AUTHVAR_TARGET_KEK;
	} else if (!memcmp(request->vendor_guid, image_security_database_guid,
		sizeof(image_security_database_guid))) {
		if (name_is(request, db_name, sizeof(db_name)))
			return PAYLOAD_MM_AUTHVAR_TARGET_DB;
		if (name_is(request, dbx_name, sizeof(dbx_name)))
			return PAYLOAD_MM_AUTHVAR_TARGET_DBX;
		if (name_is(request, dbt_name, sizeof(dbt_name)))
			return PAYLOAD_MM_AUTHVAR_TARGET_DBT;
	}
	return PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE;
}

static bool canonical_name(const void *name, size_t size)
{
	const uint8_t *bytes = name;

	if (!range_valid(name, size) || !size ||
	    size > PAYLOAD_MM_AUTHVAR_ROUTE_MAX_NAME_SIZE ||
	    size % sizeof(uint16_t))
		return false;
	for (size_t offset = 0U; offset < size; offset += sizeof(uint16_t))
		if (!bytes[offset] && !bytes[offset + 1U])
			return false;
	return true;
}

static enum payload_mm_authvar_route_result route_private(
	const struct payload_mm_authvar_route_request *request,
	struct payload_mm_authvar_route_plan *plan)
{
	if (request->attributes & PAYLOAD_MM_AUTHVAR_ATTRIBUTE_AUTH_WRITE)
		return PAYLOAD_MM_AUTHVAR_ROUTE_UNSUPPORTED;
	if (request->attributes & PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH) {
		plan->authorities[0] = request->target_exists ?
			PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB :
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
		plan->authority_count = 1U;
		return PAYLOAD_MM_AUTHVAR_ROUTE_OK;
	}
	if (request->target_exists &&
	    request->existing_attributes &
		(PAYLOAD_MM_AUTHVAR_ATTRIBUTE_AUTH_WRITE |
		 PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH))
		return PAYLOAD_MM_AUTHVAR_ROUTE_WRITE_PROTECTED;
	plan->authorities[0] = PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE;
	plan->authority_count = 1U;
	return PAYLOAD_MM_AUTHVAR_ROUTE_OK;
}

static enum payload_mm_authvar_route_result route_secure_boot(
	const struct payload_mm_authvar_route_request *request,
	struct payload_mm_authvar_route_plan *plan)
{
	const uint32_t required = PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH;
	bool bypass;
	bool deleting;

	if ((request->attributes & required) != required ||
	    request->attributes & PAYLOAD_MM_AUTHVAR_ATTRIBUTE_AUTH_WRITE)
		return PAYLOAD_MM_AUTHVAR_ROUTE_INVALID;
	plan->require_signature_list = true;
	bypass = request->custom_mode && request->trusted_physical_presence;
	if (!bypass && request->setup_mode &&
	    !(request->require_self_signed_pk &&
	      plan->target == PAYLOAD_MM_AUTHVAR_TARGET_PK))
		bypass = true;
	if (bypass) {
		plan->authorities[0] = PAYLOAD_MM_AUTHVAR_AUTHORITY_BYPASS;
		plan->authority_count = 1U;
		plan->mark_vendor_keys_modified = !request->setup_mode ||
			(request->require_self_signed_pk &&
			 plan->target == PAYLOAD_MM_AUTHVAR_TARGET_PK);
	} else if (request->setup_mode) {
		plan->authorities[0] =
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PAYLOAD_CERT;
		plan->authority_count = 1U;
	} else {
		plan->authorities[0] = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
		plan->authority_count = 1U;
		if (plan->target == PAYLOAD_MM_AUTHVAR_TARGET_DB ||
		    plan->target == PAYLOAD_MM_AUTHVAR_TARGET_DBX ||
		    plan->target == PAYLOAD_MM_AUTHVAR_TARGET_DBT) {
			plan->authorities[1] =
				PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK;
			plan->authority_count = 2U;
		}
	}
	deleting = request->target_exists && !request->payload_size &&
		!(request->attributes & PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND);
	if (plan->target == PAYLOAD_MM_AUTHVAR_TARGET_PK) {
		plan->enter_user_mode = request->setup_mode && !deleting;
		plan->enter_setup_mode = !request->setup_mode && deleting;
	}
	return PAYLOAD_MM_AUTHVAR_ROUTE_OK;
}

enum payload_mm_authvar_route_result payload_mm_authvar_route_plan(
	const struct payload_mm_authvar_route_request *request,
	struct payload_mm_authvar_route_plan *plan)
{
	struct payload_mm_authvar_route_request copied;
	struct payload_mm_authvar_route_plan routed = { 0 };
	enum payload_mm_authvar_route_result result;

	if (!plan || (uintptr_t)plan % _Alignof(*plan) ||
	    !range_valid(plan, sizeof(*plan)))
		return PAYLOAD_MM_AUTHVAR_ROUTE_INVALID;
	if (!request) {
		memset(plan, 0, sizeof(*plan));
		return PAYLOAD_MM_AUTHVAR_ROUTE_INVALID;
	}
	if ((uintptr_t)request % _Alignof(*request) ||
	    !range_valid(request, sizeof(*request))) {
		memset(plan, 0, sizeof(*plan));
		return PAYLOAD_MM_AUTHVAR_ROUTE_INVALID;
	}
	if (ranges_overlap(request, sizeof(*request), plan, sizeof(*plan)))
		return PAYLOAD_MM_AUTHVAR_ROUTE_INVALID;
	copied = *request;
	if (!range_valid(copied.name, copied.name_size)) {
		memset(plan, 0, sizeof(*plan));
		return PAYLOAD_MM_AUTHVAR_ROUTE_INVALID;
	}
	if (ranges_overlap(copied.name, copied.name_size, plan, sizeof(*plan)))
		return PAYLOAD_MM_AUTHVAR_ROUTE_INVALID;
	memset(plan, 0, sizeof(*plan));
	if (!canonical_name(copied.name, copied.name_size) ||
	    (!copied.target_exists && copied.existing_attributes))
		return PAYLOAD_MM_AUTHVAR_ROUTE_INVALID;
	routed.target = classify_target(&copied);
	if (routed.target == PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE)
		result = route_private(&copied, &routed);
	else
		result = route_secure_boot(&copied, &routed);
	if (result == PAYLOAD_MM_AUTHVAR_ROUTE_OK)
		*plan = routed;
	return result;
}
