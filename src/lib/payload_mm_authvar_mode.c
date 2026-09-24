/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_bundle.h>
#include <boot/payload_mm_authvar_store.h>
#include <commonlib/helpers.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"

#define RECORD_TIMESTAMP_OFFSET 16U
#define RECORD_TIMESTAMP_SIZE 16U

static const uint8_t global_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static const uint8_t vendor_keys_nv_guid[16] = {
	0xe0, 0xe4, 0x73, 0x90, 0xec, 0x60, 0x6e, 0x4b,
	0x99, 0x03, 0x4c, 0x22, 0x3c, 0x26, 0x0f, 0x3c,
};
static const uint8_t secure_boot_enable_guid[16] = {
	0xc7, 0x0b, 0xa3, 0xf0, 0x08, 0xaf, 0x56, 0x45,
	0x99, 0xc4, 0x00, 0x10, 0x09, 0xc9, 0x3a, 0x44,
};
static const uint8_t custom_mode_guid[16] = {
	0x0c, 0xec, 0x76, 0xc0, 0x28, 0x70, 0x99, 0x43,
	0xa0, 0x72, 0x71, 0xee, 0x5c, 0x44, 0x8b, 0x9f,
};
static const uint8_t pk_name[] = { 'P', 0, 'K', 0, 0, 0 };
static const uint8_t secure_boot_enable_name[] = {
	'S', 0, 'e', 0, 'c', 0, 'u', 0, 'r', 0, 'e', 0, 'B', 0, 'o', 0,
	'o', 0, 't', 0, 'E', 0, 'n', 0, 'a', 0, 'b', 0, 'l', 0, 'e', 0,
	0, 0,
};
static const uint8_t vendor_keys_nv_name[] = {
	'V', 0, 'e', 0, 'n', 0, 'd', 0, 'o', 0, 'r', 0, 'K', 0, 'e', 0,
	'y', 0, 's', 0, 'N', 0, 'v', 0, 0, 0,
};
static const uint8_t custom_mode_name[] = {
	'C', 0, 'u', 0, 's', 0, 't', 0, 'o', 0, 'm', 0, 'M', 0, 'o', 0,
	'd', 0, 'e', 0, 0, 0,
};

struct mode_key_descriptor {
	const uint8_t *vendor_guid;
	const uint8_t *name;
	size_t name_size;
};

static const struct mode_key_descriptor mode_keys[] = {
	[PAYLOAD_MM_AUTHVAR_MODE_KEY_PK] = {
		global_guid, pk_name, sizeof(pk_name),
	},
	[PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE] = {
		secure_boot_enable_guid, secure_boot_enable_name,
		sizeof(secure_boot_enable_name),
	},
	[PAYLOAD_MM_AUTHVAR_MODE_KEY_VENDOR_KEYS_NV] = {
		vendor_keys_nv_guid, vendor_keys_nv_name,
		sizeof(vendor_keys_nv_name),
	},
	[PAYLOAD_MM_AUTHVAR_MODE_KEY_CUSTOM_MODE] = {
		custom_mode_guid, custom_mode_name, sizeof(custom_mode_name),
	},
};

static const struct mode_key_descriptor *mode_key(
	enum payload_mm_authvar_mode_key key)
{
	if ((unsigned int)key >= ARRAY_SIZE(mode_keys))
		return NULL;
	return &mode_keys[key];
}

const struct payload_mm_authvar_store_entry *payload_mm_authvar_mode_find(
	const struct payload_mm_authvar_store_index *index,
	enum payload_mm_authvar_mode_key key)
{
	const struct mode_key_descriptor *descriptor = mode_key(key);

	if (!descriptor)
		return NULL;
	return payload_mm_authvar_store_find(index, descriptor->vendor_guid,
		descriptor->name, descriptor->name_size);
}

bool payload_mm_authvar_mode_key_matches(const uint8_t vendor_guid[16],
	const void *name, size_t name_size, enum payload_mm_authvar_mode_key key)
{
	const struct mode_key_descriptor *descriptor = mode_key(key);

	return descriptor && vendor_guid && name &&
		name_size == descriptor->name_size &&
		!memcmp(vendor_guid, descriptor->vendor_guid, 16U) &&
		!memcmp(name, descriptor->name, name_size);
}

bool payload_mm_authvar_mode_mutation_key(
	struct payload_mm_authvar_bundle_mutation *mutation,
	enum payload_mm_authvar_mode_key key)
{
	const struct mode_key_descriptor *descriptor = mode_key(key);

	if (!mutation || !descriptor)
		return false;
	memcpy(mutation->vendor_guid, descriptor->vendor_guid,
		sizeof(mutation->vendor_guid));
	mutation->name = descriptor->name;
	mutation->name_size = descriptor->name_size;
	return true;
}

bool payload_mm_authvar_mode_value(
	const struct payload_mm_authvar_store_index *index,
	enum payload_mm_authvar_mode_key key, uint32_t attributes, uint8_t *value)
{
	const struct payload_mm_authvar_store_entry *entry =
		payload_mm_authvar_mode_find(index, key);
	const uint8_t *data = payload_mm_authvar_store_data(index, entry);

	if (!entry || !data || !value || entry->attributes != attributes ||
	    entry->data_size != 1U || *data > 1U ||
	    memcmp(index->store + entry->record_offset + RECORD_TIMESTAMP_OFFSET,
		(uint8_t[RECORD_TIMESTAMP_SIZE]) { 0 }, RECORD_TIMESTAMP_SIZE))
		return false;
	*value = *data;
	return true;
}
