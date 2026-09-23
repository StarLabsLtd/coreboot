/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_format.h>
#include <boot/payload_mm_authvar_signature_db.h>
#include <boot/payload_mm_authvar_trust_store.h>

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

static uint16_t read_le16(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool range_valid(const void *data, size_t size)
{
	return size == 0U ||
		(data != NULL && (uintptr_t)data <= UINTPTR_MAX - (size - 1U));
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

static bool span_inside(size_t limit, uint32_t offset, uint32_t size)
{
	return offset <= limit && size <= limit - offset;
}

static bool name_valid(const uint8_t *name, uint32_t size)
{
	if (size < sizeof(uint16_t) || (size & 1U) || name[size - 2U] ||
	    name[size - 1U])
		return false;
	for (uint32_t offset = 0U; offset + sizeof(uint16_t) < size;
	     offset += sizeof(uint16_t))
		if (!name[offset] && !name[offset + 1U])
			return false;
	return true;
}

static bool entry_matches_record(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *entry)
{
	const uint8_t *header;
	size_t data_offset;

	if (!span_inside(index->used_size, entry->record_offset,
		PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE) ||
	    entry->name_offset != entry->record_offset +
		PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE ||
	    !span_inside(index->used_size, entry->name_offset, entry->name_size) ||
	    !span_inside(index->used_size, entry->data_offset, entry->data_size) ||
	    entry->name_size > index->maximum_name_size ||
	    entry->data_size > index->maximum_data_size)
		return false;
	header = index->store + entry->record_offset;
	data_offset = (size_t)entry->name_offset + entry->name_size;
	if (data_offset > SIZE_MAX - 3U)
		return false;
	data_offset = (data_offset + 3U) & ~(size_t)3U;
	return data_offset == entry->data_offset && read_le16(header) ==
		PAYLOAD_MM_AUTHVAR_RECORD_START_ID && header[3] == 0U &&
		(header[2] == PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
		 header[2] == PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION) &&
		read_le32(header + 4U) == entry->attributes &&
		read_le32(header + 36U) == entry->name_size &&
		read_le32(header + 40U) == entry->data_size &&
		!memcmp(header + 44U, entry->vendor_guid,
			sizeof(entry->vendor_guid)) &&
		name_valid(index->store + entry->name_offset, entry->name_size);
}

static bool index_safe(const struct payload_mm_authvar_store_index *index)
{
	size_t entry_bytes;

	if (index == NULL || (uintptr_t)index % _Alignof(*index) ||
	    !range_valid(index, sizeof(*index)) || !index->store || !index->entries ||
	    index->store_size < PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE ||
	    index->store_size > PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_SIZE ||
	    index->used_size < PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE ||
	    index->used_size > index->store_size ||
	    index->record_count > index->maximum_records ||
	    index->entry_count > index->entry_capacity ||
	    index->entry_count > index->record_count ||
	    index->entry_count > index->maximum_records ||
	    index->maximum_name_size < sizeof(uint16_t) ||
	    index->maximum_name_size >
		PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE ||
	    (index->maximum_name_size & 1U) || !index->maximum_data_size ||
	    index->maximum_data_size >
		PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_DATA_SIZE ||
	    !index->maximum_records || index->maximum_records >
		PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_RECORDS ||
	    __builtin_mul_overflow(
		(size_t)index->entry_count, sizeof(index->entries[0]),
		&entry_bytes))
		return false;
	if (!range_valid(index->store, index->store_size) ||
	    !range_valid(index->entries, entry_bytes))
		return false;
	for (uint32_t entry = 0U; entry < index->entry_count; entry++)
		if (!entry_matches_record(index, &index->entries[entry]))
			return false;
	return true;
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
	const struct payload_mm_authvar_route_plan *plan)
{
	struct payload_mm_authvar_store_index index_snapshot;
	struct payload_mm_authvar_route_plan plan_snapshot;
	const struct payload_mm_authvar_store_entry *entry;
	const void *data;
	enum payload_mm_verify_status status;

	if (!index_safe(index) || !plan_valid(plan) ||
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
		if (status == PAYLOAD_MM_VERIFY_OK)
			return status;
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
	const struct payload_mm_authvar_route_plan *plan)
{
	struct payload_mm_cms_verified_signer verified = { 0 };
	enum payload_mm_verify_status status;

	/* Protect scanner and route state before crypto can write or wipe owner. */
	if (!index_safe(index) || !plan_valid(plan) ||
	    !inputs_disjoint(owner, signed_data, &verified, index, plan))
		return PAYLOAD_MM_VERIFY_INVALID;
	status = payload_mm_cms_verify_detached_untrusted(owner, signed_data,
		content, content_count, &verified);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	return verify_with_signer(owner, signed_data, &verified, index, plan);
}
