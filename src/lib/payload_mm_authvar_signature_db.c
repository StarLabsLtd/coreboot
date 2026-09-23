/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_format.h>
#include <boot/payload_mm_authvar_signature_db.h>
#include <boot/payload_mm_authvar_store.h>

#include <commonlib/helpers.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include <string.h>

#include "payload_mm_crypto/crypto.h"

#define SIGNATURE_LIST_HEADER_SIZE 28U
#define SIGNATURE_OWNER_SIZE 16U
#define VARIABLE_SIZE UINT32_MAX
#define MAX_FILTER_COMPARISONS 65536U

struct signature_type {
	uint8_t guid[16];
	uint32_t data_size;
	bool x509;
};

/* EDK2 26.09 AuthService.c:mSupportSigItem, in EFI byte order. */
static const struct signature_type signature_types[] = {
	{ { 0x26, 0x16, 0xc4, 0xc1, 0x4c, 0x50, 0x92, 0x40,
	    0xac, 0xa9, 0x41, 0xf9, 0x36, 0x93, 0x43, 0x28 }, 32U, false },
	{ { 0xe8, 0x66, 0x57, 0x3c, 0x9c, 0x26, 0x34, 0x4e,
	    0xaa, 0x14, 0xed, 0x77, 0x6e, 0x85, 0xb3, 0xb6 }, 256U, false },
	{ { 0x90, 0x61, 0xb3, 0xe2, 0x9b, 0x87, 0x3d, 0x4a,
	    0xad, 0x8d, 0xf2, 0xe7, 0xbb, 0xa3, 0x27, 0x84 }, 256U, false },
	{ { 0x12, 0xa5, 0x6c, 0x82, 0x10, 0xcf, 0xc9, 0x4a,
	    0xb1, 0x87, 0xbe, 0x01, 0x49, 0x66, 0x31, 0xbd }, 20U, false },
	{ { 0x4f, 0x44, 0xf8, 0x67, 0x43, 0x87, 0xf1, 0x48,
	    0xa3, 0x28, 0x1e, 0xaa, 0xb8, 0x73, 0x60, 0x80 }, 256U, false },
	{ { 0xa1, 0x59, 0xc0, 0xa5, 0xe4, 0x94, 0xa7, 0x4a,
	    0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72 }, VARIABLE_SIZE, true },
	{ { 0x33, 0x52, 0x6e, 0x0b, 0x5c, 0xa6, 0xc9, 0x44,
	    0x94, 0x07, 0xd9, 0xab, 0x83, 0xbf, 0xc8, 0xbd }, 28U, false },
	{ { 0x07, 0x53, 0x3e, 0xff, 0xd0, 0x9f, 0xc9, 0x48,
	    0x85, 0xf1, 0x8a, 0xd5, 0x6c, 0x70, 0x1e, 0x01 }, 48U, false },
	{ { 0xae, 0x0f, 0x3e, 0x09, 0xc4, 0xa6, 0x50, 0x4f,
	    0x9f, 0x1b, 0xd4, 0x1e, 0x2b, 0x89, 0xc1, 0x9a }, 64U, false },
	{ { 0x92, 0xa4, 0xd2, 0x3b, 0xc0, 0x96, 0x79, 0x40,
	    0xb4, 0x20, 0xfc, 0xf9, 0x8e, 0xf1, 0x03, 0xed }, 48U, false },
	{ { 0x6e, 0x87, 0x76, 0x70, 0xc2, 0x80, 0xe6, 0x4e,
	    0xaa, 0xd2, 0x28, 0xb3, 0x49, 0xa6, 0x86, 0x5b }, 64U, false },
	{ { 0x63, 0xbf, 0x6d, 0x44, 0x02, 0x25, 0xda, 0x4c,
	    0xbc, 0xfa, 0x24, 0x65, 0xd2, 0xb0, 0xfe, 0x9d }, 80U, false },
};

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
	return left_address < right_address + right_size &&
		right_address < left_address + left_size;
}

static const struct signature_type *signature_type_for(const uint8_t guid[16])
{
	for (size_t i = 0U; i < ARRAY_SIZE(signature_types); i++)
		if (!memcmp(guid, signature_types[i].guid, sizeof(signature_types[i].guid)))
			return &signature_types[i];
	return NULL;
}

static enum payload_mm_verify_status x509_valid(
	struct payload_mm_crypto_owner *owner, const uint8_t *der, size_t size)
{
	mbedtls_x509_crt certificate;
	enum payload_mm_verify_status status;
	int result;

	if (!size || size > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE)
		return PAYLOAD_MM_VERIFY_MALFORMED;
	status = payload_mm_crypto_begin(owner);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	mbedtls_x509_crt_init(&certificate);
	result = mbedtls_x509_crt_parse_der_nocopy(&certificate, der, size);
	if (result || certificate.raw.p != der || certificate.raw.len != size ||
	    certificate.next || !mbedtls_pk_can_do(&certificate.pk, MBEDTLS_PK_RSA) ||
	    mbedtls_pk_get_bitlen(&certificate.pk) < PAYLOAD_MM_CRYPTO_MIN_RSA_BITS ||
	    mbedtls_pk_get_bitlen(&certificate.pk) > PAYLOAD_MM_CRYPTO_MAX_RSA_BITS)
		status = PAYLOAD_MM_VERIFY_MALFORMED;
	mbedtls_x509_crt_free(&certificate);
	return payload_mm_crypto_end(owner, status);
}

static enum payload_mm_verify_status validate_stream(
	struct payload_mm_crypto_owner *owner, const void *data, size_t size,
	enum payload_mm_authvar_signature_db_profile profile,
	uint32_t maximum_x509,
	struct payload_mm_authvar_signature_db_stats *stats)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;
	struct payload_mm_authvar_signature_db_stats found = { 0 };
	enum payload_mm_authvar_format_result result;

	if (!payload_mm_authvar_signature_list_begin(&cursor, data, size))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	while ((result = payload_mm_authvar_signature_list_next(&cursor, &list)) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_OK) {
		const struct signature_type *type = signature_type_for(list.type_guid);

		if (!type || list.header.size || !list.signature_count ||
		    (type->data_size == VARIABLE_SIZE ?
			list.signature_size <= SIGNATURE_OWNER_SIZE :
			list.signature_size != SIGNATURE_OWNER_SIZE + type->data_size) ||
		    found.list_count == UINT32_MAX ||
		    list.signature_count > UINT32_MAX - found.signature_count)
			return PAYLOAD_MM_VERIFY_MALFORMED;
		found.list_count++;
		found.signature_count += list.signature_count;
		if (profile == PAYLOAD_MM_AUTHVAR_PLATFORM_KEY &&
		    (!type->x509 || found.list_count != 1U || list.signature_count != 1U))
			return PAYLOAD_MM_VERIFY_MALFORMED;
		if (!type->x509)
			continue;
		if (list.signature_count > UINT32_MAX - found.x509_count)
			return PAYLOAD_MM_VERIFY_MALFORMED;
		found.x509_count += list.signature_count;
		if (found.x509_count > maximum_x509)
			return PAYLOAD_MM_VERIFY_UNSUPPORTED;
	}
	if (result != PAYLOAD_MM_AUTHVAR_FORMAT_DONE || !found.list_count ||
	    (profile == PAYLOAD_MM_AUTHVAR_PLATFORM_KEY &&
	     (found.list_count != 1U || found.signature_count != 1U ||
	      found.x509_count != 1U)))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	payload_mm_authvar_signature_list_begin(&cursor, data, size);
	while (payload_mm_authvar_signature_list_next(&cursor, &list) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_OK) {
		const struct signature_type *type = signature_type_for(list.type_guid);

		if (!type->x509)
			continue;
		for (uint32_t i = 0U; i < list.signature_count; i++) {
			struct payload_mm_authvar_signature_view signature;
			enum payload_mm_verify_status status;

			if (!payload_mm_authvar_signature_at(&list, i, &signature))
				return PAYLOAD_MM_VERIFY_MALFORMED;
			status = x509_valid(owner, signature.data.data, signature.data.size);
			if (status != PAYLOAD_MM_VERIFY_OK)
				return status;
		}
	}
	if (stats)
		*stats = found;
	return PAYLOAD_MM_VERIFY_OK;
}

enum payload_mm_verify_status payload_mm_authvar_signature_db_validate(
	struct payload_mm_crypto_owner *owner, const void *data, size_t size,
	enum payload_mm_authvar_signature_db_profile profile,
	uint32_t maximum_x509,
	struct payload_mm_authvar_signature_db_stats *stats)
{
	if (!owner || (uintptr_t)owner % _Alignof(*owner) || !data || !size ||
	    size > PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_DATA_SIZE ||
	    (profile != PAYLOAD_MM_AUTHVAR_SIGNATURE_DB &&
	     profile != PAYLOAD_MM_AUTHVAR_PLATFORM_KEY) ||
	    !range_valid(owner, sizeof(*owner)) || !range_valid(data, size) ||
	    (stats && ((uintptr_t)stats % _Alignof(*stats) ||
	     !range_valid(stats, sizeof(*stats)))) ||
	    ranges_overlap(owner, sizeof(*owner), data, size) ||
	    (stats && (ranges_overlap(stats, sizeof(*stats), owner, sizeof(*owner)) ||
	     ranges_overlap(stats, sizeof(*stats), data, size))))
		return PAYLOAD_MM_VERIFY_INVALID;
	return validate_stream(owner, data, size, profile, maximum_x509, stats);
}

static bool signature_exists(const void *current, size_t current_size,
	const struct payload_mm_authvar_signature_list_view *append_list,
	const uint8_t *append_signature)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;
	enum payload_mm_authvar_format_result result;

	payload_mm_authvar_signature_list_begin(&cursor, current, current_size);
	while ((result = payload_mm_authvar_signature_list_next(&cursor, &list)) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_OK) {
		if (list.signature_size != append_list->signature_size ||
		    memcmp(list.type_guid, append_list->type_guid, sizeof(list.type_guid)))
			continue;
		for (uint32_t i = 0U; i < list.signature_count; i++)
			if (!memcmp(list.signatures.data + (size_t)i * list.signature_size,
				append_signature, list.signature_size))
				return true;
	}
	return false;
}

static void write_le32(uint8_t *data, uint32_t value)
{
	data[0] = value;
	data[1] = value >> 8;
	data[2] = value >> 16;
	data[3] = value >> 24;
}

static enum payload_mm_verify_status filtered_size(const void *current,
	size_t current_size, const void *append, size_t append_size, size_t *size)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;
	enum payload_mm_authvar_format_result result;
	size_t total = 0U;

	payload_mm_authvar_signature_list_begin(&cursor, append, append_size);
	while ((result = payload_mm_authvar_signature_list_next(&cursor, &list)) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_OK) {
		size_t kept = 0U;

		for (uint32_t i = 0U; i < list.signature_count; i++)
			if (!signature_exists(current, current_size, &list,
				list.signatures.data + (size_t)i * list.signature_size))
				kept++;
		if (kept) {
			size_t list_size = SIGNATURE_LIST_HEADER_SIZE + list.header.size;
			if (kept > (SIZE_MAX - list_size) / list.signature_size)
				return PAYLOAD_MM_VERIFY_MALFORMED;
			list_size += kept * list.signature_size;
			if (list_size > UINT32_MAX || list_size > SIZE_MAX - total)
				return PAYLOAD_MM_VERIFY_MALFORMED;
			total += list_size;
		}
	}
	if (result != PAYLOAD_MM_AUTHVAR_FORMAT_DONE)
		return PAYLOAD_MM_VERIFY_MALFORMED;
	*size = total;
	return PAYLOAD_MM_VERIFY_OK;
}

static void emit_filtered(const void *current, size_t current_size,
	const void *append, size_t append_size, uint8_t *output)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;

	payload_mm_authvar_signature_list_begin(&cursor, append, append_size);
	while (payload_mm_authvar_signature_list_next(&cursor, &list) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_OK) {
		size_t kept = 0U;

		for (uint32_t i = 0U; i < list.signature_count; i++) {
			const uint8_t *signature = list.signatures.data +
				(size_t)i * list.signature_size;

			if (!signature_exists(current, current_size, &list, signature))
				kept++;
		}
		if (!kept)
			continue;
		memcpy(output, list.header.data - SIGNATURE_LIST_HEADER_SIZE,
			SIGNATURE_LIST_HEADER_SIZE + list.header.size);
		write_le32(output + 16U, SIGNATURE_LIST_HEADER_SIZE + list.header.size +
			kept * list.signature_size);
		output += SIGNATURE_LIST_HEADER_SIZE + list.header.size;
		for (uint32_t i = 0U; i < list.signature_count; i++) {
			const uint8_t *signature = list.signatures.data +
				(size_t)i * list.signature_size;

			if (signature_exists(current, current_size, &list, signature))
				continue;
			memcpy(output, signature, list.signature_size);
			output += list.signature_size;
		}
	}
}

enum payload_mm_verify_status payload_mm_authvar_signature_db_filter_append(
	struct payload_mm_crypto_owner *owner,
	const void *current, size_t current_size,
	const void *append, size_t append_size,
	void *output, size_t output_capacity, size_t *output_size)
{
	struct payload_mm_authvar_signature_db_stats current_stats;
	struct payload_mm_authvar_signature_db_stats append_stats;
	enum payload_mm_verify_status status;
	size_t needed;

	if (!owner || (uintptr_t)owner % _Alignof(*owner) || !current ||
	    !current_size || !append || !append_size || !output || !output_size ||
	    (uintptr_t)output_size % _Alignof(*output_size) ||
	    current_size > PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_DATA_SIZE ||
	    append_size > PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_DATA_SIZE ||
	    output_capacity > PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_DATA_SIZE ||
	    !range_valid(owner, sizeof(*owner)) || !range_valid(current, current_size) ||
	    !range_valid(append, append_size) || !range_valid(output, output_capacity) ||
	    !range_valid(output_size, sizeof(*output_size)) ||
	    ranges_overlap(current, current_size, append, append_size) ||
	    ranges_overlap(output, output_capacity, current, current_size) ||
	    ranges_overlap(output, output_capacity, append, append_size) ||
	    ranges_overlap(owner, sizeof(*owner), current, current_size) ||
	    ranges_overlap(owner, sizeof(*owner), append, append_size) ||
	    ranges_overlap(owner, sizeof(*owner), output, output_capacity) ||
	    ranges_overlap(output_size, sizeof(*output_size), owner, sizeof(*owner)) ||
	    ranges_overlap(output_size, sizeof(*output_size), current, current_size) ||
	    ranges_overlap(output_size, sizeof(*output_size), append, append_size) ||
	    ranges_overlap(output_size, sizeof(*output_size), output, output_capacity))
		return PAYLOAD_MM_VERIFY_INVALID;
	status = validate_stream(owner, current, current_size,
		PAYLOAD_MM_AUTHVAR_SIGNATURE_DB,
		PAYLOAD_MM_AUTHVAR_SIGNATURE_DB_MAX_X509, &current_stats);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	status = validate_stream(owner, append, append_size,
		PAYLOAD_MM_AUTHVAR_SIGNATURE_DB,
		PAYLOAD_MM_AUTHVAR_SIGNATURE_DB_MAX_X509, &append_stats);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	if (current_stats.signature_count &&
	    append_stats.signature_count >
		MAX_FILTER_COMPARISONS / current_stats.signature_count)
		return PAYLOAD_MM_VERIFY_UNSUPPORTED;
	status = filtered_size(current, current_size, append, append_size, &needed);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	if (needed > output_capacity)
		return PAYLOAD_MM_VERIFY_NO_MEMORY;
	emit_filtered(current, current_size, append, append_size, output);
	*output_size = needed;
	return PAYLOAD_MM_VERIFY_OK;
}
