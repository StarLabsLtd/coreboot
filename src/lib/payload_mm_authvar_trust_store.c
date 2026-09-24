/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_format.h>
#include <boot/payload_mm_authvar_signature_db.h>
#include <boot/payload_mm_authvar_trust_store.h>

#include "payload_mm_crypto/crypto.h"

#include <commonlib/helpers.h>
#include <string.h>

static const uint8_t global_variable_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};

static const uint8_t x509_guid[16] = {
	0xa1, 0x59, 0xc0, 0xa5, 0xe4, 0x94, 0xa7, 0x4a,
	0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72,
};

static const uint8_t pk_name[] = { 'P', 0, 'K', 0, 0, 0 };
static const uint8_t kek_name[] = { 'K', 0, 'E', 0, 'K', 0, 0, 0 };

#define MAX_KEK_TRUST_ANCHORS PAYLOAD_MM_AUTHVAR_SIGNATURE_DB_MAX_X509

#define SECURE_BOOT_VARIABLE_ATTRIBUTES \
	(PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE | \
	 PAYLOAD_MM_AUTHVAR_ATTRIBUTE_BOOTSERVICE_ACCESS | \
	 PAYLOAD_MM_AUTHVAR_ATTRIBUTE_RUNTIME_ACCESS | \
	 PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH)

#ifdef PAYLOAD_MM_AUTH_TEST
__weak void payload_mm_authvar_trust_store_test_before_publish(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan,
	enum payload_mm_authvar_authority *accepted_authority)
{
	(void)owner;
	(void)index;
	(void)plan;
	(void)accepted_authority;
}
#endif

static bool range_valid(const void *data, size_t size)
{
	return size == 0U ||
		(data != NULL && (uintptr_t)data <= UINTPTR_MAX - size);
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

static bool plan_valid(const struct payload_mm_authvar_route_plan *plan)
{
	if (!plan || (uintptr_t)plan % _Alignof(*plan) ||
	    !range_valid(plan, sizeof(*plan)))
		return false;
	if (plan->target == PAYLOAD_MM_AUTHVAR_TARGET_PK ||
	    plan->target == PAYLOAD_MM_AUTHVAR_TARGET_KEK)
		return plan->authority_count == 1U &&
			plan->authorities[0] ==
				PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	if (plan->target == PAYLOAD_MM_AUTHVAR_TARGET_DB ||
	    plan->target == PAYLOAD_MM_AUTHVAR_TARGET_DBX ||
	    plan->target == PAYLOAD_MM_AUTHVAR_TARGET_DBT)
		return plan->authority_count == 2U &&
			plan->authorities[0] ==
				PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK &&
			plan->authorities[1] ==
				PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK;
	return false;
}

static bool inputs_disjoint(const struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_cms_verified_signer *verified,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan)
{
	const void *descriptors[] = { owner, signed_data, verified, index, plan };
	const size_t descriptor_sizes[] = {
		sizeof(*owner), sizeof(*signed_data), sizeof(*verified),
		sizeof(*index), sizeof(*plan),
	};
	const void *protected_ranges[3];
	size_t protected_sizes[3];

	if (!owner || (uintptr_t)owner % _Alignof(*owner) ||
	    !signed_data || (uintptr_t)signed_data % _Alignof(*signed_data) ||
	    !verified || (uintptr_t)verified % _Alignof(*verified) ||
	    !range_valid(owner, sizeof(*owner)) ||
	    !range_valid(signed_data, sizeof(*signed_data)) ||
	    !range_valid(verified, sizeof(*verified)) ||
	    !range_valid(signed_data->data, signed_data->size) ||
	    !signed_data->size || signed_data->size > PAYLOAD_MM_MAX_CMS_SIZE)
		return false;
	protected_ranges[0] = index->store;
	protected_sizes[0] = index->store_size;
	protected_ranges[1] = index->entries;
	protected_sizes[1] =
		(size_t)index->entry_count * sizeof(index->entries[0]);
	protected_ranges[2] = signed_data->data;
	protected_sizes[2] = signed_data->size;
	for (size_t left = 0U; left < ARRAY_SIZE(descriptors); left++) {
		for (size_t right = left + 1U; right < ARRAY_SIZE(descriptors);
		     right++)
			if (ranges_overlap(descriptors[left], descriptor_sizes[left],
				descriptors[right], descriptor_sizes[right]))
				return false;
		for (size_t range = 0U; range < ARRAY_SIZE(protected_ranges);
		     range++)
			if (ranges_overlap(descriptors[left], descriptor_sizes[left],
				protected_ranges[range], protected_sizes[range]))
				return false;
	}
	for (size_t left = 0U; left < ARRAY_SIZE(protected_ranges); left++)
		for (size_t right = left + 1U;
		     right < ARRAY_SIZE(protected_ranges); right++)
			if (ranges_overlap(protected_ranges[left], protected_sizes[left],
				protected_ranges[right], protected_sizes[right]))
				return false;
	return true;
}

static bool authority_output_valid(
	const enum payload_mm_authvar_authority *accepted_authority,
	const struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content, size_t content_count,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan)
{
	const void *descriptors[] = { owner, signed_data, content, index, plan };
	const size_t descriptor_sizes[] = {
		sizeof(*owner), sizeof(*signed_data),
		content_count * sizeof(*content), sizeof(*index), sizeof(*plan),
	};
	size_t entry_bytes;

	if (!accepted_authority ||
	    (uintptr_t)accepted_authority % _Alignof(*accepted_authority) ||
	    !range_valid(accepted_authority, sizeof(*accepted_authority)) ||
	    !content || (uintptr_t)content % _Alignof(*content) ||
	    !content_count || content_count > PAYLOAD_MM_HASH_MAX_SPANS ||
	    !range_valid(content, content_count * sizeof(*content)))
		return false;
	for (size_t span = 0U; span < content_count; span++)
		if (!range_valid(content[span].data, content[span].size))
			return false;
	entry_bytes = (size_t)index->entry_count * sizeof(index->entries[0]);
	for (size_t descriptor = 0U; descriptor < ARRAY_SIZE(descriptors);
	     descriptor++)
		if (ranges_overlap(accepted_authority, sizeof(*accepted_authority),
			descriptors[descriptor], descriptor_sizes[descriptor]))
			return false;
	if (ranges_overlap(accepted_authority, sizeof(*accepted_authority),
		signed_data->data, signed_data->size) ||
	    ranges_overlap(accepted_authority, sizeof(*accepted_authority),
		index->store, index->store_size) ||
	    ranges_overlap(accepted_authority, sizeof(*accepted_authority),
		index->entries, entry_bytes))
		return false;
	for (size_t span = 0U; span < content_count; span++)
		if (ranges_overlap(accepted_authority,
			sizeof(*accepted_authority), content[span].data,
			content[span].size))
			return false;
	return true;
}

static bool list_is_x509(
	const struct payload_mm_authvar_signature_list_view *list)
{
	return !memcmp(list->type_guid, x509_guid, sizeof(x509_guid));
}


static enum payload_mm_verify_status verify_anchor(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_cms_verified_signer *verified,
	const struct payload_mm_authvar_signature_view *signature)
{
	const struct payload_mm_crypto_span anchor = {
		signature->data.data, signature->data.size,
	};

	return payload_mm_authvar_trust_anchor_verify(owner, signed_data, verified,
		&anchor);
}

static enum payload_mm_verify_status verify_platform_key(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_cms_verified_signer *verified,
	const void *data, size_t size)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;
	struct payload_mm_authvar_signature_view signature;
	enum payload_mm_verify_status status;
	size_t matches = 0U;

	status = payload_mm_authvar_signature_db_validate(owner, data, size,
		PAYLOAD_MM_AUTHVAR_PLATFORM_KEY, 1U, NULL);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	payload_mm_authvar_signature_list_begin(&cursor, data, size);
	if (payload_mm_authvar_signature_list_next(&cursor, &list) !=
		PAYLOAD_MM_AUTHVAR_FORMAT_OK ||
	    !payload_mm_authvar_signature_at(&list, 0U, &signature))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	if (verified->signer_certificate.size != signature.data.size ||
	    memcmp(verified->signer_certificate.data, signature.data.data,
		signature.data.size))
		return PAYLOAD_MM_VERIFY_REJECTED;
	for (size_t certificate = 0U;
	     certificate < verified->certificate_count; certificate++)
		if (verified->certificates[certificate].size == signature.data.size &&
		    !memcmp(verified->certificates[certificate].data,
			signature.data.data, signature.data.size))
			matches++;
	return matches == 1U ? PAYLOAD_MM_VERIFY_OK : PAYLOAD_MM_VERIFY_REJECTED;
}

static enum payload_mm_verify_status verify_exchange_keys(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_cms_verified_signer *verified,
	const void *data, size_t size)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;
	struct payload_mm_authvar_signature_view signature;
	enum payload_mm_authvar_format_result result;
	enum payload_mm_verify_status status;

	status = payload_mm_authvar_signature_db_validate(owner, data, size,
		PAYLOAD_MM_AUTHVAR_SIGNATURE_DB, MAX_KEK_TRUST_ANCHORS, NULL);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	payload_mm_authvar_signature_list_begin(&cursor, data, size);
	while ((result = payload_mm_authvar_signature_list_next(&cursor, &list)) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_OK) {
		if (!list_is_x509(&list))
			continue;
		for (uint32_t item = 0U; item < list.signature_count; item++) {
			if (!payload_mm_authvar_signature_at(&list, item, &signature))
				return PAYLOAD_MM_VERIFY_MALFORMED;
			status = verify_anchor(owner, signed_data, verified, &signature);
			if (status == PAYLOAD_MM_VERIFY_OK)
				return status;
			if (status != PAYLOAD_MM_VERIFY_REJECTED)
				return status;
		}
	}
	return result == PAYLOAD_MM_AUTHVAR_FORMAT_DONE ?
		PAYLOAD_MM_VERIFY_REJECTED : PAYLOAD_MM_VERIFY_MALFORMED;
}

static enum payload_mm_verify_status verify_with_signer(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_cms_verified_signer *verified,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan,
	enum payload_mm_authvar_authority *accepted_authority)
{
	struct payload_mm_authvar_store_index index_snapshot;
	struct payload_mm_authvar_route_plan plan_snapshot;
	const struct payload_mm_authvar_store_entry *entry;
	const void *data;
	enum payload_mm_verify_status status;

	if (!payload_mm_authvar_store_index_valid(index) || !plan_valid(plan) ||
	    !inputs_disjoint(owner, signed_data, verified, index, plan))
		return PAYLOAD_MM_VERIFY_INVALID;
	index_snapshot = *index;
	plan_snapshot = *plan;
	index = &index_snapshot;
	plan = &plan_snapshot;
	for (uint8_t authority = 0U; authority < plan->authority_count;
	     authority++) {
		if (plan->authorities[authority] ==
		    PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK) {
			entry = payload_mm_authvar_store_find(index, global_variable_guid,
				pk_name, sizeof(pk_name));
		} else {
			entry = payload_mm_authvar_store_find(index, global_variable_guid,
				kek_name, sizeof(kek_name));
		}
		if (!entry) {
			status = PAYLOAD_MM_VERIFY_REJECTED;
			continue;
		}
		if (entry->attributes != SECURE_BOOT_VARIABLE_ATTRIBUTES)
			return PAYLOAD_MM_VERIFY_MALFORMED;
		data = payload_mm_authvar_store_data(index, entry);
		if (!data)
			return PAYLOAD_MM_VERIFY_INVALID;
		status = plan->authorities[authority] ==
			PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK ?
			verify_platform_key(owner, verified, data,
				entry->data_size) :
			verify_exchange_keys(owner, signed_data, verified, data,
				entry->data_size);
		if (status == PAYLOAD_MM_VERIFY_OK) {
			*accepted_authority = plan->authorities[authority];
			return status;
		}
		if (status != PAYLOAD_MM_VERIFY_REJECTED)
			return status;
	}
	return PAYLOAD_MM_VERIFY_REJECTED;
}

enum payload_mm_verify_status payload_mm_authvar_trust_store_verify(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content, size_t content_count,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan,
	enum payload_mm_authvar_authority *accepted_authority)
{
	struct payload_mm_cms_verified_signer verified = { 0 };
	struct payload_mm_crypto_span signed_data_snapshot;
	struct payload_mm_crypto_span content_snapshot[PAYLOAD_MM_HASH_MAX_SPANS];
	struct payload_mm_authvar_store_index index_snapshot;
	struct payload_mm_authvar_route_plan plan_snapshot;
	enum payload_mm_authvar_authority accepted =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE;
	enum payload_mm_verify_status status;
	bool changed;
	bool clean;

	/* Protect scanner and route state before crypto can write or wipe owner. */
	if (!payload_mm_authvar_store_index_valid(index) || !plan_valid(plan) ||
	    !inputs_disjoint(owner, signed_data, &verified, index, plan) ||
	    !authority_output_valid(accepted_authority, owner, signed_data,
		content, content_count, index, plan))
		return PAYLOAD_MM_VERIFY_INVALID;
	*accepted_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE;
	signed_data_snapshot = *signed_data;
	memcpy(content_snapshot, content, content_count * sizeof(*content));
	index_snapshot = *index;
	plan_snapshot = *plan;
	status = payload_mm_cms_verify_detached_untrusted(owner, signed_data,
		content, content_count, &verified);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	status = verify_with_signer(owner, signed_data, &verified, index, plan,
		&accepted);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
#ifdef PAYLOAD_MM_AUTH_TEST
	payload_mm_authvar_trust_store_test_before_publish(owner, index, plan,
		accepted_authority);
#endif
	changed = memcmp(signed_data, &signed_data_snapshot,
		sizeof(*signed_data)) ||
	    memcmp(content, content_snapshot,
		content_count * sizeof(*content)) ||
	    memcmp(index, &index_snapshot, sizeof(*index)) ||
	    memcmp(plan, &plan_snapshot, sizeof(*plan)) ||
	    *accepted_authority != PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE;
	clean = payload_mm_crypto_idle() &&
		payload_mm_crypto_owner_is_clean(owner);
	if (!clean)
		payload_mm_crypto_abort(owner);
	*accepted_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE;
	if (changed)
		return PAYLOAD_MM_VERIFY_CHANGED;
	if (!clean)
		return PAYLOAD_MM_VERIFY_INTERNAL;
	if (accepted == PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE)
		return PAYLOAD_MM_VERIFY_INTERNAL;
	*accepted_authority = accepted;
	return PAYLOAD_MM_VERIFY_OK;
}
