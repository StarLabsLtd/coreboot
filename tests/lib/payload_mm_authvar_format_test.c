/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_format.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define assert(c) do { if (!(c)) abort(); } while (0)

#define AUTH2_FIXED_SIZE 40U
#define CERTIFICATE_SIZE 24U
#define LIST_HEADER_SIZE 28U

static const uint8_t pkcs7_guid[16] = {
	0x9d, 0xd2, 0xaf, 0x4a, 0xdf, 0x68, 0xee, 0x49,
	0x8a, 0xa9, 0x34, 0x7d, 0x37, 0x56, 0x65, 0xa7,
};

static void put16(uint8_t *bytes, size_t offset, uint16_t value)
{
	bytes[offset] = (uint8_t)value;
	bytes[offset + 1U] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, size_t offset, uint32_t value)
{
	for (size_t i = 0; i < 4U; i++)
		bytes[offset + i] = (uint8_t)(value >> (8U * i));
}

static bool bytes_are(const void *data, size_t size, uint8_t value)
{
	const uint8_t *bytes = data;

	for (size_t i = 0; i < size; i++) {
		if (bytes[i] != value)
			return false;
	}
	return true;
}

static void valid_time(uint8_t time[16])
{
	memset(time, 0, 16U);
	put16(time, 0, 2024U);
	time[2] = 2U;
	time[3] = 29U;
	time[4] = 23U;
	time[5] = 59U;
	time[6] = 59U;
}

static size_t make_auth2(uint8_t *data, size_t pkcs7_size, size_t payload_size)
{
	const size_t size = AUTH2_FIXED_SIZE + pkcs7_size + payload_size;

	memset(data, 0, size);
	valid_time(data);
	put32(data, 16U, (uint32_t)(CERTIFICATE_SIZE + pkcs7_size));
	put16(data, 20U, 0x0200U);
	put16(data, 22U, 0x0ef1U);
	memcpy(data + 24U, pkcs7_guid, sizeof(pkcs7_guid));
	for (size_t i = 0; i < pkcs7_size; i++)
		data[AUTH2_FIXED_SIZE + i] = (uint8_t)(0x40U + i);
	for (size_t i = 0; i < payload_size; i++)
		data[AUTH2_FIXED_SIZE + pkcs7_size + i] = (uint8_t)(0x90U + i);
	return size;
}

static void expect_auth2_failure(const void *data, size_t size)
{
	struct payload_mm_authvar_auth2_view view;

	memset(&view, 0xa5, sizeof(view));
	assert(payload_mm_authvar_auth2_parse(data, size, &view) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED);
	assert(bytes_are(&view, sizeof(view), 0));
}

static void auth2_boundaries_and_unaligned(void)
{
	uint8_t storage[16U + 96U];

	for (size_t shift = 0; shift < 16U; shift++) {
		uint8_t *data = storage + shift;
		struct payload_mm_authvar_auth2_view view;
		const size_t size = make_auth2(data, 7U, 9U);

		memset(&view, 0xa5, sizeof(view));
		assert(payload_mm_authvar_auth2_parse(data, size, &view) ==
			PAYLOAD_MM_AUTHVAR_FORMAT_OK);
		assert(!memcmp(view.timestamp, data, sizeof(view.timestamp)));
		assert(view.certificate_revision == 0x0200U);
		assert(view.certificate_type == 0x0ef1U);
		assert(!memcmp(view.certificate_guid, pkcs7_guid,
			sizeof(pkcs7_guid)));
		assert(view.pkcs7.data == data + AUTH2_FIXED_SIZE);
		assert(view.pkcs7.size == 7U);
		assert(view.payload.data == data + AUTH2_FIXED_SIZE + 7U);
		assert(view.payload.size == 9U);
	}

	make_auth2(storage, 0, 0);
	for (size_t size = 0; size < AUTH2_FIXED_SIZE; size++)
		expect_auth2_failure(storage, size);
	expect_auth2_failure(NULL, 0);
	assert(payload_mm_authvar_auth2_parse(storage, AUTH2_FIXED_SIZE, NULL) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED);

	for (uint32_t length = 0; length < CERTIFICATE_SIZE; length++) {
		make_auth2(storage, 0, 0);
		put32(storage, 16U, length);
		expect_auth2_failure(storage, AUTH2_FIXED_SIZE);
	}
	make_auth2(storage, 0, 0);
	put32(storage, 16U, CERTIFICATE_SIZE + 1U);
	expect_auth2_failure(storage, AUTH2_FIXED_SIZE);
	make_auth2(storage, 0, 0);
	put32(storage, 16U, UINT32_MAX);
	expect_auth2_failure(storage, AUTH2_FIXED_SIZE);

	/* The certificate revision is decoded but intentionally unrestricted. */
	for (size_t i = 0; i < 3U; i++) {
		static const uint16_t revisions[] = { 0, 0x0100U, UINT16_MAX };
		struct payload_mm_authvar_auth2_view view;

		make_auth2(storage, 0, 0);
		put16(storage, 20U, revisions[i]);
		assert(payload_mm_authvar_auth2_parse(storage, AUTH2_FIXED_SIZE,
			&view) == PAYLOAD_MM_AUTHVAR_FORMAT_OK);
		assert(view.certificate_revision == revisions[i]);
		assert(view.pkcs7.size == 0 && view.payload.size == 0);
	}
}

static void expect_metadata_invalid(const struct payload_mm_authvar_auth2_view *base,
	size_t offset, uint8_t value)
{
	struct payload_mm_authvar_auth2_view view = *base;

	((uint8_t *)&view)[offset] = value;
	assert(!payload_mm_authvar_auth2_metadata_valid(&view));
}

static void metadata_validation(void)
{
	uint8_t data[AUTH2_FIXED_SIZE];
	struct payload_mm_authvar_auth2_view view;
	struct payload_mm_authvar_auth2_view changed;

	make_auth2(data, 0, 0);
	assert(payload_mm_authvar_auth2_parse(data, sizeof(data), &view) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_OK);
	assert(payload_mm_authvar_auth2_metadata_valid(&view));
	assert(payload_mm_authvar_timestamp_store_valid(view.timestamp));
	assert(!payload_mm_authvar_auth2_metadata_valid(NULL));
	assert(!payload_mm_authvar_timestamp_store_valid(NULL));

	changed = view;
	changed.certificate_type = 0x0ef0U;
	assert(!payload_mm_authvar_auth2_metadata_valid(&changed));
	for (size_t i = 0; i < sizeof(view.certificate_guid); i++)
		expect_metadata_invalid(&view,
			offsetof(struct payload_mm_authvar_auth2_view, certificate_guid) + i,
			(uint8_t)(view.certificate_guid[i] ^ 0xffU));

	/* All-zero is the scanner-compatible EDK2 initialization sentinel. */
	changed = view;
	memset(changed.timestamp, 0, sizeof(changed.timestamp));
	assert(payload_mm_authvar_auth2_metadata_valid(&changed));
	assert(payload_mm_authvar_timestamp_store_valid(changed.timestamp));

	/* Every reserved EFI_TIME byte is independently enforced. */
	static const uint8_t reserved[] = { 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	for (size_t i = 0; i < sizeof(reserved); i++) {
		changed = view;
		changed.timestamp[reserved[i]] = 1U;
		assert(!payload_mm_authvar_auth2_metadata_valid(&changed));
		assert(!payload_mm_authvar_timestamp_store_valid(changed.timestamp));
	}

	changed = view;
	put16(changed.timestamp, 0, 1899U);
	assert(payload_mm_authvar_auth2_metadata_valid(&changed));
	assert(!payload_mm_authvar_timestamp_store_valid(changed.timestamp));
	put16(changed.timestamp, 0, 10000U);
	assert(payload_mm_authvar_auth2_metadata_valid(&changed));
	assert(!payload_mm_authvar_timestamp_store_valid(changed.timestamp));
	for (size_t field = 2U; field <= 6U; field++) {
		static const uint8_t invalid[] = { 0, 0, 0, 24, 60, 60 };
		changed = view;
		changed.timestamp[field] = invalid[field - 1U];
		assert(payload_mm_authvar_auth2_metadata_valid(&changed));
		assert(!payload_mm_authvar_timestamp_store_valid(changed.timestamp));
	}
	changed = view;
	changed.timestamp[2] = 13U;
	assert(payload_mm_authvar_auth2_metadata_valid(&changed));
	assert(!payload_mm_authvar_timestamp_store_valid(changed.timestamp));
	changed = view;
	changed.timestamp[2] = 4U;
	changed.timestamp[3] = 30U;
	assert(payload_mm_authvar_auth2_metadata_valid(&changed));
	assert(payload_mm_authvar_timestamp_store_valid(changed.timestamp));
	changed.timestamp[3] = 31U;
	assert(payload_mm_authvar_auth2_metadata_valid(&changed));
	assert(!payload_mm_authvar_timestamp_store_valid(changed.timestamp));
	changed = view;
	put16(changed.timestamp, 0, 2100U);
	changed.timestamp[3] = 29U;
	assert(payload_mm_authvar_auth2_metadata_valid(&changed));
	assert(!payload_mm_authvar_timestamp_store_valid(changed.timestamp));
	put16(changed.timestamp, 0, 2000U);
	assert(payload_mm_authvar_auth2_metadata_valid(&changed));
	assert(payload_mm_authvar_timestamp_store_valid(changed.timestamp));
	put16(changed.timestamp, 0, 2023U);
	assert(payload_mm_authvar_auth2_metadata_valid(&changed));
	assert(!payload_mm_authvar_timestamp_store_valid(changed.timestamp));

	/* Revision and the spans are not signed-metadata format decisions. */
	changed = view;
	changed.certificate_revision = UINT16_MAX;
	changed.pkcs7 = (struct payload_mm_authvar_span){ NULL, SIZE_MAX };
	changed.payload = (struct payload_mm_authvar_span){ NULL, SIZE_MAX };
	assert(payload_mm_authvar_auth2_metadata_valid(&changed));
}

static size_t make_list(uint8_t *data, uint8_t tag, uint32_t header_size,
	uint32_t signature_size, uint32_t count)
{
	const size_t list_size = LIST_HEADER_SIZE + header_size +
		(size_t)signature_size * count;

	memset(data, 0, list_size);
	for (size_t i = 0; i < 16U; i++)
		data[i] = (uint8_t)(tag + i);
	put32(data, 16U, (uint32_t)list_size);
	put32(data, 20U, header_size);
	put32(data, 24U, signature_size);
	for (uint32_t i = 0; i < header_size; i++)
		data[LIST_HEADER_SIZE + i] = (uint8_t)(0x70U + i);
	for (uint32_t entry = 0; entry < count; entry++) {
		uint8_t *signature = data + LIST_HEADER_SIZE + header_size +
			(size_t)entry * signature_size;

		for (size_t i = 0; i < 16U; i++)
			signature[i] = (uint8_t)(tag + entry + i);
		for (uint32_t i = 16U; i < signature_size; i++)
			signature[i] = (uint8_t)(0xa0U + entry + i);
	}
	return list_size;
}

static void assert_list_zero(const struct payload_mm_authvar_signature_list_view *view)
{
	assert(bytes_are(view, sizeof(*view), 0));
}

static void signature_lists_and_unaligned(void)
{
	uint8_t storage[16U + 256U];

	for (size_t shift = 0; shift < 16U; shift++) {
		uint8_t *data = storage + shift;
		const size_t size = make_list(data, 0x10U, 3U, 20U, 2U);
		struct payload_mm_authvar_signature_list_cursor cursor;
		struct payload_mm_authvar_signature_list_view list;
		struct payload_mm_authvar_signature_view signature;

		assert(payload_mm_authvar_signature_list_begin(&cursor, data, size));
		memset(&list, 0xa5, sizeof(list));
		assert(payload_mm_authvar_signature_list_next(&cursor, &list) ==
			PAYLOAD_MM_AUTHVAR_FORMAT_OK);
		assert(!memcmp(list.type_guid, data, 16U));
		assert(list.header.data == data + LIST_HEADER_SIZE);
		assert(list.header.size == 3U);
		assert(list.signatures.data == data + LIST_HEADER_SIZE + 3U);
		assert(list.signatures.size == 40U);
		assert(list.signature_size == 20U);
		assert(list.signature_count == 2U);
		assert(cursor.remaining == 0);
		assert(cursor.next == data + size);

		for (uint32_t i = 0; i < 2U; i++) {
			memset(&signature, 0xa5, sizeof(signature));
			assert(payload_mm_authvar_signature_at(&list, i, &signature));
			assert(!memcmp(signature.owner_guid,
				list.signatures.data + (size_t)i * 20U, 16U));
			assert(signature.data.data == list.signatures.data +
				(size_t)i * 20U + 16U);
			assert(signature.data.size == 4U);
		}
		memset(&signature, 0xa5, sizeof(signature));
		assert(!payload_mm_authvar_signature_at(&list, 2U, &signature));
		assert(bytes_are(&signature, sizeof(signature), 0));

		memset(&list, 0xa5, sizeof(list));
		assert(payload_mm_authvar_signature_list_next(&cursor, &list) ==
			PAYLOAD_MM_AUTHVAR_FORMAT_DONE);
		assert_list_zero(&list);
	}
}

static void signature_multiple_and_empty(void)
{
	uint8_t data[256];
	size_t first = make_list(data, 0x10U, 0, 16U, 0);
	size_t second = make_list(data + first, 0x30U, 0, 17U, 1U);
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;
	struct payload_mm_authvar_signature_view signature;

	assert(payload_mm_authvar_signature_list_begin(&cursor, data, first + second));
	assert(payload_mm_authvar_signature_list_next(&cursor, &list) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_OK);
	assert(list.signature_count == 0 && list.signatures.size == 0);
	memset(&signature, 0xa5, sizeof(signature));
	assert(!payload_mm_authvar_signature_at(&list, 0, &signature));
	assert(bytes_are(&signature, sizeof(signature), 0));
	assert(payload_mm_authvar_signature_list_next(&cursor, &list) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_OK);
	assert(list.type_guid[0] == 0x30U && list.signature_count == 1U);
	assert(payload_mm_authvar_signature_list_next(&cursor, &list) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_DONE);

	assert(payload_mm_authvar_signature_list_begin(&cursor, NULL, 0));
	assert(payload_mm_authvar_signature_list_next(&cursor, &list) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_DONE);
	assert(!payload_mm_authvar_signature_list_begin(NULL, data, first));
	memset(&cursor, 0xa5, sizeof(cursor));
	assert(!payload_mm_authvar_signature_list_begin(&cursor, NULL, 1U));
	assert(bytes_are(&cursor, sizeof(cursor), 0));
	assert(payload_mm_authvar_signature_list_next(NULL, &list) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED);
	assert(payload_mm_authvar_signature_list_begin(&cursor, data, first));
	struct payload_mm_authvar_signature_list_cursor before = cursor;
	assert(payload_mm_authvar_signature_list_next(&cursor, NULL) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED);
	assert(cursor.next == before.next && cursor.remaining == before.remaining);
	assert(!payload_mm_authvar_signature_at(&list, 0, NULL));
	memset(&signature, 0xa5, sizeof(signature));
	assert(!payload_mm_authvar_signature_at(NULL, 0, &signature));
	assert(bytes_are(&signature, sizeof(signature), 0));
}

static void expect_list_malformed(uint8_t *data, size_t size)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_cursor before;
	struct payload_mm_authvar_signature_list_view list;

	assert(payload_mm_authvar_signature_list_begin(&cursor, data, size));
	before = cursor;
	memset(&list, 0xa5, sizeof(list));
	assert(payload_mm_authvar_signature_list_next(&cursor, &list) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED);
	assert_list_zero(&list);
	assert(cursor.next == before.next && cursor.remaining == before.remaining);
}

static void signature_malformed_boundaries(void)
{
	uint8_t data[256];

	memset(data, 0, sizeof(data));
	for (size_t size = 1U; size < LIST_HEADER_SIZE; size++) {
		uint8_t *short_list = malloc(size);

		assert(short_list != NULL);
		memset(short_list, 0, size);
		expect_list_malformed(short_list, size);
		free(short_list);
	}
	for (uint32_t list_size = 0; list_size < LIST_HEADER_SIZE; list_size++) {
		make_list(data, 1U, 0, 16U, 0);
		put32(data, 16U, list_size);
		expect_list_malformed(data, LIST_HEADER_SIZE);
	}
	make_list(data, 1U, 0, 16U, 0);
	put32(data, 16U, LIST_HEADER_SIZE + 16U);
	expect_list_malformed(data, LIST_HEADER_SIZE);
	make_list(data, 1U, 0, 16U, 0);
	put32(data, 20U, UINT32_MAX);
	expect_list_malformed(data, LIST_HEADER_SIZE);
	make_list(data, 1U, 0, 16U, 0);
	put32(data, 20U, 16U);
	expect_list_malformed(data, LIST_HEADER_SIZE);

	for (uint32_t signature_size = 0; signature_size < 16U; signature_size++) {
		make_list(data, 1U, 0, 16U, 0);
		put32(data, 24U, signature_size);
		expect_list_malformed(data, LIST_HEADER_SIZE);
	}

	make_list(data, 1U, 0, 17U, 1U);
	put32(data, 16U, LIST_HEADER_SIZE + 16U);
	expect_list_malformed(data, LIST_HEADER_SIZE + 16U);

	/* A valid first list never hides a malformed trailing list. */
	size_t size = make_list(data, 1U, 0, 16U, 1U);
	data[size] = 0;
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_cursor before;
	struct payload_mm_authvar_signature_list_view list;
	assert(payload_mm_authvar_signature_list_begin(&cursor, data, size + 1U));
	assert(payload_mm_authvar_signature_list_next(&cursor, &list) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_OK);
	before = cursor;
	memset(&list, 0xa5, sizeof(list));
	assert(payload_mm_authvar_signature_list_next(&cursor, &list) ==
		PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED);
	assert_list_zero(&list);
	assert(cursor.next == before.next && cursor.remaining == before.remaining);
}

static void malformed_signature_views(void)
{
	uint8_t data[64] = { 0 };
	struct payload_mm_authvar_signature_list_view list = {
		.signatures = { data, 32U },
		.signature_size = 16U,
		.signature_count = 2U,
	};
	struct payload_mm_authvar_signature_view signature;

	assert(payload_mm_authvar_signature_at(&list, 1U, &signature));
	list.signature_size = 15U;
	memset(&signature, 0xa5, sizeof(signature));
	assert(!payload_mm_authvar_signature_at(&list, 0, &signature));
	assert(bytes_are(&signature, sizeof(signature), 0));
	list.signature_size = 16U;
	list.signatures.size = 31U;
	assert(!payload_mm_authvar_signature_at(&list, 1U, &signature));
	list.signatures.size = 32U;
	list.signatures.data = NULL;
	assert(!payload_mm_authvar_signature_at(&list, 0, &signature));
}

int main(void)
{
	auth2_boundaries_and_unaligned();
	metadata_validation();
	signature_lists_and_unaligned();
	signature_multiple_and_empty();
	signature_malformed_boundaries();
	malformed_signature_views();
	return 0;
}
