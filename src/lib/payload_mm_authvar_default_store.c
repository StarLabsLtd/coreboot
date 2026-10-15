/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_default_store.h>
#include <boot/payload_mm_authvar_record.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>
#include <commonlib/payload_mm_authvar_fv.h>
#include <commonlib/helpers.h>
#include <stdint.h>
#include <string.h>

#define DEFAULT_STORE_FIRST_RECORD \
	(PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE)
#define DEFAULT_STORE_RECORD_BYTES 260U

static const uint8_t custom_mode_guid[16] = {
	0x0c, 0xec, 0x76, 0xc0, 0x28, 0x70, 0x99, 0x43,
	0xa0, 0x72, 0x71, 0xee, 0x5c, 0x44, 0x8b, 0x9f,
};
static const uint8_t certdb_guid[16] = {
	0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
	0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
};
static const uint8_t vendor_keys_nv_guid[16] = {
	0xe0, 0xe4, 0x73, 0x90, 0xec, 0x60, 0x6e, 0x4b,
	0x99, 0x03, 0x4c, 0x22, 0x3c, 0x26, 0x0f, 0x3c,
};
static const uint8_t custom_mode_name[] = {
	'C', 0, 'u', 0, 's', 0, 't', 0, 'o', 0, 'm', 0, 'M', 0, 'o', 0,
	'd', 0, 'e', 0, 0, 0,
};
static const uint8_t certdb_name[] = {
	'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 0, 0,
};
static const uint8_t vendor_keys_nv_name[] = {
	'V', 0, 'e', 0, 'n', 0, 'd', 0, 'o', 0, 'r', 0, 'K', 0, 'e', 0,
	'y', 0, 's', 0, 'N', 0, 'v', 0, 0, 0,
};
static const uint8_t standard_mode = 0U;
static const uint8_t empty_certdb[] = { 4, 0, 0, 0 };
static const uint8_t vendor_keys_valid = 1U;

struct default_record {
	const uint8_t *vendor_guid;
	const uint8_t *name;
	size_t name_size;
	uint32_t attributes;
	const void *data;
	size_t data_size;
	enum payload_mm_authvar_record_timestamp timestamp_mode;
};

static const struct default_record defaults[] = {
	{
		custom_mode_guid, custom_mode_name, sizeof(custom_mode_name),
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS,
		&standard_mode, sizeof(standard_mode),
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED,
	},
	{
		certdb_guid, certdb_name, sizeof(certdb_name),
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
		empty_certdb, sizeof(empty_certdb),
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO,
	},
	{
		vendor_keys_nv_guid, vendor_keys_nv_name,
		sizeof(vendor_keys_nv_name),
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
		&vendor_keys_valid, sizeof(vendor_keys_valid),
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO,
	},
};

static bool span_valid(const void *data, size_t size)
{
	return data && (uintptr_t)data <= UINTPTR_MAX - size;
}

enum payload_mm_authvar_default_store_source
payload_mm_authvar_default_store_compose(const void *source, void *candidate,
					 size_t region_size, size_t block_size)
{
	struct payload_mm_authvar_fv_geometry geometry;
	const uint8_t *source_bytes = source;
	uint8_t *candidate_bytes = candidate;
	uint8_t records[DEFAULT_STORE_RECORD_BYTES];
	const uintptr_t source_address = (uintptr_t)source;
	const uintptr_t candidate_address = (uintptr_t)candidate;
	size_t candidate_used = DEFAULT_STORE_FIRST_RECORD;
	size_t records_used = 0U;
	bool erased = true;
	bool complete = true;
	bool subset = true;

	if (!span_valid(source, region_size) || !span_valid(candidate, region_size))
		return PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID;
	if ((source_address <= candidate_address ?
	     candidate_address - source_address :
	     source_address - candidate_address) < region_size)
		return PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID;
	if (!payload_mm_authvar_fv_geometry(&geometry, region_size, block_size))
		return PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID;
	for (size_t i = 0; i < ARRAY_SIZE(defaults); i++) {
		size_t record_size;
		size_t data_offset;

		if (!payload_mm_authvar_record_layout(defaults[i].name_size,
			defaults[i].data_size, &record_size, &data_offset) ||
		    __builtin_add_overflow(candidate_used, record_size,
			&candidate_used))
			return PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID;
	}
	if (candidate_used > geometry.variable_size)
		return PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID;

	for (size_t i = 0; i < ARRAY_SIZE(defaults); i++) {
		struct payload_mm_authvar_record_descriptor descriptor = {
			.name = defaults[i].name,
			.name_size = defaults[i].name_size,
			.attributes = defaults[i].attributes,
		};
		const struct payload_mm_authvar_record_span span = {
			.data = defaults[i].data,
			.size = defaults[i].data_size,
		};
		uint32_t record_size;

		memcpy(descriptor.vendor_guid, defaults[i].vendor_guid,
			sizeof(descriptor.vendor_guid));
		if (!payload_mm_authvar_record_encode(&descriptor, &span, 1U,
			defaults[i].timestamp_mode, records + records_used,
			sizeof(records) - records_used, &record_size))
			return PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID;
		records[records_used + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
		records_used += record_size;
	}
	if (records_used != sizeof(records) ||
	    !payload_mm_authvar_fv_format(candidate, region_size, block_size))
		return PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID;
	memcpy(candidate_bytes + DEFAULT_STORE_FIRST_RECORD, records,
		sizeof(records));

	for (size_t i = 0; i < region_size; i++) {
		erased &= source_bytes[i] == 0xffU;
		complete &= source_bytes[i] == candidate_bytes[i];
		subset &= (source_bytes[i] & candidate_bytes[i]) == candidate_bytes[i];
	}
	if (erased)
		return PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED;
	if (complete)
		return PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_COMPLETE;
	if (subset)
		return PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_NOR_SUBSET;
	return PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_FOREIGN;
}
