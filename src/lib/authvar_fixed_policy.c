/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/authvar_fixed_policy.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"

/*
 * Adapted from CDK2's DAVE fixed policy, based on Star Labs EDK2
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
static const uint8_t x509_guid[16] = {
	0xa1, 0x59, 0xc0, 0xa5, 0xe4, 0x94, 0xa7, 0x4a,
	0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72,
};

static bool error(uint64_t status)
{
	return (status & PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT) != 0;
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
			return AUTHVAR_FIXED_REVOCATIONS;
	}
	if ((!memcmp(guid, enable_guid, sizeof(enable_guid)) &&
	     name_equal(name, name_size, "SecureBootEnable")) ||
	    (!memcmp(guid, custom_guid, sizeof(custom_guid)) &&
	     name_equal(name, name_size, "CustomMode")))
		return AUTHVAR_FIXED_LOCKED;
	return AUTHVAR_FIXED_ORDINARY;
}

static uint64_t exact_certificates(const void *data, size_t size,
	const struct authvar_fixed_certificate *certificates, size_t count)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;
	size_t expected_size = 0;

	if (!certificates || !count)
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	for (size_t i = 0; i < count; i++) {
		if (!certificates[i].data.data || !certificates[i].data.size ||
		    !certificates[i].owner_guid || expected_size > SIZE_MAX - 44U ||
		    certificates[i].data.size > SIZE_MAX - 44U - expected_size)
			return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
		expected_size += 44U + certificates[i].data.size;
	}
	if (size != expected_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	if (!payload_mm_authvar_signature_list_begin(&cursor, data, size))
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	/* DAVE serializes exactly one zero-header ESL per certificate, in order. */
	for (size_t i = 0; i < count; i++) {
		struct payload_mm_authvar_signature_view signature;

		if (payload_mm_authvar_signature_list_next(&cursor, &list) !=
		    PAYLOAD_MM_AUTHVAR_FORMAT_OK)
			return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
		if (list.header.size || list.signature_count != 1 ||
		    memcmp(list.type_guid, x509_guid, sizeof(x509_guid)) ||
		    !payload_mm_authvar_signature_at(&list, 0, &signature) ||
		    signature.data.size != certificates[i].data.size ||
		    memcmp(signature.owner_guid, certificates[i].owner_guid, 16) ||
		    memcmp(signature.data.data, certificates[i].data.data, signature.data.size))
			return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	}
	return cursor.remaining ? PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION :
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static bool database_valid(struct payload_mm_authvar_span database)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;
	enum payload_mm_authvar_format_result result;

	if (!payload_mm_authvar_signature_list_begin(&cursor, database.data, database.size))
		return false;
	while ((result = payload_mm_authvar_signature_list_next(&cursor, &list)) ==
	       PAYLOAD_MM_AUTHVAR_FORMAT_OK)
		;
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
			    !memcmp(signature.owner_guid, required->owner_guid, 16) &&
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

static uint64_t validate_key(const struct payload_mm_authvar_store_index *index,
	const uint8_t guid[16], const uint16_t *name, size_t name_size,
	const struct authvar_fixed_certificate *certificates, size_t count)
{
	const struct payload_mm_authvar_store_entry *entry =
		payload_mm_authvar_store_find(index, guid, name, name_size);
	const void *data;

	if (!entry || entry->attributes != (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
	    PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS | PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED))
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	data = payload_mm_authvar_store_data(index, entry);
	if (!data)
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	return exact_certificates(data, entry->data_size, certificates, count);
}

static uint64_t validate_policy(const struct payload_mm_authvar_store_index *index,
	const struct authvar_fixed_policy *policy, bool require_append)
{
	static const uint16_t pk[] = { 'P', 'K', 0 };
	static const uint16_t kek[] = { 'K', 'E', 'K', 0 };
	static const uint16_t db[] = { 'd', 'b', 0 };
	static const uint16_t dbx[] = { 'd', 'b', 'x', 0 };
	struct payload_mm_authvar_auth2_view baseline;
	const struct payload_mm_authvar_store_entry *entry;
	struct payload_mm_authvar_span actual;
	uint64_t status;

	if (!index || !policy)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (!payload_mm_authvar_store_index_valid(index))
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	status = validate_key(index, global_guid, pk, sizeof(pk), policy->pk, policy->pk_count);
	if (error(status))
		return status;
	status = validate_key(index, global_guid, kek, sizeof(kek), policy->kek, policy->kek_count);
	if (error(status))
		return status;
	status = validate_key(index, image_security_guid, db, sizeof(db), policy->db, policy->db_count);
	if (error(status))
		return status;
	entry = payload_mm_authvar_store_find(index, image_security_guid, dbx, sizeof(dbx));
	if (!entry || entry->attributes != (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
	    PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS | PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
	    PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED) || !entry->data_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	actual = (struct payload_mm_authvar_span) {
		payload_mm_authvar_store_data(index, entry), entry->data_size,
	};
	if (!actual.data || payload_mm_authvar_auth2_parse(policy->dbx_baseline.data,
	    policy->dbx_baseline.size, &baseline) != PAYLOAD_MM_AUTHVAR_FORMAT_OK ||
	    !payload_mm_authvar_auth2_metadata_valid(&baseline) || !baseline.payload.size ||
	    !policy->dbx_append.size)
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	status = require_revocations(baseline.payload, actual);
	if (error(status) || !require_append)
		return status;
	return require_revocations(policy->dbx_append, actual);
}

uint64_t authvar_fixed_validate(const struct payload_mm_authvar_store_index *index,
	const struct authvar_fixed_policy *policy)
{
	return validate_policy(index, policy, true);
}

uint64_t authvar_fixed_prepare_dbx_update(const struct payload_mm_authvar_store_index *index,
	const struct authvar_fixed_policy *policy, const uint8_t **data, size_t *size)
{
	struct payload_mm_authvar_auth2_view update;
	uint8_t enabled, custom;
	uint64_t status;

	if (!data || !size)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	*data = NULL;
	*size = 0;
	status = validate_policy(index, policy, true);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION)
		return status;
	/* Missing revocations never authorize replacing keys or entering CustomMode. */
	status = validate_policy(index, policy, false);
	if (error(status))
		return status;
	if (!payload_mm_authvar_mode_value(index, PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE,
	    0x03U, &enabled) || enabled != 1U ||
	    !payload_mm_authvar_mode_value(index, PAYLOAD_MM_AUTHVAR_MODE_KEY_CUSTOM_MODE,
	    0x03U, &custom) || custom != 0U)
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	if (payload_mm_authvar_auth2_parse(policy->dbx_update.data, policy->dbx_update.size,
	    &update) != PAYLOAD_MM_AUTHVAR_FORMAT_OK ||
	    !payload_mm_authvar_auth2_metadata_valid(&update) || !update.payload.size)
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	*data = policy->dbx_update.data;
	*size = policy->dbx_update.size;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}
