/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/authvar_fixed_policy.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_record.h>
#include <boot/payload_mm_authvar_store.h>
#include <boot/payload_mm_authvar_view.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"

/*
 * Adapted from edIIe's DAVE fixed policy, based on Star Labs EDK2
 * EnrollDefaultKeys/StaticSecureBootPolicyLib at
 * 0f425ebcc9e84351e479f07ef7d7f2714f8a1cf0 (BSD-2-Clause-Patent).
 */

static const uint8_t global_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static const uint8_t image_security_guid[16] = {
	0xcb, 0xb2, 0x19, 0xd7, 0x3a, 0x3d, 0x96, 0x45,
	0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f,
};
static const uint8_t enable_guid[16] = {
	0xc7, 0x0b, 0xa3, 0xf0, 0x08, 0xaf, 0x56, 0x45,
	0x99, 0xc4, 0x00, 0x10, 0x09, 0xc9, 0x3a, 0x44,
};
static const uint8_t custom_guid[16] = {
	0x0c, 0xec, 0x76, 0xc0, 0x28, 0x70, 0x99, 0x43,
	0xa0, 0x72, 0x71, 0xee, 0x5c, 0x44, 0x8b, 0x9f,
};
static const uint8_t vendor_keys_nv_guid[16] = {
	0xe0, 0xe4, 0x73, 0x90, 0xec, 0x60, 0x6e, 0x4b,
	0x99, 0x03, 0x4c, 0x22, 0x3c, 0x26, 0x0f, 0x3c,
};
static const uint8_t x509_guid[16] = {
	0xa1, 0x59, 0xc0, 0xa5, 0xe4, 0x94, 0xa7, 0x4a,
	0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72,
};

size_t authvar_fixed_decode_certificates(struct payload_mm_authvar_span asset,
	struct authvar_fixed_certificate *certificates, size_t capacity)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;
	enum payload_mm_authvar_format_result result;
	size_t count = 0;

	if (!certificates || !capacity ||
	    !payload_mm_authvar_signature_list_begin(&cursor, asset.data, asset.size))
		return 0;
	while ((result = payload_mm_authvar_signature_list_next(&cursor, &list)) ==
	       PAYLOAD_MM_AUTHVAR_FORMAT_OK) {
		struct payload_mm_authvar_signature_view signature;

		if (count == capacity || list.header.size || list.signature_count != 1 ||
		    memcmp(list.type_guid, x509_guid, sizeof(x509_guid)) ||
		    !payload_mm_authvar_signature_at(&list, 0, &signature) || !signature.data.size)
			return 0;
		certificates[count++] = (struct authvar_fixed_certificate) {
			.data = signature.data, .owner_guid = list.signatures.data,
		};
	}
	return result == PAYLOAD_MM_AUTHVAR_FORMAT_DONE ? count : 0;
}


static bool name_equal(const void *name, size_t name_size, const char *expected)
{
	const uint8_t *bytes = name;
	size_t length = strlen(expected);

	if (name_size != (length + 1U) * 2U)
		return false;
	for (size_t i = 0; i < length; i++)
		if (bytes[i * 2U] != (uint8_t)expected[i] || bytes[i * 2U + 1U] != 0)
			return false;
	return bytes[length * 2U] == 0 && bytes[length * 2U + 1U] == 0;
}

enum authvar_fixed_key authvar_fixed_classify(const uint8_t guid[16],
	const void *name, size_t name_size)
{
	if (!memcmp(guid, global_guid, sizeof(global_guid)) &&
	    (name_equal(name, name_size, "PK") || name_equal(name, name_size, "KEK")))
		return AUTHVAR_FIXED_LOCKED;
	if (!memcmp(guid, image_security_guid, sizeof(image_security_guid))) {
		if (name_equal(name, name_size, "db"))
			return AUTHVAR_FIXED_LOCKED;
		if (name_equal(name, name_size, "dbx"))
			return AUTHVAR_FIXED_LOCKED;
		if (name_equal(name, name_size, "dbt"))
			return AUTHVAR_FIXED_LOCKED;
	}
	if ((!memcmp(guid, enable_guid, sizeof(enable_guid)) &&
	     name_equal(name, name_size, "SecureBootEnable")) ||
	    (!memcmp(guid, custom_guid, sizeof(custom_guid)) &&
	     name_equal(name, name_size, "CustomMode")) ||
	    (!memcmp(guid, vendor_keys_nv_guid, sizeof(vendor_keys_nv_guid)) &&
	     name_equal(name, name_size, "VendorKeysNv")))
		return AUTHVAR_FIXED_LOCKED;
	return AUTHVAR_FIXED_ORDINARY;
}

struct payload_mm_authvar_span authvar_fixed_database(const struct authvar_fixed_policy *policy,
	const uint8_t guid[16], const void *name, size_t name_size)
{
	if (!memcmp(guid, global_guid, sizeof(global_guid))) {
		if (name_equal(name, name_size, "PK"))
			return policy->factory_pk;
		if (name_equal(name, name_size, "KEK"))
			return policy->factory_kek;
	}
	if (!memcmp(guid, image_security_guid, sizeof(image_security_guid))) {
		if (name_equal(name, name_size, "db"))
			return policy->factory_db;
		if (name_equal(name, name_size, "dbx"))
			return policy->factory_dbx;
	}
	return (struct payload_mm_authvar_span) { 0 };
}

uint64_t authvar_fixed_next(const uint8_t guid[16], const void *name, size_t name_size,
	bool runtime, struct payload_mm_authvar_view_value *value)
{
	static const struct {
		const uint8_t *guid;
		const uint16_t *name;
		size_t size;
	} keys[] = {
		{ global_guid, u"PK", sizeof(u"PK") },
		{ global_guid, u"KEK", sizeof(u"KEK") },
		{ image_security_guid, u"db", sizeof(u"db") },
		{ enable_guid, u"SecureBootEnable", sizeof(u"SecureBootEnable") },
		{ custom_guid, u"CustomMode", sizeof(u"CustomMode") },
		{ vendor_keys_nv_guid, u"VendorKeysNv", sizeof(u"VendorKeysNv") },
		{ image_security_guid, u"dbx", sizeof(u"dbx") },
	};
	size_t i = runtime ? ARRAY_SIZE(keys) - 1 : 0;

	if (name) {
		for (; i < ARRAY_SIZE(keys); i++)
			if (name_size == keys[i].size && !memcmp(name, keys[i].name, name_size) &&
			    !memcmp(guid, keys[i].guid, 16))
				break;
		if (i == ARRAY_SIZE(keys))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		i++;
	}
	if (i >= ARRAY_SIZE(keys))
		return PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
	*value = (struct payload_mm_authvar_view_value) {
		.vendor_guid = keys[i].guid,
		.name = keys[i].name,
		.name_size = keys[i].size,
	};
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}


static bool database_valid(struct payload_mm_authvar_span database)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;
	enum payload_mm_authvar_format_result result;

	if (!payload_mm_authvar_signature_list_begin(&cursor, database.data, database.size))
		return false;
	while ((result = payload_mm_authvar_signature_list_next(&cursor, &list)) ==
	       PAYLOAD_MM_AUTHVAR_FORMAT_OK) {
		if (list.signature_size <= 16U)
			return false;
	}
	return result == PAYLOAD_MM_AUTHVAR_FORMAT_DONE;
}

static bool contains_signature(struct payload_mm_authvar_span database,
	const struct payload_mm_authvar_signature_list_view *required_list,
	const struct payload_mm_authvar_signature_view *required)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;

	if (!payload_mm_authvar_signature_list_begin(&cursor, database.data, database.size))
		return false;
	while (payload_mm_authvar_signature_list_next(&cursor, &list) ==
	       PAYLOAD_MM_AUTHVAR_FORMAT_OK) {
		if (list.header.size != required_list->header.size ||
		    memcmp(list.type_guid, required_list->type_guid, 16) ||
		    memcmp(list.header.data, required_list->header.data, list.header.size))
			continue;
		for (uint32_t i = 0; i < list.signature_count; i++) {
			struct payload_mm_authvar_signature_view signature;

			if (payload_mm_authvar_signature_at(&list, i, &signature) &&
			    signature.data.size == required->data.size &&
			    !memcmp(signature.data.data, required->data.data, signature.data.size))
				return true;
		}
	}
	return false;
}

static uint64_t require_revocations(struct payload_mm_authvar_span required,
	struct payload_mm_authvar_span actual)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;

	/* No callbacks or mutations intervene: validate each immutable database once. */
	if (!database_valid(required) || !database_valid(actual) ||
	    !payload_mm_authvar_signature_list_begin(&cursor, required.data, required.size))
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	while (payload_mm_authvar_signature_list_next(&cursor, &list) ==
	       PAYLOAD_MM_AUTHVAR_FORMAT_OK) {
		for (uint32_t i = 0; i < list.signature_count; i++) {
			struct payload_mm_authvar_signature_view signature;

			if (!payload_mm_authvar_signature_at(&list, i, &signature) ||
			    !contains_signature(actual, &list, &signature))
				return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		}
	}
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint64_t authvar_fixed_validate(const struct payload_mm_authvar_store_index *index,
	const struct authvar_fixed_policy *policy)
{
	static const uint16_t dbx[] = { 'd', 'b', 'x', 0 };
	if (!policy->factory_pk.data || !policy->factory_pk.size ||
	    !policy->factory_kek.data || !policy->factory_kek.size ||
	    !policy->factory_db.data || !policy->factory_db.size ||
	    !policy->factory_dbx.data || !policy->factory_dbx.size)
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	if (!database_valid(policy->factory_dbx))
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	if (!index)
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	const struct payload_mm_authvar_store_entry *entry =
		payload_mm_authvar_store_find(index, image_security_guid, dbx, sizeof(dbx));
	if (!entry)
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;

	/* Switching to fixed policy must not drop any already-installed revocation. */
	return require_revocations((struct payload_mm_authvar_span) {
		payload_mm_authvar_store_data(index, entry), entry->data_size,
	}, policy->factory_dbx);
}
