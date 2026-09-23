/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_format.h>
#include <string.h>

#define AUTH2_TIMESTAMP_SIZE 16U
#define AUTH2_CERTIFICATE_OFFSET 16U
#define AUTH2_CERTIFICATE_HEADER_SIZE 24U
#define AUTH2_PKCS7_OFFSET 40U
#define SIGNATURE_LIST_HEADER_SIZE 28U
#define SIGNATURE_OWNER_SIZE 16U
#define WIN_CERT_TYPE_EFI_GUID 0x0ef1U

static const uint8_t pkcs7_guid[16] = {
	0x9d, 0xd2, 0xaf, 0x4a, 0xdf, 0x68, 0xee, 0x49,
	0x8a, 0xa9, 0x34, 0x7d, 0x37, 0x56, 0x65, 0xa7,
};

static uint16_t read_le16(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool bytes_are_zero(const uint8_t *data, size_t size)
{
	for (size_t i = 0; i < size; i++)
		if (data[i])
			return false;
	return true;
}

static bool timestamp_valid(const uint8_t timestamp[AUTH2_TIMESTAMP_SIZE])
{
	static const uint8_t days_per_month[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
	};
	uint16_t year;
	uint8_t maximum_day;

	if (bytes_are_zero(timestamp, AUTH2_TIMESTAMP_SIZE))
		return true;
	if (timestamp[7] || read_le32(timestamp + 8) ||
	    read_le16(timestamp + 12) || timestamp[14] || timestamp[15])
		return false;
	year = read_le16(timestamp);
	if (year < 1900 || year > 9999 || timestamp[2] < 1 || timestamp[2] > 12)
		return false;
	maximum_day = days_per_month[timestamp[2] - 1U];
	if (timestamp[2] == 2 && (!(year % 4) && ((year % 100) || !(year % 400))))
		maximum_day++;
	if (timestamp[3] < 1 || timestamp[3] > maximum_day || timestamp[4] > 23 ||
	    timestamp[5] > 59 || timestamp[6] > 59)
		return false;
	return true;
}

static bool timestamp_reserved_fields_valid(
	const uint8_t timestamp[AUTH2_TIMESTAMP_SIZE])
{
	return !timestamp[7] && !read_le32(timestamp + 8) &&
		!read_le16(timestamp + 12) && !timestamp[14] && !timestamp[15];
}

enum payload_mm_authvar_format_result payload_mm_authvar_auth2_parse(
	const void *data, size_t size, struct payload_mm_authvar_auth2_view *view)
{
	const uint8_t *bytes = data;
	uint32_t certificate_size;
	size_t payload_offset;

	if (!view)
		return PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED;
	memset(view, 0, sizeof(*view));
	if (!bytes || size < AUTH2_PKCS7_OFFSET)
		return PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED;
	certificate_size = read_le32(bytes + AUTH2_CERTIFICATE_OFFSET);
	if (certificate_size < AUTH2_CERTIFICATE_HEADER_SIZE ||
	    certificate_size > size - AUTH2_CERTIFICATE_OFFSET)
		return PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED;
	payload_offset = AUTH2_CERTIFICATE_OFFSET + certificate_size;
	memcpy(view->timestamp, bytes, sizeof(view->timestamp));
	view->certificate_revision = read_le16(bytes + 20U);
	view->certificate_type = read_le16(bytes + 22U);
	memcpy(view->certificate_guid, bytes + 24U, sizeof(view->certificate_guid));
	view->pkcs7.data = bytes + AUTH2_PKCS7_OFFSET;
	view->pkcs7.size = certificate_size - AUTH2_CERTIFICATE_HEADER_SIZE;
	view->payload.data = bytes + payload_offset;
	view->payload.size = size - payload_offset;
	return PAYLOAD_MM_AUTHVAR_FORMAT_OK;
}

bool payload_mm_authvar_auth2_metadata_valid(
	const struct payload_mm_authvar_auth2_view *view)
{
	return view && timestamp_reserved_fields_valid(view->timestamp) &&
		view->certificate_type == WIN_CERT_TYPE_EFI_GUID &&
		!memcmp(view->certificate_guid, pkcs7_guid, sizeof(pkcs7_guid));
}

bool payload_mm_authvar_timestamp_store_valid(const uint8_t timestamp[16])
{
	return timestamp && timestamp_valid(timestamp);
}

bool payload_mm_authvar_signature_list_begin(
	struct payload_mm_authvar_signature_list_cursor *cursor,
	const void *data, size_t size)
{
	if (!cursor)
		return false;
	memset(cursor, 0, sizeof(*cursor));
	if (size && !data)
		return false;
	cursor->next = data;
	cursor->remaining = size;
	return true;
}

enum payload_mm_authvar_format_result payload_mm_authvar_signature_list_next(
	struct payload_mm_authvar_signature_list_cursor *cursor,
	struct payload_mm_authvar_signature_list_view *view)
{
	const uint8_t *list;
	uint32_t list_size;
	uint32_t header_size;
	uint32_t signature_size;
	size_t payload_size;

	if (!view)
		return PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED;
	memset(view, 0, sizeof(*view));
	if (!cursor || (cursor->remaining && !cursor->next))
		return PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED;
	if (!cursor->remaining)
		return PAYLOAD_MM_AUTHVAR_FORMAT_DONE;
	if (cursor->remaining < SIGNATURE_LIST_HEADER_SIZE)
		return PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED;
	list = cursor->next;
	list_size = read_le32(list + 16U);
	header_size = read_le32(list + 20U);
	signature_size = read_le32(list + 24U);
	if (list_size < SIGNATURE_LIST_HEADER_SIZE || list_size > cursor->remaining ||
	    header_size > list_size - SIGNATURE_LIST_HEADER_SIZE ||
	    signature_size < SIGNATURE_OWNER_SIZE)
		return PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED;
	payload_size = list_size - SIGNATURE_LIST_HEADER_SIZE - header_size;
	if (payload_size % signature_size)
		return PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED;
	memcpy(view->type_guid, list, sizeof(view->type_guid));
	view->header.data = list + SIGNATURE_LIST_HEADER_SIZE;
	view->header.size = header_size;
	view->signatures.data = view->header.data + header_size;
	view->signatures.size = payload_size;
	view->signature_size = signature_size;
	view->signature_count = (uint32_t)(payload_size / signature_size);
	cursor->next += list_size;
	cursor->remaining -= list_size;
	return PAYLOAD_MM_AUTHVAR_FORMAT_OK;
}

bool payload_mm_authvar_signature_at(
	const struct payload_mm_authvar_signature_list_view *list, uint32_t index,
	struct payload_mm_authvar_signature_view *signature)
{
	size_t offset;

	if (!signature)
		return false;
	memset(signature, 0, sizeof(*signature));
	if (!list || index >= list->signature_count ||
	    list->signature_size < SIGNATURE_OWNER_SIZE ||
	    (list->signatures.size && !list->signatures.data) ||
	    index > SIZE_MAX / list->signature_size)
		return false;
	offset = (size_t)index * list->signature_size;
	if (offset > list->signatures.size ||
	    list->signature_size > list->signatures.size - offset)
		return false;
	memcpy(signature->owner_guid, list->signatures.data + offset,
		sizeof(signature->owner_guid));
	signature->data.data = list->signatures.data + offset + SIGNATURE_OWNER_SIZE;
	signature->data.size = list->signature_size - SIGNATURE_OWNER_SIZE;
	return true;
}
