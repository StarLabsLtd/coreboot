/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_candidate.h>
#include <boot/payload_mm_authvar_certdb.h>
#include <boot/payload_mm_authvar_format.h>
#include <boot/payload_mm_authvar_record.h>
#include <boot/payload_mm_authvar_service.h>
#include <commonlib/helpers.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"
#include "payload_mm_crypto/crypto.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM authenticated-variable candidate must only be built in SMM"
#endif

static const u8 vendor_keys_nv_guid[16] = {
	0xe0, 0xe4, 0x73, 0x90, 0xec, 0x60, 0x6e, 0x4b,
	0x99, 0x03, 0x4c, 0x22, 0x3c, 0x26, 0x0f, 0x3c,
};
static const u8 global_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static const u8 image_guid[16] = {
	0xcb, 0xb2, 0x19, 0xd7, 0x3a, 0x3d, 0x96, 0x45,
	0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f,
};
static const u8 secure_boot_enable_guid[16] = {
	0xc7, 0x0b, 0xa3, 0xf0, 0x08, 0xaf, 0x56, 0x45,
	0x99, 0xc4, 0x00, 0x10, 0x09, 0xc9, 0x3a, 0x44,
};
static const u8 certdb_guid[16] = {
	0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
	0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
};
static const u8 vendor_keys_nv_name[] = {
	'V', 0, 'e', 0, 'n', 0, 'd', 0, 'o', 0, 'r', 0, 'K', 0, 'e', 0,
	'y', 0, 's', 0, 'N', 0, 'v', 0, 0, 0,
};
static const u8 secure_boot_enable_name[] = {
	'S', 0, 'e', 0, 'c', 0, 'u', 0, 'r', 0, 'e', 0, 'B', 0, 'o', 0,
	'o', 0, 't', 0, 'E', 0, 'n', 0, 'a', 0, 'b', 0, 'l', 0, 'e', 0,
	0, 0,
};
static const u8 pk_name[] = { 'P', 0, 'K', 0, 0, 0 };
static const u8 kek_name[] = { 'K', 0, 'E', 0, 'K', 0, 0, 0 };
static const u8 db_name[] = { 'd', 0, 'b', 0, 0, 0 };
static const u8 dbx_name[] = { 'd', 0, 'b', 0, 'x', 0, 0, 0 };
static const u8 dbt_name[] = { 'd', 0, 'b', 0, 't', 0, 0, 0 };
static const u8 certdb_name[] = {
	'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 0, 0,
};

static bool range_valid(const void *data, size_t size)
{
	return !size || (data && (uintptr_t)data <= UINTPTR_MAX - size);
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
	return left_address <= right_address ?
		right_address - left_address < left_size :
		left_address - right_address < right_size;
}

static bool bytes_are_zero(const u8 *bytes, size_t size)
{
	u8 combined = 0U;

	while (size--)
		combined |= *bytes++;
	return combined == 0U;
}

static int timestamp_compare(const u8 left[16], const uint8_t right[16])
{
	static const u8 fields[] = { 2U, 3U, 4U, 5U, 6U };
	const u16 left_year = (uint16_t)left[0] |
		(uint16_t)left[1] << 8;
	const u16 right_year = (uint16_t)right[0] |
		(uint16_t)right[1] << 8;

	if (left_year != right_year)
		return left_year < right_year ? -1 : 1;
	for (size_t i = 0U; i < ARRAY_SIZE(fields); i++)
		if (left[fields[i]] != right[fields[i]])
			return left[fields[i]] < right[fields[i]] ? -1 : 1;
	return 0;
}

static bool canonical_name(const void *name, size_t size)
{
	const u8 *bytes = name;

	if (!range_valid(name, size) || size < 2U * sizeof(uint16_t) ||
	    size > UINT32_MAX || size % sizeof(uint16_t) ||
	    bytes[size - 2U] || bytes[size - 1U])
		return false;
	for (size_t i = 0U; i + sizeof(uint16_t) < size; i += sizeof(uint16_t))
		if (!bytes[i] && !bytes[i + 1U])
			return false;
	return true;
}

static bool key_is(const u8 guid[16], const void *name, size_t name_size,
		   const u8 expected_guid[16], const uint8_t *expected_name,
	size_t expected_name_size)
{
	return name_size == expected_name_size &&
		!memcmp(guid, expected_guid, 16U) &&
		!memcmp(name, expected_name, name_size);
}

static bool key_equal(const u8 left_guid[16], const void *left_name,
		      size_t left_size, const uint8_t right_guid[16], const void *right_name,
	size_t right_size)
{
	return left_size == right_size && !memcmp(left_guid, right_guid, 16U) &&
		!memcmp(left_name, right_name, left_size);
}

static bool private_key(const u8 guid[16], const void *name, size_t name_size)
{
	return !payload_mm_authvar_bundle_key_reserved(guid, name, name_size) &&
		!key_is(guid, name, name_size, global_guid, pk_name,
			sizeof(pk_name)) &&
		!key_is(guid, name, name_size, global_guid, kek_name,
			sizeof(kek_name)) &&
		!key_is(guid, name, name_size, image_guid, db_name,
			sizeof(db_name)) &&
		!key_is(guid, name, name_size, image_guid, dbx_name,
			sizeof(dbx_name)) &&
		!key_is(guid, name, name_size, image_guid, dbt_name,
			sizeof(dbt_name));
}

static bool mutation_valid(const struct payload_mm_authvar_bundle_mutation *item,
			   enum payload_mm_authvar_bundle_role role)
{
	const u32 nv_bs = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	const u32 nv_bs_time = nv_bs |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	const bool delete = item->mutation.kind ==
		PAYLOAD_MM_AUTHVAR_MUTATION_DELETE;

	if (item->role != role || !canonical_name(item->name, item->name_size) ||
	    (item->data_size && !range_valid(item->data, item->data_size)) ||
	    item->mutation.data_size != item->data_size ||
	    (item->mutation.kind != PAYLOAD_MM_AUTHVAR_MUTATION_WRITE && !delete))
		return false;
	if (delete) {
		if (item->mutation.attributes || item->data_size || item->data ||
		    !bytes_are_zero(item->mutation.timestamp, 16U))
			return false;
	} else if (!item->data_size || !item->data) {
		return false;
	}
	if (role == PAYLOAD_MM_AUTHVAR_BUNDLE_TARGET)
		return !key_is(item->vendor_guid, item->name, item->name_size,
			secure_boot_enable_guid, secure_boot_enable_name,
			sizeof(secure_boot_enable_name)) &&
			!key_is(item->vendor_guid, item->name, item->name_size,
			vendor_keys_nv_guid, vendor_keys_nv_name,
			sizeof(vendor_keys_nv_name)) &&
			!payload_mm_authvar_bundle_key_reserved(item->vendor_guid,
				item->name, item->name_size) &&
			(delete || ((item->mutation.attributes & nv_bs) == nv_bs &&
			 (item->mutation.attributes &
			  PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED) &&
			 !(item->mutation.attributes &
			  (PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR |
			   PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE |
			   PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE)) &&
			 !(item->mutation.attributes &
			   ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED) &&
			 payload_mm_authvar_timestamp_store_valid(
				item->mutation.timestamp) &&
			 !bytes_are_zero(item->mutation.timestamp, 16U)));
	if (role == PAYLOAD_MM_AUTHVAR_BUNDLE_CERTDB)
		return key_is(item->vendor_guid, item->name, item->name_size,
			certdb_guid, certdb_name, sizeof(certdb_name)) && !delete &&
			item->mutation.attributes ==
				(nv_bs | PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
				 PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED) &&
			item->data_size >= sizeof(uint32_t) &&
			bytes_are_zero(item->mutation.timestamp, 16U);
	if (role == PAYLOAD_MM_AUTHVAR_BUNDLE_SECURE_BOOT_ENABLE)
		return key_is(item->vendor_guid, item->name, item->name_size,
			secure_boot_enable_guid, secure_boot_enable_name,
			sizeof(secure_boot_enable_name)) &&
			(delete || (item->mutation.attributes == nv_bs &&
			 item->data_size == 1U &&
			 *(const uint8_t *)item->data == 1U &&
			 bytes_are_zero(item->mutation.timestamp, 16U)));
	return key_is(item->vendor_guid, item->name, item->name_size,
		vendor_keys_nv_guid, vendor_keys_nv_name,
		sizeof(vendor_keys_nv_name)) && !delete &&
		item->mutation.attributes == nv_bs_time && item->data_size == 1U &&
		!*(const uint8_t *)item->data &&
		bytes_are_zero(item->mutation.timestamp, 16U);
}

static const struct payload_mm_authvar_bundle_mutation *bundle_role(
	const struct payload_mm_authvar_bundle_plan *bundle,
	enum payload_mm_authvar_bundle_role role);

static bool bundle_valid(const struct payload_mm_authvar_bundle_plan *bundle,
	bool at_runtime)
{
	size_t next = 1U;

	if (!bundle || bundle->outcome != PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION ||
	    bundle->mutation_count < 1U ||
	    bundle->mutation_count > PAYLOAD_MM_AUTHVAR_BUNDLE_MAX_MUTATIONS ||
	    bundle->volatile_modes & ~(PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS) ||
	    !mutation_valid(&bundle->mutations[0],
		PAYLOAD_MM_AUTHVAR_BUNDLE_TARGET))
		return false;
	if (at_runtime && bundle->mutations[0].mutation.kind ==
		PAYLOAD_MM_AUTHVAR_MUTATION_WRITE &&
	    !(bundle->mutations[0].mutation.attributes &
	      PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS))
		return false;
	if (next < bundle->mutation_count && bundle->mutations[next].role ==
	    PAYLOAD_MM_AUTHVAR_BUNDLE_CERTDB &&
	    !mutation_valid(&bundle->mutations[next++],
		PAYLOAD_MM_AUTHVAR_BUNDLE_CERTDB))
		return false;
	if (next < bundle->mutation_count && bundle->mutations[next].role ==
	    PAYLOAD_MM_AUTHVAR_BUNDLE_SECURE_BOOT_ENABLE &&
	    !mutation_valid(&bundle->mutations[next++],
		PAYLOAD_MM_AUTHVAR_BUNDLE_SECURE_BOOT_ENABLE))
		return false;
	if (next < bundle->mutation_count && bundle->mutations[next].role ==
	    PAYLOAD_MM_AUTHVAR_BUNDLE_VENDOR_KEYS_NV &&
	    !mutation_valid(&bundle->mutations[next++],
		PAYLOAD_MM_AUTHVAR_BUNDLE_VENDOR_KEYS_NV))
		return false;
	if (next != bundle->mutation_count)
		return false;
	if (!bundle_role(bundle, PAYLOAD_MM_AUTHVAR_BUNDLE_CERTDB))
		return bundle->certdb_operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD &&
			!bundle->private_binding_size &&
			bytes_are_zero(bundle->private_binding,
				sizeof(bundle->private_binding));
	if (bundle->mutation_count != 2U ||
	    bundle->mutations[1].role != PAYLOAD_MM_AUTHVAR_BUNDLE_CERTDB)
		return false;
	if (bundle->certdb_operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD)
		return (bundle->private_binding_size == 32U ||
			bundle->private_binding_size == 48U ||
			bundle->private_binding_size == 64U) &&
			bytes_are_zero(bundle->private_binding +
				bundle->private_binding_size,
				sizeof(bundle->private_binding) -
				bundle->private_binding_size);
	return bundle->certdb_operation == PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE &&
		!bundle->private_binding_size &&
		bytes_are_zero(bundle->private_binding,
			sizeof(bundle->private_binding));
}

static const struct payload_mm_authvar_bundle_mutation *bundle_role(
	const struct payload_mm_authvar_bundle_plan *bundle,
	enum payload_mm_authvar_bundle_role role)
{
	for (size_t i = 0U; i < bundle->mutation_count; i++)
		if (bundle->mutations[i].role == role)
			return &bundle->mutations[i];
	return NULL;
}

static bool certdb_replacement_matches(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_bundle_plan *bundle, void *workspace,
	size_t workspace_size, bool at_runtime)
{
	const uint32_t attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	const struct payload_mm_authvar_bundle_mutation *target =
		&bundle->mutations[0];
	const struct payload_mm_authvar_bundle_mutation *certdb =
		bundle_role(bundle, PAYLOAD_MM_AUTHVAR_BUNDLE_CERTDB);
	const struct payload_mm_authvar_store_entry *source_target;
	const struct payload_mm_authvar_store_entry *source_certdb;
	const void *source_data;
	size_t expected_size;
	enum payload_mm_authvar_certdb_result result;

	if (!certdb)
		return true;
	if (!private_key(target->vendor_guid, target->name, target->name_size))
		return false;
	source_target = payload_mm_authvar_store_find(index, target->vendor_guid,
		target->name, target->name_size);
	if ((bundle->certdb_operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD &&
	     (source_target || target->mutation.kind !=
		PAYLOAD_MM_AUTHVAR_MUTATION_WRITE)) ||
	    (bundle->certdb_operation == PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE &&
	     (!source_target || target->mutation.kind !=
		PAYLOAD_MM_AUTHVAR_MUTATION_DELETE)))
		return false;
	if (source_target &&
	    ((source_target->attributes &
	      (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
	       PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
	       PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED)) !=
	     (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
	      PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
	      PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED) ||
	     source_target->attributes &
		(PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE |
		 PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE |
		 PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR) ||
	     source_target->attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED ||
	     (at_runtime && !(source_target->attributes &
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)) ||
	     !payload_mm_authvar_timestamp_store_valid(index->store +
		source_target->record_offset + 16U) ||
	     bytes_are_zero(index->store + source_target->record_offset + 16U,
		16U)))
		return false;
	source_certdb = payload_mm_authvar_store_find(index, certdb_guid,
		certdb_name, sizeof(certdb_name));
	if (!source_certdb || source_certdb->attributes != attributes ||
	    !bytes_are_zero(index->store + source_certdb->record_offset + 16U,
		16U))
		return false;
	source_data = payload_mm_authvar_store_data(index, source_certdb);
	if (!source_data)
		return false;
	result = payload_mm_authvar_certdb_compose(source_data,
		source_certdb->data_size, bundle->certdb_operation,
		target->vendor_guid, target->name,
		target->name_size - sizeof(uint16_t),
		bundle->certdb_operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD ?
			bundle->private_binding : NULL,
		bundle->certdb_operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD ?
			bundle->private_binding_size : 0U,
		workspace, workspace_size, &expected_size);
	return result == PAYLOAD_MM_AUTHVAR_CERTDB_OK &&
		expected_size == certdb->data_size &&
		!memcmp(workspace, certdb->data, expected_size);
}

static bool index_equal(const struct payload_mm_authvar_store_index *left,
			const struct payload_mm_authvar_store_index *right)
{
	return left->store == right->store &&
		left->store_size == right->store_size &&
		left->used_size == right->used_size &&
		left->dirty_tail_offset == right->dirty_tail_offset &&
		left->record_count == right->record_count &&
		left->entry_count == right->entry_count &&
		left->maximum_name_size == right->maximum_name_size &&
		left->maximum_data_size == right->maximum_data_size &&
		left->maximum_records == right->maximum_records &&
		!memcmp(left->entries, right->entries,
			(size_t)left->entry_count * sizeof(left->entries[0]));
}

static bool mutations_fit_and_match(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_bundle_plan *bundle,
	const struct payload_mm_authvar_write_policy *policy)
{
	for (size_t i = 0U; i < bundle->mutation_count; i++) {
		const struct payload_mm_authvar_bundle_mutation *item =
			&bundle->mutations[i];
		const struct payload_mm_authvar_store_entry *entry =
			payload_mm_authvar_store_find(index, item->vendor_guid,
						      item->name, item->name_size);
		size_t record_size;
		size_t data_offset;

		if (item->mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_DELETE) {
			if (!entry)
				return false;
			continue;
		}
		if (entry && entry->attributes != item->mutation.attributes)
			return false;
		if (entry && timestamp_compare(item->mutation.timestamp,
					       index->store + entry->record_offset + 16U) < 0)
			return false;
		if (item->name_size > policy->maximum_name_size ||
		    item->data_size > policy->maximum_data_size ||
		    !payload_mm_authvar_record_layout(item->name_size,
			item->data_size, &record_size, &data_offset) ||
		    record_size > policy->maximum_record_size)
			return false;
	}
	return true;
}

static bool mutated_key(const struct payload_mm_authvar_bundle_plan *bundle,
			const u8 guid[16], const void *name, size_t name_size)
{
	for (size_t i = 0U; i < bundle->mutation_count; i++)
		if (key_equal(guid, name, name_size,
			      bundle->mutations[i].vendor_guid,
			bundle->mutations[i].name,
			bundle->mutations[i].name_size))
			return true;
	return false;
}

static bool encode_at(u8 *candidate, size_t capacity, size_t *used,
		      const struct payload_mm_authvar_record_descriptor *descriptor,
	const void *data, size_t data_size,
	enum payload_mm_authvar_record_timestamp timestamp_mode)
{
	const struct payload_mm_authvar_record_span span = {
		.data = data,
		.size = data_size,
	};
	u32 record_size;

	if (*used > capacity || !payload_mm_authvar_record_encode(descriptor,
								  &span, 1U, timestamp_mode, candidate + *used, capacity - *used,
		&record_size))
		return false;
	candidate[*used + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	*used += record_size;
	return true;
}

static bool emit_survivors(const struct payload_mm_authvar_store_index *index,
			   const struct payload_mm_authvar_bundle_plan *bundle, uint8_t state,
	u8 *candidate, size_t capacity, size_t *used, uint32_t *count)
{
	for (u32 i = 0U; i < index->entry_count; i++) {
		const struct payload_mm_authvar_store_entry *entry = &index->entries[i];
		const u8 *header = index->store + entry->record_offset;
		const void *name;
		size_t record_size;
		size_t data_offset;

		if (header[2] != state)
			continue;
		name = payload_mm_authvar_store_name(index, entry);
		if (!name || !payload_mm_authvar_store_data(index, entry) ||
		    mutated_key(bundle, entry->vendor_guid, name,
				entry->name_size))
			continue;
		if (!payload_mm_authvar_record_layout(entry->name_size,
						      entry->data_size, &record_size, &data_offset) ||
		    *used > capacity || record_size > capacity - *used)
			return false;
		memcpy(candidate + *used, header, record_size);
		candidate[*used + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
		*used += record_size;
		(*count)++;
	}
	return true;
}

static bool emit_mutations(const struct payload_mm_authvar_bundle_plan *bundle,
			   u8 *candidate, size_t capacity, size_t *used, uint32_t *count)
{
	for (size_t i = 0U; i < bundle->mutation_count; i++) {
		const struct payload_mm_authvar_bundle_mutation *item =
			&bundle->mutations[i];
		struct payload_mm_authvar_record_descriptor descriptor;
		enum payload_mm_authvar_record_timestamp timestamp_mode =
			PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED;

		if (item->mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_DELETE)
			continue;
		descriptor = (struct payload_mm_authvar_record_descriptor) {
			.name = item->name,
			.name_size = item->name_size,
			.attributes = item->mutation.attributes,
		};
		memcpy(descriptor.vendor_guid, item->vendor_guid, 16U);
		memcpy(descriptor.timestamp, item->mutation.timestamp, 16U);
		if (item->role == PAYLOAD_MM_AUTHVAR_BUNDLE_VENDOR_KEYS_NV ||
		    item->role == PAYLOAD_MM_AUTHVAR_BUNDLE_CERTDB)
			timestamp_mode =
				PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO;
		if (!encode_at(candidate, capacity, used, &descriptor, item->data,
			       item->data_size, timestamp_mode))
			return false;
		(*count)++;
	}
	return true;
}

static bool candidate_matches(const struct payload_mm_authvar_store_index *source,
			      const struct payload_mm_authvar_store_index *built,
	const struct payload_mm_authvar_bundle_plan *bundle, size_t used,
	uint32_t expected_count)
{
	if (built->dirty_tail_offset || built->used_size != used ||
	    built->record_count != expected_count ||
	    built->entry_count != expected_count)
		return false;
	for (u32 i = 0U; i < source->entry_count; i++) {
		const struct payload_mm_authvar_store_entry *old = &source->entries[i];
		const void *name = payload_mm_authvar_store_name(source, old);
		const void *data = payload_mm_authvar_store_data(source, old);
		const struct payload_mm_authvar_store_entry *now;

		if (mutated_key(bundle, old->vendor_guid, name, old->name_size))
			continue;
		now = payload_mm_authvar_store_find(built, old->vendor_guid, name,
						    old->name_size);
		if (!now || now->attributes != old->attributes ||
		    now->data_size != old->data_size ||
		    memcmp(payload_mm_authvar_store_data(built, now), data,
			   old->data_size) ||
		    memcmp(built->store + now->record_offset + 16U,
			   source->store + old->record_offset + 16U, 16U))
			return false;
	}
	for (size_t i = 0U; i < bundle->mutation_count; i++) {
		const struct payload_mm_authvar_bundle_mutation *item =
			&bundle->mutations[i];
		const struct payload_mm_authvar_store_entry *entry =
			payload_mm_authvar_store_find(built, item->vendor_guid,
						      item->name, item->name_size);

		if (item->mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_DELETE) {
			if (entry)
				return false;
			continue;
		}
		if (!entry || entry->attributes != item->mutation.attributes ||
		    entry->data_size != item->data_size ||
		    memcmp(payload_mm_authvar_store_data(built, entry), item->data,
			   item->data_size) ||
		    memcmp(built->store + entry->record_offset + 16U,
			   item->mutation.timestamp, 16U))
			return false;
	}
	for (u32 i = 0U; i < built->entry_count; i++)
		if (built->store[built->entries[i].record_offset + 2U] !=
		    PAYLOAD_MM_AUTHVAR_STATE_ADDED)
			return false;
	return true;
}

static bool final_certdb_matches(
	const struct payload_mm_authvar_store_index *built,
	const struct payload_mm_authvar_bundle_plan *bundle)
{
	const struct payload_mm_authvar_bundle_mutation *target =
		&bundle->mutations[0];
	const struct payload_mm_authvar_bundle_mutation *certdb =
		bundle_role(bundle, PAYLOAD_MM_AUTHVAR_BUNDLE_CERTDB);
	const struct payload_mm_authvar_store_entry *entry;
	struct payload_mm_authvar_certdb_binding binding;
	const void *data;
	enum payload_mm_authvar_certdb_result result;

	if (!certdb)
		return true;
	entry = payload_mm_authvar_store_find(built, certdb_guid, certdb_name,
		sizeof(certdb_name));
	if (!entry || entry->attributes != certdb->mutation.attributes ||
	    !bytes_are_zero(built->store + entry->record_offset + 16U, 16U))
		return false;
	data = payload_mm_authvar_store_data(built, entry);
	if (!data)
		return false;
	result = payload_mm_authvar_certdb_find(data, entry->data_size,
		target->vendor_guid, target->name,
		target->name_size - sizeof(uint16_t), &binding);
	if (bundle->certdb_operation == PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE)
		return result == PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND;
	return result == PAYLOAD_MM_AUTHVAR_CERTDB_OK &&
		binding.size == bundle->private_binding_size &&
		!memcmp(binding.data, bundle->private_binding, binding.size);
}

static bool projection_matches(
	const struct payload_mm_authvar_store_index *source,
	const struct payload_mm_authvar_store_index *built,
	const struct payload_mm_authvar_bundle_plan *bundle,
	const struct payload_mm_authvar_candidate_binding *binding)
{
	const struct payload_mm_authvar_bundle_mutation *target =
		bundle_role(bundle, PAYLOAD_MM_AUTHVAR_BUNDLE_TARGET);
	const struct payload_mm_authvar_bundle_mutation *enable =
		bundle_role(bundle, PAYLOAD_MM_AUTHVAR_BUNDLE_SECURE_BOOT_ENABLE);
	const struct payload_mm_authvar_bundle_mutation *vendor_mutation =
		bundle_role(bundle, PAYLOAD_MM_AUTHVAR_BUNDLE_VENDOR_KEYS_NV);
	const struct payload_mm_authvar_store_entry *pk =
		payload_mm_authvar_store_find(built, global_guid, pk_name,
					      sizeof(pk_name));
	const struct payload_mm_authvar_store_entry *vendor =
		payload_mm_authvar_store_find(built, vendor_keys_nv_guid,
					      vendor_keys_nv_name, sizeof(vendor_keys_nv_name));
	const u8 *vendor_data = payload_mm_authvar_store_data(built, vendor);
	const struct payload_mm_authvar_store_entry *source_pk =
		payload_mm_authvar_store_find(source, global_guid, pk_name,
					      sizeof(pk_name));
	const struct payload_mm_authvar_store_entry *source_vendor =
		payload_mm_authvar_store_find(source, vendor_keys_nv_guid,
					      vendor_keys_nv_name, sizeof(vendor_keys_nv_name));
	const struct payload_mm_authvar_store_entry *source_enable =
		payload_mm_authvar_store_find(source, secure_boot_enable_guid,
					      secure_boot_enable_name,
			sizeof(secure_boot_enable_name));
	const u8 *source_vendor_data =
		payload_mm_authvar_store_data(source, source_vendor);
	const u8 *source_enable_data =
		payload_mm_authvar_store_data(source, source_enable);
	const bool source_setup = binding->source_volatile_modes &
		PAYLOAD_MM_AUTHVAR_MODE_SETUP;
	const bool source_secure = binding->source_volatile_modes &
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;
	const bool source_vendor_keys = binding->source_volatile_modes &
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	const bool setup = bundle->volatile_modes & PAYLOAD_MM_AUTHVAR_MODE_SETUP;
	const bool secure = bundle->volatile_modes &
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;
	const bool vendor_keys = bundle->volatile_modes &
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;

	const u32 nv_bs = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	const u32 nv_bs_time = nv_bs |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;

	if (!payload_mm_authvar_candidate_projection_valid(source, built, binding,
		bundle->volatile_modes))
		return false;

	if ((binding->at_runtime && vendor_mutation) ||
	    binding->source_volatile_modes &
		~(PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		  PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		  PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS) ||
	    source_setup == !!source_pk || !source_vendor ||
	    source_vendor->attributes != nv_bs_time ||
	    source_vendor->data_size != 1U || !source_vendor_data ||
	    !bytes_are_zero(source->store + source_vendor->record_offset + 16U,
		16U) ||
	    *source_vendor_data > 1U ||
	    source_vendor_keys != !!*source_vendor_data ||
	    (source_enable && (source_enable->attributes != nv_bs ||
	     source_enable->data_size != 1U || !source_enable_data ||
	     *source_enable_data > 1U ||
	     !bytes_are_zero(source->store + source_enable->record_offset + 16U,
		16U))) ||
	    (!binding->at_runtime &&
	     ((source_setup && source_secure) ||
	      (!source_setup && (!source_enable || !source_enable_data ||
	       source_enable->data_size != 1U || *source_enable_data > 1U ||
	       source_secure != !!*source_enable_data)))) ||
	    setup == !!pk || !vendor || vendor->attributes != nv_bs_time ||
	    vendor->data_size != 1U ||
	    !vendor_data || *vendor_data > 1U || vendor_keys != !!*vendor_data)
		return false;
	if (!enable)
		return secure == source_secure;
	/* Only a PK transition may persist the matching enable-state change. */
	if (binding->at_runtime || !target || !key_is(target->vendor_guid, target->name,
						      target->name_size, global_guid, pk_name, sizeof(pk_name)))
		return false;
	if (enable->mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_WRITE)
		return pk != NULL && !setup && secure;
	return !pk && setup && !secure &&
		payload_mm_authvar_store_find(source, secure_boot_enable_guid,
					      secure_boot_enable_name,
			sizeof(secure_boot_enable_name)) != NULL;
}

bool payload_mm_authvar_candidate_projection_valid(
	const struct payload_mm_authvar_store_index *source,
	const struct payload_mm_authvar_store_index *candidate,
	const struct payload_mm_authvar_candidate_binding *binding,
	u8 volatile_modes)
{
	const u32 nv_bs = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	const u32 nv_bs_time = nv_bs |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	const u8 mode_mask = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	const struct payload_mm_authvar_store_entry *source_pk;
	const struct payload_mm_authvar_store_entry *candidate_pk;
	const struct payload_mm_authvar_store_entry *source_enable;
	const struct payload_mm_authvar_store_entry *candidate_enable;
	const struct payload_mm_authvar_store_entry *source_vendor;
	const struct payload_mm_authvar_store_entry *candidate_vendor;
	const u8 *source_enable_data;
	const u8 *candidate_enable_data;
	const u8 *source_vendor_data;
	const u8 *candidate_vendor_data;
	bool source_secure;
	bool secure;
	bool enable_changed;

	if (!payload_mm_authvar_store_index_valid(source) ||
	    !payload_mm_authvar_store_index_valid(candidate) || !binding ||
	    binding->at_runtime > 1U ||
	    binding->source_volatile_modes & ~mode_mask ||
	    volatile_modes & ~mode_mask)
		return false;
	source_pk = payload_mm_authvar_store_find(source, global_guid, pk_name,
		sizeof(pk_name));
	candidate_pk = payload_mm_authvar_store_find(candidate, global_guid, pk_name,
		sizeof(pk_name));
	source_enable = payload_mm_authvar_store_find(source, secure_boot_enable_guid,
		secure_boot_enable_name, sizeof(secure_boot_enable_name));
	candidate_enable = payload_mm_authvar_store_find(candidate,
		secure_boot_enable_guid, secure_boot_enable_name,
		sizeof(secure_boot_enable_name));
	source_vendor = payload_mm_authvar_store_find(source, vendor_keys_nv_guid,
		vendor_keys_nv_name, sizeof(vendor_keys_nv_name));
	candidate_vendor = payload_mm_authvar_store_find(candidate,
		vendor_keys_nv_guid, vendor_keys_nv_name, sizeof(vendor_keys_nv_name));
	source_enable_data = payload_mm_authvar_store_data(source, source_enable);
	candidate_enable_data = payload_mm_authvar_store_data(candidate,
		candidate_enable);
	source_vendor_data = payload_mm_authvar_store_data(source, source_vendor);
	candidate_vendor_data = payload_mm_authvar_store_data(candidate,
		candidate_vendor);
	if (!!(binding->source_volatile_modes & PAYLOAD_MM_AUTHVAR_MODE_SETUP) ==
	    !!source_pk || !source_vendor || !source_vendor_data ||
	    source_vendor->attributes != nv_bs_time ||
	    source_vendor->data_size != 1U || *source_vendor_data > 1U ||
	    !bytes_are_zero(source->store + source_vendor->record_offset + 16U,
		16U) ||
	    !!(binding->source_volatile_modes &
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS) != !!*source_vendor_data ||
	    !candidate_vendor || !candidate_vendor_data ||
	    candidate_vendor->attributes != nv_bs_time ||
	    candidate_vendor->data_size != 1U || *candidate_vendor_data > 1U ||
	    !bytes_are_zero(candidate->store + candidate_vendor->record_offset + 16U,
		16U) ||
	    !!(volatile_modes & PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS) !=
		!!*candidate_vendor_data ||
	    !!(volatile_modes & PAYLOAD_MM_AUTHVAR_MODE_SETUP) == !!candidate_pk ||
	    (source_enable && (!source_enable_data ||
	     source_enable->attributes != nv_bs || source_enable->data_size != 1U ||
	     *source_enable_data > 1U ||
	     !bytes_are_zero(source->store + source_enable->record_offset + 16U,
		16U))) ||
	    (candidate_enable && (!candidate_enable_data ||
	     candidate_enable->attributes != nv_bs ||
	     candidate_enable->data_size != 1U || *candidate_enable_data > 1U ||
	     !bytes_are_zero(candidate->store + candidate_enable->record_offset + 16U,
		16U))))
		return false;
	source_secure = binding->source_volatile_modes &
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;
	secure = volatile_modes & PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;
	enable_changed = !!source_enable != !!candidate_enable ||
		(source_enable && *source_enable_data != *candidate_enable_data);
	if ((source_pk && !source_enable) ||
	    (candidate_pk && !candidate_enable))
		return false;
	if (binding->at_runtime)
		return !enable_changed && secure == source_secure;
	return source_secure ==
		(!!source_pk && !!source_enable && !!*source_enable_data) &&
		secure ==
		(!!candidate_pk && !!candidate_enable && !!*candidate_enable_data);
}

static bool result_disjoint_from_inputs(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_bundle_plan *bundle,
	const struct payload_mm_authvar_write_policy *policy,
	const struct payload_mm_authvar_candidate_binding *binding,
	const void *candidate, struct payload_mm_authvar_store_entry *scan_entries,
	size_t scan_entry_capacity,
	struct payload_mm_authvar_candidate_result *result,
	size_t *scan_entry_bytes)
{
	size_t index_entry_bytes;
	size_t mutation_count = MIN((size_t)bundle->mutation_count,
		(size_t)PAYLOAD_MM_AUTHVAR_BUNDLE_MAX_MUTATIONS);

	if (__builtin_mul_overflow(scan_entry_capacity, sizeof(*scan_entries),
				   scan_entry_bytes) ||
	    __builtin_mul_overflow((size_t)index->entry_count,
				   sizeof(index->entries[0]), &index_entry_bytes) ||
	    !range_valid(index->store, index->store_size) ||
	    !range_valid(index->entries, index_entry_bytes) ||
	    !range_valid(candidate, index->store_size) ||
	    !range_valid(scan_entries, *scan_entry_bytes))
		return false;
	if (ranges_overlap(result, sizeof(*result), index, sizeof(*index)) ||
	    ranges_overlap(result, sizeof(*result), bundle, sizeof(*bundle)) ||
	    ranges_overlap(result, sizeof(*result), policy, sizeof(*policy)) ||
	    ranges_overlap(result, sizeof(*result), binding, sizeof(*binding)) ||
	    ranges_overlap(result, sizeof(*result), candidate, index->store_size) ||
	    ranges_overlap(result, sizeof(*result), scan_entries,
			   *scan_entry_bytes) ||
	    ranges_overlap(result, sizeof(*result), index->store,
			   index->store_size) ||
	    ranges_overlap(result, sizeof(*result), index->entries,
			   index_entry_bytes))
		return false;
	for (size_t i = 0U; i < mutation_count; i++) {
		const struct payload_mm_authvar_bundle_mutation *item =
			&bundle->mutations[i];

		if (!range_valid(item->name, item->name_size) ||
		    !range_valid(item->data, item->data_size) ||
		    ranges_overlap(result, sizeof(*result), item->name,
				   item->name_size) ||
		    ranges_overlap(result, sizeof(*result), item->data,
				   item->data_size))
			return false;
	}
	return true;
}

uint64_t payload_mm_authvar_candidate_build(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_bundle_plan *bundle,
	const struct payload_mm_authvar_write_policy *policy,
	const struct payload_mm_authvar_candidate_binding *binding,
	void *candidate_buffer, size_t candidate_capacity,
	struct payload_mm_authvar_store_entry *scan_entries,
	size_t scan_entry_capacity,
	struct payload_mm_authvar_candidate_result *result)
{
	struct payload_mm_authvar_candidate_result draft = { 0 };
	struct payload_mm_authvar_store_index scan = {
		.entries = scan_entries,
		.entry_capacity = scan_entry_capacity <= UINT32_MAX ?
			(uint32_t)scan_entry_capacity : 0U,
	};
	struct payload_mm_authvar_store_limits limits;
	u8 *candidate = candidate_buffer;
	size_t entry_bytes;
	size_t used = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	u32 count = 0U;
	enum payload_mm_verify_status hash_status;

	/* Prove the typed top-level objects before dereferencing any of them. */
	if (!index || !bundle || !policy || !binding || !candidate ||
	    !scan_entries || !result || (uintptr_t)index % _Alignof(*index) ||
	    (uintptr_t)bundle % _Alignof(*bundle) ||
	    (uintptr_t)policy % _Alignof(*policy) ||
	    (uintptr_t)binding % _Alignof(*binding) ||
	    (uintptr_t)scan_entries % _Alignof(*scan_entries) ||
	    (uintptr_t)result % _Alignof(*result) ||
	    !range_valid(index, sizeof(*index)) ||
	    !range_valid(bundle, sizeof(*bundle)) ||
	    !range_valid(policy, sizeof(*policy)) ||
	    !range_valid(binding, sizeof(*binding)) ||
	    !range_valid(result, sizeof(*result)))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	/* Alias/range failure leaves result untouched because clearing is unsafe. */
	if (!result_disjoint_from_inputs(index, bundle, policy, binding,
					 candidate, scan_entries, scan_entry_capacity, result, &entry_bytes))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	memset(result, 0, sizeof(*result));
	if (!payload_mm_authvar_store_index_valid(index) ||
	    !bundle_valid(bundle, binding->at_runtime) ||
	    !binding->generation || !binding->token || binding->at_runtime > 1U ||
	    !bytes_are_zero(binding->reserved, sizeof(binding->reserved)) ||
	    scan_entry_capacity < index->maximum_records ||
	    scan_entry_capacity > UINT32_MAX ||
	    candidate_capacity < index->store_size ||
	    !range_valid(candidate, index->store_size) ||
	    !range_valid(scan_entries, entry_bytes) ||
	    ranges_overlap(candidate, index->store_size, scan_entries, entry_bytes) ||
	    ranges_overlap(candidate, index->store_size, index, sizeof(*index)) ||
	    ranges_overlap(candidate, index->store_size, bundle, sizeof(*bundle)) ||
	    ranges_overlap(candidate, index->store_size, policy, sizeof(*policy)) ||
	    ranges_overlap(candidate, index->store_size, binding, sizeof(*binding)) ||
	    ranges_overlap(scan_entries, entry_bytes, index, sizeof(*index)) ||
	    ranges_overlap(scan_entries, entry_bytes, bundle, sizeof(*bundle)) ||
	    ranges_overlap(scan_entries, entry_bytes, policy, sizeof(*policy)) ||
	    ranges_overlap(scan_entries, entry_bytes, binding, sizeof(*binding)) ||
	    ranges_overlap(candidate, index->store_size, index->store,
			   index->store_size) ||
	    ranges_overlap(candidate, index->store_size, index->entries,
			   (size_t)index->entry_count * sizeof(index->entries[0])) ||
	    ranges_overlap(scan_entries, entry_bytes, index->store,
			   index->store_size) ||
	    ranges_overlap(scan_entries, entry_bytes, index->entries,
			   (size_t)index->entry_count * sizeof(index->entries[0])) ||
	    policy->maximum_name_size < 4U ||
	    (policy->maximum_name_size & 1U) ||
	    policy->maximum_name_size > index->maximum_name_size ||
	    policy->maximum_data_size > index->maximum_data_size ||
	    policy->maximum_records > index->maximum_records ||
	    !policy->maximum_data_size || !policy->maximum_records ||
	    policy->maximum_data_size > policy->maximum_record_size -
		PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE ||
	    policy->maximum_record_size < PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE ||
	    policy->maximum_record_size > index->store_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	for (size_t i = 0U; i < bundle->mutation_count; i++) {
		const struct payload_mm_authvar_bundle_mutation *item =
			&bundle->mutations[i];

		if (ranges_overlap(candidate, index->store_size, item->name,
				   item->name_size) ||
		    ranges_overlap(candidate, index->store_size, item->data,
				   item->data_size) ||
		    ranges_overlap(scan_entries, entry_bytes, item->name,
				   item->name_size) ||
		    ranges_overlap(scan_entries, entry_bytes, item->data,
				   item->data_size))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	}

	limits = (struct payload_mm_authvar_store_limits) {
		.maximum_store_size = index->store_size,
		.maximum_name_size = index->maximum_name_size,
		.maximum_data_size = index->maximum_data_size,
		.maximum_records = index->maximum_records,
	};
	if (payload_mm_authvar_store_scan(&scan, index->store, index->store_size,
				  &limits) != CB_SUCCESS || !index_equal(index, &scan) ||
	    !mutations_fit_and_match(index, bundle, policy) ||
	    !certdb_replacement_matches(index, bundle, candidate,
		index->store_size, binding->at_runtime))
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	memset(candidate, 0xff, index->store_size);
	memcpy(candidate, index->store, PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE);
	if (!emit_survivors(index, bundle, PAYLOAD_MM_AUTHVAR_STATE_ADDED,
			    candidate, index->store_size, &used, &count) ||
	    !emit_survivors(index, bundle,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION,
		candidate, index->store_size, &used, &count) ||
	    !emit_mutations(bundle, candidate, index->store_size, &used, &count))
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	if (count > policy->maximum_records)
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	memset(scan_entries, 0, entry_bytes);
	scan = (struct payload_mm_authvar_store_index) {
		.entries = scan_entries,
		.entry_capacity = (uint32_t)scan_entry_capacity,
	};
	if (payload_mm_authvar_store_scan(&scan, candidate, index->store_size,
				  &limits) != CB_SUCCESS ||
	    !candidate_matches(index, &scan, bundle, used, count) ||
	    !final_certdb_matches(&scan, bundle) ||
	    !projection_matches(index, &scan, bundle, binding))
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	hash_status = payload_mm_sha256(index->store, index->store_size,
					draft.source_digest);
	if (hash_status != PAYLOAD_MM_VERIFY_OK)
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	hash_status = payload_mm_sha256(candidate, index->store_size,
					draft.candidate_digest);
	if (hash_status != PAYLOAD_MM_VERIFY_OK)
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	draft.binding = *binding;
	draft.policy = *policy;
	draft.source_used_size = index->used_size;
	draft.candidate_used_size = (uint32_t)used;
	draft.candidate_record_count = count;
	draft.volatile_modes = bundle->volatile_modes;
	*result = draft;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}
