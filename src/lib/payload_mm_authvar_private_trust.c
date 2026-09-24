/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_certdb.h>
#include <boot/payload_mm_authvar_private_trust.h>

#include "payload_mm_authvar_private_binding.h"
#include "payload_mm_crypto/crypto.h"

#include <commonlib/helpers.h>
#include <mbedtls/platform_util.h>
#include <string.h>

#define PRIVATE_CONTENT_SPANS 5U
#define CONTENT_NAME 0U
#define CONTENT_GUID 1U
#define CONTENT_ATTRIBUTES 2U
#define CONTENT_TIMESTAMP 3U
#define CONTENT_PAYLOAD 4U

#define CERTDB_ATTRIBUTES \
	(PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE | \
	 PAYLOAD_MM_AUTHVAR_ATTRIBUTE_BOOTSERVICE_ACCESS | \
	 PAYLOAD_MM_AUTHVAR_ATTRIBUTE_RUNTIME_ACCESS | \
	 PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH)
#define PRIVATE_ATTRIBUTE_MASK \
	(CERTDB_ATTRIBUTES | PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND)

static const uint8_t certdb_guid[16] = {
	0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
	0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
};
static const uint8_t certdb_name[] = {
	'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 0, 0,
};

struct protected_digest {
	uint8_t signed_data[PAYLOAD_MM_SHA256_SIZE];
	uint8_t content[PRIVATE_CONTENT_SPANS][PAYLOAD_MM_SHA256_SIZE];
	uint8_t store[PAYLOAD_MM_SHA256_SIZE];
	uint8_t entries[PAYLOAD_MM_SHA256_SIZE];
};

#ifdef PAYLOAD_MM_AUTH_TEST
__weak void payload_mm_authvar_private_trust_test_before_publish(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan,
	struct payload_mm_authvar_private_trust_decision *decision)
{
	(void)owner;
	(void)index;
	(void)plan;
	(void)decision;
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

static uint32_t content_attributes(const struct payload_mm_crypto_span *content)
{
	const uint8_t *bytes = content[CONTENT_ATTRIBUTES].data;

	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
		(uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static bool canonical_name(const struct payload_mm_crypto_span *name)
{
	if (!name->size || name->size > PAYLOAD_MM_AUTHVAR_ROUTE_MAX_NAME_SIZE ||
	    name->size % sizeof(uint16_t))
		return false;
	for (size_t offset = 0U; offset < name->size; offset += sizeof(uint16_t))
		if (!name->data[offset] && !name->data[offset + 1U])
			return false;
	return true;
}

static bool plan_valid(const struct payload_mm_authvar_route_plan *plan)
{
	if (!plan || (uintptr_t)plan % _Alignof(*plan) ||
	    !range_valid(plan, sizeof(*plan)) ||
	    plan->target != PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE ||
	    plan->authority_count != 1U ||
	    plan->authorities[1] != PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE ||
	    plan->require_signature_list || plan->enter_user_mode ||
	    plan->enter_setup_mode || plan->mark_vendor_keys_modified)
		return false;
	return plan->authorities[0] ==
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER ||
		plan->authorities[0] == PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
}

static bool inputs_valid(struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content, size_t content_count,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan,
	struct payload_mm_authvar_private_trust_decision *decision)
{
	const void *descriptors[] = {
		owner, signed_data, content, index, plan, decision,
	};
	const size_t descriptor_sizes[] = {
		sizeof(*owner), sizeof(*signed_data), content_count * sizeof(*content),
		sizeof(*index), sizeof(*plan), sizeof(*decision),
	};
	const void *data[PRIVATE_CONTENT_SPANS + 3U];
	size_t sizes[PRIVATE_CONTENT_SPANS + 3U];
	size_t entries_size;
	uint32_t attributes;

	if (!owner || (uintptr_t)owner % _Alignof(*owner) ||
	    !signed_data || (uintptr_t)signed_data % _Alignof(*signed_data) ||
	    !content || (uintptr_t)content % _Alignof(*content) ||
	    !decision || (uintptr_t)decision % _Alignof(*decision) ||
	    !range_valid(owner, sizeof(*owner)) ||
	    !range_valid(signed_data, sizeof(*signed_data)) ||
	    !range_valid(content, content_count * sizeof(*content)) ||
	    !range_valid(decision, sizeof(*decision)) ||
	    content_count != PRIVATE_CONTENT_SPANS ||
	    !range_valid(signed_data->data, signed_data->size) ||
	    !signed_data->size || signed_data->size > PAYLOAD_MM_MAX_CMS_SIZE ||
	    !payload_mm_authvar_store_index_valid(index) || !plan_valid(plan))
		return false;
	for (size_t span = 0U; span < content_count; span++)
		if (!range_valid(content[span].data, content[span].size))
			return false;
	if (!canonical_name(&content[CONTENT_NAME]) ||
	    content[CONTENT_GUID].size != 16U ||
	    content[CONTENT_ATTRIBUTES].size != sizeof(uint32_t) ||
	    content[CONTENT_TIMESTAMP].size != 16U)
		return false;
	attributes = content_attributes(content);
	if ((attributes & (PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH)) !=
		(PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE |
		 PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH) ||
	    attributes & ~PRIVATE_ATTRIBUTE_MASK ||
	    ((attributes & PAYLOAD_MM_AUTHVAR_ATTRIBUTE_RUNTIME_ACCESS) &&
	     !(attributes & PAYLOAD_MM_AUTHVAR_ATTRIBUTE_BOOTSERVICE_ACCESS)))
		return false;
	entries_size = (size_t)index->entry_count * sizeof(index->entries[0]);
	data[0] = signed_data->data;
	sizes[0] = signed_data->size;
	data[1] = index->store;
	sizes[1] = index->store_size;
	data[2] = index->entries;
	sizes[2] = entries_size;
	for (size_t span = 0U; span < content_count; span++) {
		data[span + 3U] = content[span].data;
		sizes[span + 3U] = content[span].size;
	}
	for (size_t left = 0U; left < ARRAY_SIZE(descriptors); left++)
		for (size_t right = left + 1U; right < ARRAY_SIZE(descriptors);
		     right++)
			if (ranges_overlap(descriptors[left], descriptor_sizes[left],
				descriptors[right], descriptor_sizes[right]))
				return false;
	for (size_t descriptor = 0U; descriptor < ARRAY_SIZE(descriptors);
	     descriptor++)
		for (size_t bytes = 0U; bytes < ARRAY_SIZE(data); bytes++)
			if (ranges_overlap(descriptors[descriptor],
				descriptor_sizes[descriptor], data[bytes], sizes[bytes]))
				return false;
	for (size_t left = 0U; left < ARRAY_SIZE(data); left++)
		for (size_t right = left + 1U; right < ARRAY_SIZE(data); right++)
			if (ranges_overlap(data[left], sizes[left], data[right],
				sizes[right]))
				return false;
	return true;
}

static enum payload_mm_verify_status digest_inputs(
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content,
	const struct payload_mm_authvar_store_index *index,
	struct protected_digest *digest)
{
	enum payload_mm_verify_status status;
	size_t entries_size = (size_t)index->entry_count *
		sizeof(index->entries[0]);

	status = payload_mm_sha256(signed_data->data, signed_data->size,
		digest->signed_data);
	for (size_t span = 0U; status == PAYLOAD_MM_VERIFY_OK &&
	     span < PRIVATE_CONTENT_SPANS; span++)
		status = payload_mm_sha256(content[span].data, content[span].size,
			digest->content[span]);
	if (status == PAYLOAD_MM_VERIFY_OK)
		status = payload_mm_sha256(index->store, index->store_size,
			digest->store);
	if (status == PAYLOAD_MM_VERIFY_OK)
		status = payload_mm_sha256(index->entries, entries_size,
			digest->entries);
	return status;
}

static const struct payload_mm_authvar_store_entry *find_target(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_crypto_span *content)
{
	const struct payload_mm_crypto_span *name = &content[CONTENT_NAME];
	const uint8_t *guid = content[CONTENT_GUID].data;

	for (uint32_t item = 0U; item < index->entry_count; item++) {
		const struct payload_mm_authvar_store_entry *entry =
			&index->entries[item];
		const uint8_t *stored_name =
			payload_mm_authvar_store_name(index, entry);

		if (entry->name_size == name->size + sizeof(uint16_t) &&
		    !memcmp(entry->vendor_guid, guid, 16U) &&
		    !memcmp(stored_name, name->data, name->size) &&
		    !stored_name[name->size] && !stored_name[name->size + 1U])
			return entry;
	}
	return NULL;
}

static enum payload_mm_verify_status certdb_result(
	enum payload_mm_authvar_certdb_result result)
{
	if (result == PAYLOAD_MM_AUTHVAR_CERTDB_INVALID)
		return PAYLOAD_MM_VERIFY_INVALID;
	if (result == PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED)
		return PAYLOAD_MM_VERIFY_MALFORMED;
	if (result == PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND)
		return PAYLOAD_MM_VERIFY_REJECTED;
	return PAYLOAD_MM_VERIFY_INTERNAL;
}

enum payload_mm_verify_status payload_mm_authvar_private_trust_verify(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content, size_t content_count,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan,
	struct payload_mm_authvar_private_trust_decision *decision)
{
	struct payload_mm_authvar_private_trust_decision draft = { 0 };
	struct payload_mm_authvar_private_binding binding = { 0 };
	struct payload_mm_authvar_certdb_binding stored;
	struct payload_mm_cms_verified_signer verified = { 0 };
	struct payload_mm_crypto_span stored_span;
	struct payload_mm_crypto_span signed_data_copy;
	struct payload_mm_crypto_span content_copy[PRIVATE_CONTENT_SPANS];
	struct payload_mm_authvar_store_index index_copy;
	struct payload_mm_authvar_route_plan plan_copy;
	struct protected_digest before = { 0 };
	struct protected_digest after = { 0 };
	const struct payload_mm_authvar_store_entry *target;
	const struct payload_mm_authvar_store_entry *certdb_entry;
	const void *certdb_data;
	enum payload_mm_authvar_certdb_result lookup;
	enum payload_mm_verify_status status;
	bool target_exists;
	bool needs_certdb;
	bool clean;

	if (!inputs_valid(owner, signed_data, content, content_count, index, plan,
		decision))
		return PAYLOAD_MM_VERIFY_INVALID;
	memset(decision, 0, sizeof(*decision));
	signed_data_copy = *signed_data;
	memcpy(content_copy, content, sizeof(content_copy));
	index_copy = *index;
	plan_copy = *plan;
	status = digest_inputs(&signed_data_copy, content_copy, &index_copy, &before);
	if (status != PAYLOAD_MM_VERIFY_OK)
		goto out;
	target = find_target(&index_copy, content_copy);
	target_exists = target != NULL;
	if ((target_exists && plan_copy.authorities[0] !=
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB) ||
	    (!target_exists && plan_copy.authorities[0] !=
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER)) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
		goto out;
	}
	if (target_exists && target->attributes !=
	    (content_attributes(content_copy) &
	     ~PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND)) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
		goto out;
	}
	status = payload_mm_cms_verify_detached_untrusted(owner, &signed_data_copy,
		content_copy, PRIVATE_CONTENT_SPANS, &verified);
	if (status != PAYLOAD_MM_VERIFY_OK)
		goto out;
	needs_certdb = target_exists || content_copy[CONTENT_PAYLOAD].size;
	if (!needs_certdb) {
		draft.accepted_authority =
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
		status = PAYLOAD_MM_VERIFY_OK;
		goto publish;
	}
	certdb_entry = payload_mm_authvar_store_find(&index_copy, certdb_guid,
		certdb_name, sizeof(certdb_name));
	if (!certdb_entry) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
		goto out;
	}
	if (certdb_entry->attributes != CERTDB_ATTRIBUTES) {
		status = PAYLOAD_MM_VERIFY_MALFORMED;
		goto out;
	}
	certdb_data = payload_mm_authvar_store_data(&index_copy, certdb_entry);
	if (!certdb_data) {
		status = PAYLOAD_MM_VERIFY_INVALID;
		goto out;
	}
	lookup = payload_mm_authvar_certdb_find(certdb_data,
		certdb_entry->data_size, content_copy[CONTENT_GUID].data,
		content_copy[CONTENT_NAME].data, content_copy[CONTENT_NAME].size,
		&stored);
	if (!target_exists) {
		if (lookup != PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND) {
			status = lookup == PAYLOAD_MM_AUTHVAR_CERTDB_OK ?
				PAYLOAD_MM_VERIFY_REJECTED : certdb_result(lookup);
			goto out;
		}
		status = payload_mm_authvar_private_binding_derive(owner,
			&signed_data_copy, &verified, &binding);
		if (status != PAYLOAD_MM_VERIFY_OK)
			goto out;
		draft.accepted_authority =
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
		draft.new_binding_size = binding.size;
		memcpy(draft.new_binding, binding.digest, binding.size);
	} else {
		if (lookup != PAYLOAD_MM_AUTHVAR_CERTDB_OK) {
			status = certdb_result(lookup);
			goto out;
		}
		stored_span = (struct payload_mm_crypto_span) {
			.data = stored.data,
			.size = stored.size,
		};
		status = payload_mm_authvar_private_binding_match(owner,
			&signed_data_copy, &verified, &stored_span);
		if (status != PAYLOAD_MM_VERIFY_OK)
			goto out;
		draft.accepted_authority =
			PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	}
publish:
#ifdef PAYLOAD_MM_AUTH_TEST
	payload_mm_authvar_private_trust_test_before_publish(owner, index, plan,
		decision);
#endif
out:
	if (digest_inputs(&signed_data_copy, content_copy, &index_copy, &after) !=
		PAYLOAD_MM_VERIFY_OK ||
	    memcmp(signed_data, &signed_data_copy, sizeof(*signed_data)) ||
	    memcmp(content, content_copy, sizeof(content_copy)) ||
	    memcmp(index, &index_copy, sizeof(*index)) ||
	    memcmp(plan, &plan_copy, sizeof(*plan)) ||
	    memcmp(&before, &after, sizeof(before)) ||
	    memcmp(decision,
		&(struct payload_mm_authvar_private_trust_decision) { 0 },
		sizeof(*decision)))
		status = PAYLOAD_MM_VERIFY_CHANGED;
	clean = payload_mm_crypto_idle() && payload_mm_crypto_owner_is_clean(owner);
	if (!clean)
		payload_mm_crypto_abort(owner);
	memset(decision, 0, sizeof(*decision));
	if (!clean && status != PAYLOAD_MM_VERIFY_CHANGED)
		status = PAYLOAD_MM_VERIFY_INTERNAL;
	if (status == PAYLOAD_MM_VERIFY_OK)
		*decision = draft;
	mbedtls_platform_zeroize(&verified, sizeof(verified));
	mbedtls_platform_zeroize(&binding, sizeof(binding));
	mbedtls_platform_zeroize(&draft, sizeof(draft));
	mbedtls_platform_zeroize(&before, sizeof(before));
	mbedtls_platform_zeroize(&after, sizeof(after));
	return status;
}
