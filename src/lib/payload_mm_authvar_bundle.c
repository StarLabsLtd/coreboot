/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_bundle.h>
#include <boot/payload_mm_authvar_format.h>
#include <boot/payload_mm_authvar_view.h>
#include <commonlib/helpers.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"

static const uint8_t global_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static const uint8_t image_guid[16] = {
	0xcb, 0xb2, 0x19, 0xd7, 0x3a, 0x3d, 0x96, 0x45,
	0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f,
};
static const uint8_t cert_db_guid[16] = {
	0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
	0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
};

static const uint8_t kek_name[] = { 'K', 0, 'E', 0, 'K', 0, 0, 0 };
static const uint8_t db_name[] = { 'd', 0, 'b', 0, 0, 0 };
static const uint8_t dbx_name[] = { 'd', 0, 'b', 0, 'x', 0, 0, 0 };
static const uint8_t dbt_name[] = { 'd', 0, 'b', 0, 't', 0, 0, 0 };

/* Written out to keep the reserved-key audit independent of host wchar_t. */
static const uint8_t audit_mode_name[] = {
	'A', 0, 'u', 0, 'd', 0, 'i', 0, 't', 0, 'M', 0, 'o', 0, 'd', 0,
	'e', 0, 0, 0,
};
static const uint8_t deployed_mode_name[] = {
	'D', 0, 'e', 0, 'p', 0, 'l', 0, 'o', 0, 'y', 0, 'e', 0, 'd', 0,
	'M', 0, 'o', 0, 'd', 0, 'e', 0, 0, 0,
};
static const uint8_t kek_default_name[] = {
	'K', 0, 'E', 0, 'K', 0, 'D', 0, 'e', 0, 'f', 0, 'a', 0, 'u', 0,
	'l', 0, 't', 0, 0, 0,
};
static const uint8_t pk_default_name[] = {
	'P', 0, 'K', 0, 'D', 0, 'e', 0, 'f', 0, 'a', 0, 'u', 0, 'l', 0,
	't', 0, 0, 0,
};
static const uint8_t db_default_name[] = {
	'd', 0, 'b', 0, 'D', 0, 'e', 0, 'f', 0, 'a', 0, 'u', 0, 'l', 0,
	't', 0, 0, 0,
};
static const uint8_t dbx_default_name[] = {
	'd', 0, 'b', 0, 'x', 0, 'D', 0, 'e', 0, 'f', 0, 'a', 0, 'u', 0,
	'l', 0, 't', 0, 0, 0,
};
static const uint8_t dbt_default_name[] = {
	'd', 0, 'b', 0, 't', 0, 'D', 0, 'e', 0, 'f', 0, 'a', 0, 'u', 0,
	'l', 0, 't', 0, 0, 0,
};
static const uint8_t cert_db_name[] = {
	'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 0, 0,
};
static const uint8_t vendor_keys_modified;
static const uint8_t secure_boot_enabled = 1U;

static bool bytes_are_zero(const uint8_t *bytes, size_t size)
{
	uint8_t combined = 0U;

	while (size--)
		combined |= *bytes++;
	return combined == 0U;
}

static bool target_write_attributes_valid(uint32_t attributes, bool at_runtime)
{
	const uint32_t required = PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_BOOTSERVICE_ACCESS;
	const uint32_t supported = PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED &
		~PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND;

	if (attributes & ~supported ||
	    attributes & (PAYLOAD_MM_AUTHVAR_ATTRIBUTE_AUTH_WRITE |
		PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR) ||
	    (attributes & required) != required ||
	    (at_runtime &&
	     !(attributes & PAYLOAD_MM_AUTHVAR_ATTRIBUTE_RUNTIME_ACCESS)))
		return false;
	return true;
}

static bool range_valid(const void *data, size_t size)
{
	return size == 0U || (data && (uintptr_t)data <= UINTPTR_MAX - size);
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

static bool key_is(const struct payload_mm_authvar_policy_request *request,
	const uint8_t guid[16], const uint8_t *name, size_t name_size)
{
	return request->name_size == name_size &&
		!memcmp(request->vendor_guid, guid, 16U) &&
		!memcmp(request->name, name, name_size);
}

static bool target_matches(const struct payload_mm_authvar_policy_request *request,
	enum payload_mm_authvar_target target)
{
	switch (target) {
	case PAYLOAD_MM_AUTHVAR_TARGET_PK:
		return payload_mm_authvar_mode_key_matches(request->vendor_guid,
			request->name, request->name_size,
			PAYLOAD_MM_AUTHVAR_MODE_KEY_PK);
	case PAYLOAD_MM_AUTHVAR_TARGET_KEK:
		return key_is(request, global_guid, kek_name, sizeof(kek_name));
	case PAYLOAD_MM_AUTHVAR_TARGET_DB:
		return key_is(request, image_guid, db_name, sizeof(db_name));
	case PAYLOAD_MM_AUTHVAR_TARGET_DBX:
		return key_is(request, image_guid, dbx_name, sizeof(dbx_name));
	case PAYLOAD_MM_AUTHVAR_TARGET_DBT:
		return key_is(request, image_guid, dbt_name, sizeof(dbt_name));
	case PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE:
		return !payload_mm_authvar_mode_key_matches(request->vendor_guid,
				request->name, request->name_size,
				PAYLOAD_MM_AUTHVAR_MODE_KEY_PK) &&
			!key_is(request, global_guid, kek_name, sizeof(kek_name)) &&
			!key_is(request, image_guid, db_name, sizeof(db_name)) &&
			!key_is(request, image_guid, dbx_name, sizeof(dbx_name)) &&
			!key_is(request, image_guid, dbt_name, sizeof(dbt_name));
	}
	return false;
}

bool payload_mm_authvar_bundle_key_reserved(const uint8_t vendor_guid[16],
	const void *name, size_t name_size)
{
	static const struct {
		const uint8_t *guid;
		const uint8_t *name;
		size_t name_size;
	} keys[] = {
		{ global_guid, audit_mode_name, sizeof(audit_mode_name) },
		{ global_guid, deployed_mode_name, sizeof(deployed_mode_name) },
		{ global_guid, kek_default_name, sizeof(kek_default_name) },
		{ global_guid, pk_default_name, sizeof(pk_default_name) },
		{ global_guid, db_default_name, sizeof(db_default_name) },
		{ global_guid, dbx_default_name, sizeof(dbx_default_name) },
		{ global_guid, dbt_default_name, sizeof(dbt_default_name) },
		{ cert_db_guid, cert_db_name, sizeof(cert_db_name) },
	};

	if (!vendor_guid || !name || !name_size ||
	    (uintptr_t)vendor_guid > UINTPTR_MAX - 16U ||
	    (uintptr_t)name > UINTPTR_MAX - name_size)
		return false;
	for (enum payload_mm_authvar_mode_key key =
		PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE;
	     key <= PAYLOAD_MM_AUTHVAR_MODE_KEY_CUSTOM_MODE; key++)
		if (payload_mm_authvar_mode_key_matches(vendor_guid, name,
			name_size, key))
			return true;
	if (payload_mm_authvar_view_key_reserved(vendor_guid, name, name_size))
		return true;

	for (size_t i = 0U; i < ARRAY_SIZE(keys); i++)
		if (name_size == keys[i].name_size &&
		    !memcmp(vendor_guid, keys[i].guid, 16U) &&
		    !memcmp(name, keys[i].name, name_size))
			return true;
	return false;
}

static bool canonical_name(const void *name, size_t size)
{
	const uint8_t *bytes = name;

	if (!range_valid(name, size) || size < sizeof(uint16_t) ||
	    size > PAYLOAD_MM_AUTHVAR_ROUTE_MAX_NAME_SIZE + sizeof(uint16_t) ||
	    size % sizeof(uint16_t) || bytes[size - 2U] || bytes[size - 1U])
		return false;
	for (size_t i = 0U; i + sizeof(uint16_t) < size; i += sizeof(uint16_t))
		if (!bytes[i] && !bytes[i + 1U])
			return false;
	return true;
}

static bool decision_valid(
	const struct payload_mm_authvar_authority_decision *decision)
{
	const uint32_t private_intents =
		PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING |
		PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING;
	const bool binding_size_valid = decision->new_binding_size == 32U ||
		decision->new_binding_size == 48U ||
		decision->new_binding_size == 64U;
	const bool binding_tail_zero =
		decision->new_binding_size <= sizeof(decision->new_binding) &&
		bytes_are_zero(decision->new_binding +
			MIN(decision->new_binding_size,
				sizeof(decision->new_binding)),
			sizeof(decision->new_binding) -
			MIN(decision->new_binding_size,
				sizeof(decision->new_binding)));

	if (decision->outcome < PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION ||
	    decision->outcome > PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND ||
	    decision->target < PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE ||
	    decision->target > PAYLOAD_MM_AUTHVAR_TARGET_DBT ||
	    decision->accepted_authority <= PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE ||
	    decision->accepted_authority >
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB)
		return false;
	if (decision->outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP ||
	    decision->outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND)
		return !decision->mutation.kind && !decision->mutation.attributes &&
			!decision->mutation.data_size &&
			!memcmp(decision->mutation.timestamp,
				(uint8_t[16]) { 0 }, 16U) && !decision->data &&
			!decision->data_size && !decision->intents &&
			!decision->new_binding_size &&
			bytes_are_zero(decision->new_binding,
				sizeof(decision->new_binding));
	if (decision->outcome != PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION ||
	    (decision->mutation.kind != PAYLOAD_MM_AUTHVAR_MUTATION_WRITE &&
	     decision->mutation.kind != PAYLOAD_MM_AUTHVAR_MUTATION_DELETE))
		return false;
	if (decision->mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_DELETE) {
		if (decision->intents & PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING ||
		    decision->new_binding_size ||
		    !bytes_are_zero(decision->new_binding,
			sizeof(decision->new_binding)))
			return false;
		return !decision->mutation.attributes &&
			!decision->mutation.data_size &&
			!memcmp(decision->mutation.timestamp,
				(uint8_t[16]) { 0 }, 16U) && !decision->data &&
			!decision->data_size;
	}
	if (decision->intents & PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING)
		return false;
	if (decision->intents & PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING)
		return binding_size_valid && binding_tail_zero &&
			decision->mutation.data_size == decision->data_size &&
			range_valid(decision->data, decision->data_size);
	return !(decision->intents & private_intents) &&
		!decision->new_binding_size &&
		bytes_are_zero(decision->new_binding,
			sizeof(decision->new_binding)) &&
		decision->mutation.data_size == decision->data_size &&
		range_valid(decision->data, decision->data_size);
}

static bool decision_data_safe(
	const struct payload_mm_authvar_bundle_snapshot *snapshot)
{
	const struct payload_mm_authvar_authority_decision *decision =
		snapshot->decision;
	const struct payload_mm_authvar_policy_request *request = snapshot->request;
	const void *forbidden[] = {
		snapshot, request, decision, snapshot->index, request->name,
		snapshot->index->store, snapshot->index->entries,
	};
	size_t forbidden_sizes[] = {
		sizeof(*snapshot), sizeof(*request), sizeof(*decision),
		sizeof(*snapshot->index), request->name_size,
		snapshot->index->store_size,
		(size_t)snapshot->index->entry_count *
			sizeof(snapshot->index->entries[0]),
	};
	uintptr_t data_address = (uintptr_t)decision->data;
	uintptr_t request_address = (uintptr_t)request->data;

	if (!decision->data_size)
		return true;
	if (ranges_overlap(decision->data, decision->data_size,
		request->data, request->data_size))
		return data_address >= request_address &&
			data_address - request_address <= request->data_size &&
			decision->data_size <= request->data_size -
				(data_address - request_address);
	for (size_t i = 0U; i < ARRAY_SIZE(forbidden); i++)
		if (ranges_overlap(decision->data, decision->data_size,
			forbidden[i], forbidden_sizes[i]))
			return false;
	return true;
}

static bool inputs_valid(const struct payload_mm_authvar_bundle_snapshot *snapshot,
	const struct payload_mm_authvar_bundle_plan *plan)
{
	const struct payload_mm_authvar_policy_request *request;
	const struct payload_mm_authvar_authority_decision *decision;
	size_t entries_size;
	size_t part_count = 9U;
	const void *parts[10];
	size_t sizes[10];

	if (!snapshot || !plan || (uintptr_t)snapshot % _Alignof(*snapshot) ||
	    (uintptr_t)plan % _Alignof(*plan) ||
	    !range_valid(snapshot, sizeof(*snapshot)) ||
	    !range_valid(plan, sizeof(*plan)) ||
	    ranges_overlap(snapshot, sizeof(*snapshot), plan, sizeof(*plan)))
		return false;
	request = snapshot->request;
	decision = snapshot->decision;
	if (!request || !decision ||
	    (uintptr_t)request % _Alignof(*request) ||
	    (uintptr_t)decision % _Alignof(*decision) ||
	    !range_valid(request, sizeof(*request)) ||
	    !range_valid(decision, sizeof(*decision)) ||
	    !canonical_name(request->name, request->name_size) ||
	    !range_valid(request->data, request->data_size) ||
	    request->operation != PAYLOAD_MM_AUTHVAR_SERVICE_SET ||
	    !payload_mm_authvar_store_index_valid(snapshot->index))
		return false;
	entries_size = (size_t)snapshot->index->entry_count *
		sizeof(snapshot->index->entries[0]);
	parts[0] = snapshot; sizes[0] = sizeof(*snapshot);
	parts[1] = plan; sizes[1] = sizeof(*plan);
	parts[2] = request; sizes[2] = sizeof(*request);
	parts[3] = decision; sizes[3] = sizeof(*decision);
	parts[4] = snapshot->index; sizes[4] = sizeof(*snapshot->index);
	parts[5] = request->name; sizes[5] = request->name_size;
	parts[6] = request->data; sizes[6] = request->data_size;
	parts[7] = snapshot->index->store; sizes[7] = snapshot->index->store_size;
	parts[8] = snapshot->index->entries; sizes[8] = entries_size;
	if (decision->intents &
	    (PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING |
	     PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING)) {
		parts[9] = snapshot->certdb_workspace;
		sizes[9] = snapshot->certdb_workspace_size;
		part_count++;
	}
	for (size_t left = 0U; left < part_count; left++)
		for (size_t right = left + 1U; right < part_count; right++)
			if (ranges_overlap(parts[left], sizes[left], parts[right],
				sizes[right]))
				return false;
	return !ranges_overlap(decision->data, decision->data_size, plan,
		sizeof(*plan));
}

static enum payload_mm_verify_status certdb_status(
	enum payload_mm_authvar_certdb_result result)
{
	switch (result) {
	case PAYLOAD_MM_AUTHVAR_CERTDB_OK:
		return PAYLOAD_MM_VERIFY_OK;
	case PAYLOAD_MM_AUTHVAR_CERTDB_INVALID:
		return PAYLOAD_MM_VERIFY_INVALID;
	case PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED:
		return PAYLOAD_MM_VERIFY_MALFORMED;
	case PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND:
	case PAYLOAD_MM_AUTHVAR_CERTDB_EXISTS:
	case PAYLOAD_MM_AUTHVAR_CERTDB_NO_SPACE:
	default:
		return PAYLOAD_MM_VERIFY_REJECTED;
	}
}

static enum payload_mm_verify_status add_certdb_mutation(
	const struct payload_mm_authvar_bundle_snapshot *snapshot,
	struct payload_mm_authvar_bundle_plan *plan,
	const struct payload_mm_authvar_authority_decision *decision,
	enum payload_mm_authvar_certdb_operation operation)
{
	const struct payload_mm_authvar_policy_request *request = snapshot->request;
	const uint32_t attributes = PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_RUNTIME_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH;
	const struct payload_mm_authvar_store_entry *entry;
	const void *source;
	size_t replacement_size;
	enum payload_mm_authvar_certdb_result result;
	struct payload_mm_authvar_bundle_mutation *mutation;

	if (!snapshot->certdb_workspace ||
	    snapshot->certdb_workspace_size < snapshot->index->maximum_data_size ||
	    ranges_overlap(snapshot->certdb_workspace,
		snapshot->certdb_workspace_size, decision->data,
		decision->data_size))
		return PAYLOAD_MM_VERIFY_INVALID;
	entry = payload_mm_authvar_store_find(snapshot->index, cert_db_guid,
		cert_db_name, sizeof(cert_db_name));
	if (!entry || entry->attributes != attributes ||
	    !bytes_are_zero(snapshot->index->store + entry->record_offset + 16U,
		16U))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	source = payload_mm_authvar_store_data(snapshot->index, entry);
	if (!source)
		return PAYLOAD_MM_VERIFY_INVALID;
	result = payload_mm_authvar_certdb_compose(source, entry->data_size,
		operation, request->vendor_guid, request->name,
		request->name_size - sizeof(uint16_t),
		operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD ?
			decision->new_binding : NULL,
		operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD ?
			decision->new_binding_size : 0U,
		snapshot->certdb_workspace, snapshot->index->maximum_data_size,
		&replacement_size);
	if (result != PAYLOAD_MM_AUTHVAR_CERTDB_OK)
		return certdb_status(result);
	if (plan->mutation_count >= PAYLOAD_MM_AUTHVAR_BUNDLE_MAX_MUTATIONS ||
	    replacement_size > UINT32_MAX)
		return PAYLOAD_MM_VERIFY_INTERNAL;
	mutation = &plan->mutations[plan->mutation_count++];
	mutation->role = PAYLOAD_MM_AUTHVAR_BUNDLE_CERTDB;
	mutation->mutation.kind = PAYLOAD_MM_AUTHVAR_MUTATION_WRITE;
	mutation->mutation.attributes = attributes;
	mutation->mutation.data_size = (uint32_t)replacement_size;
	memcpy(mutation->vendor_guid, cert_db_guid,
		sizeof(mutation->vendor_guid));
	mutation->name = cert_db_name;
	mutation->name_size = sizeof(cert_db_name);
	mutation->data = snapshot->certdb_workspace;
	mutation->data_size = replacement_size;
	plan->certdb_operation = operation;
	if (operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD) {
		plan->private_binding_size = decision->new_binding_size;
		memcpy(plan->private_binding, decision->new_binding,
			decision->new_binding_size);
	}
	return PAYLOAD_MM_VERIFY_OK;
}

static uint8_t mode_projection(const struct payload_mm_authvar_bundle_facts *facts)
{
	return (facts->setup_mode ? PAYLOAD_MM_AUTHVAR_MODE_SETUP : 0U) |
		(facts->secure_boot ? PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT : 0U) |
		(facts->vendor_keys ? PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS : 0U);
}

static bool add_mutation(struct payload_mm_authvar_bundle_plan *plan,
	enum payload_mm_authvar_bundle_role role, uint32_t kind, uint32_t attributes,
	const uint8_t guid[16], const void *name, size_t name_size,
	const void *data, size_t data_size)
{
	struct payload_mm_authvar_bundle_mutation *mutation;

	if (plan->mutation_count >= PAYLOAD_MM_AUTHVAR_BUNDLE_MAX_MUTATIONS ||
	    data_size > UINT32_MAX)
		return false;
	mutation = &plan->mutations[plan->mutation_count++];

	mutation->role = role;
	mutation->mutation.kind = kind;
	mutation->mutation.attributes = attributes;
	mutation->mutation.data_size = (uint32_t)data_size;
	memcpy(mutation->vendor_guid, guid, sizeof(mutation->vendor_guid));
	mutation->name = name;
	mutation->name_size = name_size;
	mutation->data = data;
	mutation->data_size = data_size;
	return true;
}

static bool add_mode_mutation(struct payload_mm_authvar_bundle_plan *plan,
	enum payload_mm_authvar_bundle_role role, uint32_t kind,
	uint32_t attributes, enum payload_mm_authvar_mode_key key,
	const void *data, size_t data_size)
{
	struct payload_mm_authvar_bundle_mutation *mutation;

	if (plan->mutation_count >= PAYLOAD_MM_AUTHVAR_BUNDLE_MAX_MUTATIONS ||
	    data_size > UINT32_MAX)
		return false;
	mutation = &plan->mutations[plan->mutation_count++];
	mutation->role = role;
	mutation->mutation.kind = kind;
	mutation->mutation.attributes = attributes;
	mutation->mutation.data_size = (uint32_t)data_size;
	mutation->data = data;
	mutation->data_size = data_size;
	return payload_mm_authvar_mode_mutation_key(mutation, key);
}

enum payload_mm_verify_status payload_mm_authvar_bundle_plan(
	const struct payload_mm_authvar_bundle_snapshot *snapshot,
	struct payload_mm_authvar_bundle_plan *plan)
{
	struct payload_mm_authvar_bundle_plan draft = { 0 };
	const struct payload_mm_authvar_authority_decision *decision;
	const struct payload_mm_authvar_policy_request *request;
	const struct payload_mm_authvar_store_entry *pk;
	const struct payload_mm_authvar_store_entry *secure_boot_enable;
	const struct payload_mm_authvar_store_entry *target_entry;
	uint8_t vendor_keys_nv_data;
	bool secure_boot_enable_data = false;
	const uint32_t mode_intents = PAYLOAD_MM_AUTHVAR_INTENT_ENTER_USER_MODE |
		PAYLOAD_MM_AUTHVAR_INTENT_ENTER_SETUP_MODE |
		PAYLOAD_MM_AUTHVAR_INTENT_MARK_VENDOR_KEYS;
	bool final_pk;
	bool needs_vendor_write;
	uint32_t private_intents;
	enum payload_mm_verify_status certdb_result;

	if (!inputs_valid(snapshot, plan))
		return PAYLOAD_MM_VERIFY_INVALID;
	memset(plan, 0, sizeof(*plan));
	decision = snapshot->decision;
	request = snapshot->request;
	if (!decision_valid(decision) ||
	    !target_matches(request, decision->target) ||
	    payload_mm_authvar_bundle_key_reserved(request->vendor_guid,
		request->name, request->name_size) ||
	    (snapshot->facts.at_runtime && !snapshot->facts.ready_to_boot))
		return PAYLOAD_MM_VERIFY_INVALID;
	if (!decision_data_safe(snapshot))
		return PAYLOAD_MM_VERIFY_INVALID;
	if (!(request->attributes & PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH) ||
	    request->attributes & PAYLOAD_MM_AUTHVAR_ATTRIBUTE_AUTH_WRITE)
		return PAYLOAD_MM_VERIFY_CHANGED;
	if (decision->outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION &&
	    decision->mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_WRITE &&
	    (!decision->data_size ||
	     !target_write_attributes_valid(decision->mutation.attributes,
		snapshot->facts.at_runtime) ||
	     decision->mutation.attributes !=
		(request->attributes & ~PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND) ||
	     !payload_mm_authvar_timestamp_store_valid(
		decision->mutation.timestamp) ||
	     bytes_are_zero(decision->mutation.timestamp,
		sizeof(decision->mutation.timestamp))))
		return PAYLOAD_MM_VERIFY_CHANGED;
	pk = payload_mm_authvar_mode_find(snapshot->index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_PK);
	if (snapshot->facts.setup_mode == (pk != NULL))
		return PAYLOAD_MM_VERIFY_CHANGED;
	secure_boot_enable = payload_mm_authvar_mode_find(snapshot->index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE);
	if (!payload_mm_authvar_mode_value(snapshot->index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_VENDOR_KEYS_NV,
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH,
		&vendor_keys_nv_data) ||
	    snapshot->facts.vendor_keys != !!vendor_keys_nv_data)
		return PAYLOAD_MM_VERIFY_CHANGED;
	if (secure_boot_enable &&
	    !payload_mm_authvar_mode_enabled(snapshot->index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE,
		(PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE |
		 PAYLOAD_MM_AUTHVAR_ATTRIBUTE_BOOTSERVICE_ACCESS),
		&secure_boot_enable_data))
		return PAYLOAD_MM_VERIFY_CHANGED;
	if (!snapshot->facts.at_runtime &&
	    ((snapshot->facts.setup_mode && snapshot->facts.secure_boot) ||
	     (!snapshot->facts.setup_mode &&
	      (!secure_boot_enable || snapshot->facts.secure_boot !=
		      secure_boot_enable_data))))
		return PAYLOAD_MM_VERIFY_CHANGED;
	if (decision->outcome != PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION) {
		target_entry = payload_mm_authvar_store_find(snapshot->index,
			request->vendor_guid, request->name, request->name_size);
		if (target_entry ||
		    (decision->outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP) !=
			!!(request->attributes &
			   PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND))
			return PAYLOAD_MM_VERIFY_CHANGED;
		draft.outcome = decision->outcome;
		*plan = draft;
		return PAYLOAD_MM_VERIFY_OK;
	}
	private_intents = decision->intents &
		(PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING |
		 PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING);
	if (decision->intents & ~(mode_intents |
		PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING |
		PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING) ||
	    private_intents == (PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING |
		PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING) ||
	    (private_intents && decision->intents & mode_intents) ||
	    (decision->intents & PAYLOAD_MM_AUTHVAR_INTENT_ENTER_USER_MODE &&
	     decision->intents & PAYLOAD_MM_AUTHVAR_INTENT_ENTER_SETUP_MODE))
		return PAYLOAD_MM_VERIFY_UNSUPPORTED;
	if (decision->target == PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE) {
		target_entry = payload_mm_authvar_store_find(snapshot->index,
			request->vendor_guid, request->name, request->name_size);
		if (decision->mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_DELETE) {
			if (decision->accepted_authority !=
				PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB ||
			    !target_entry ||
			    private_intents !=
				PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING)
				return PAYLOAD_MM_VERIFY_CHANGED;
		} else if (decision->accepted_authority ==
			   PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER) {
			if (target_entry || private_intents !=
				PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING)
				return PAYLOAD_MM_VERIFY_CHANGED;
		} else if (decision->accepted_authority !=
			   PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB ||
			   !target_entry || private_intents) {
			return PAYLOAD_MM_VERIFY_CHANGED;
		}
	} else if (private_intents) {
		return PAYLOAD_MM_VERIFY_CHANGED;
	}
	if (decision->mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_DELETE &&
	    !payload_mm_authvar_store_find(snapshot->index, request->vendor_guid,
		request->name, request->name_size))
		return PAYLOAD_MM_VERIFY_CHANGED;
	final_pk = pk != NULL;
	if (decision->target == PAYLOAD_MM_AUTHVAR_TARGET_PK)
		final_pk = decision->mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_WRITE;
	if (!!(decision->intents & PAYLOAD_MM_AUTHVAR_INTENT_ENTER_USER_MODE) !=
	    (snapshot->facts.setup_mode && final_pk) ||
	    !!(decision->intents & PAYLOAD_MM_AUTHVAR_INTENT_ENTER_SETUP_MODE) !=
	    (!snapshot->facts.setup_mode && !final_pk))
		return PAYLOAD_MM_VERIFY_CHANGED;
	needs_vendor_write = decision->intents &
		PAYLOAD_MM_AUTHVAR_INTENT_MARK_VENDOR_KEYS &&
		snapshot->facts.vendor_keys;
	if (snapshot->facts.at_runtime && needs_vendor_write)
		return PAYLOAD_MM_VERIFY_REJECTED;

	draft.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION;
	draft.volatile_modes = mode_projection(&snapshot->facts);
	if (!add_mutation(&draft, PAYLOAD_MM_AUTHVAR_BUNDLE_TARGET,
		decision->mutation.kind, decision->mutation.attributes,
		request->vendor_guid, request->name, request->name_size,
		decision->data, decision->data_size))
		return PAYLOAD_MM_VERIFY_INTERNAL;
	draft.mutations[0].mutation = decision->mutation;
	if (private_intents) {
		certdb_result = add_certdb_mutation(snapshot, &draft, decision,
			private_intents & PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING ?
				PAYLOAD_MM_AUTHVAR_CERTDB_ADD :
				PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE);
		if (certdb_result != PAYLOAD_MM_VERIFY_OK)
			return certdb_result;
	}
	if (decision->intents & PAYLOAD_MM_AUTHVAR_INTENT_ENTER_USER_MODE) {
		draft.volatile_modes &=
			(uint8_t)~PAYLOAD_MM_AUTHVAR_MODE_SETUP;
		if (!snapshot->facts.at_runtime) {
			draft.volatile_modes |= PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;
			if (!add_mode_mutation(&draft,
				PAYLOAD_MM_AUTHVAR_BUNDLE_SECURE_BOOT_ENABLE,
				PAYLOAD_MM_AUTHVAR_MUTATION_WRITE,
				PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE |
				PAYLOAD_MM_AUTHVAR_ATTRIBUTE_BOOTSERVICE_ACCESS,
				PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE,
				&secure_boot_enabled, 1U))
				return PAYLOAD_MM_VERIFY_INTERNAL;
		}
	} else if (decision->intents & PAYLOAD_MM_AUTHVAR_INTENT_ENTER_SETUP_MODE) {
		draft.volatile_modes |= PAYLOAD_MM_AUTHVAR_MODE_SETUP;
		if (!snapshot->facts.at_runtime) {
			draft.volatile_modes &=
				(uint8_t)~PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;
			if (secure_boot_enable)
				if (!add_mode_mutation(&draft,
					PAYLOAD_MM_AUTHVAR_BUNDLE_SECURE_BOOT_ENABLE,
					PAYLOAD_MM_AUTHVAR_MUTATION_DELETE, 0U,
					PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE,
					NULL, 0U))
					return PAYLOAD_MM_VERIFY_INTERNAL;
		}
	}
	if (decision->intents & PAYLOAD_MM_AUTHVAR_INTENT_MARK_VENDOR_KEYS) {
		draft.volatile_modes &=
			(uint8_t)~PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
		if (needs_vendor_write) {
			if (!add_mode_mutation(&draft,
				PAYLOAD_MM_AUTHVAR_BUNDLE_VENDOR_KEYS_NV,
				PAYLOAD_MM_AUTHVAR_MUTATION_WRITE,
				PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE |
				PAYLOAD_MM_AUTHVAR_ATTRIBUTE_BOOTSERVICE_ACCESS |
				PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH,
				PAYLOAD_MM_AUTHVAR_MODE_KEY_VENDOR_KEYS_NV,
				&vendor_keys_modified, 1U))
				return PAYLOAD_MM_VERIFY_INTERNAL;
		}
	}
	if (draft.mutation_count > PAYLOAD_MM_AUTHVAR_BUNDLE_MAX_MUTATIONS)
		return PAYLOAD_MM_VERIFY_INTERNAL;
	*plan = draft;
	return PAYLOAD_MM_VERIFY_OK;
}
