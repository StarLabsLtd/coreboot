/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_candidate.h>
#include <boot/payload_mm_authvar_format.h>
#include <boot/payload_mm_authvar_record.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/lib/payload_mm_authvar_internal.h"

#define assert(c) do { if (!(c)) __builtin_abort(); } while (0)
#define STORE_SIZE 1024U
#define MAX_ENTRIES 16U

static u8 store[STORE_SIZE];
static u8 candidate[STORE_SIZE];
static u8 source_copy[STORE_SIZE];
static struct payload_mm_authvar_store_entry entries[MAX_ENTRIES];
static struct payload_mm_authvar_store_entry scratch[MAX_ENTRIES];
static struct payload_mm_authvar_store_index index;
static unsigned int hash_calls;
static unsigned int hash_fail_call;
static const u8 store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
};
static const u8 vknv_guid[16] = {
	0xe0, 0xe4, 0x73, 0x90, 0xec, 0x60, 0x6e, 0x4b,
	0x99, 0x03, 0x4c, 0x22, 0x3c, 0x26, 0x0f, 0x3c,
};
static const u8 global_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static const u8 enable_guid[16] = {
	0xc7, 0x0b, 0xa3, 0xf0, 0x08, 0xaf, 0x56, 0x45,
	0x99, 0xc4, 0x00, 0x10, 0x09, 0xc9, 0x3a, 0x44,
};
static const u8 custom_guid[16] = {
	0x0c, 0xec, 0x76, 0xc0, 0x28, 0x70, 0x99, 0x43,
	0xa0, 0x72, 0x71, 0xee, 0x5c, 0x44, 0x8b, 0x9f,
};
static const u8 certdb_guid[16] = {
	0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
	0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
};
static const u8 vknv_name[] = {
	'V', 0, 'e', 0, 'n', 0, 'd', 0, 'o', 0, 'r', 0, 'K', 0, 'e', 0,
	'y', 0, 's', 0, 'N', 0, 'v', 0, 0, 0,
};
static const u8 target_guid[16] = { 0x42 };
static const u8 target_name[] = { 'F', 0, 'o', 0, 'o', 0, 0, 0 };
static const u8 other_guid[16] = { 0x77 };
static const u8 other_name[] = { 'O', 0, 't', 0, 'h', 0, 'e', 0, 'r', 0, 0, 0 };
static const u8 pk_name[] = { 'P', 0, 'K', 0, 0, 0 };
static const u8 kek_name[] = { 'K', 0, 'E', 0, 'K', 0, 0, 0 };
static const u8 setup_name[] = {
	'S', 0, 'e', 0, 't', 0, 'u', 0, 'p', 0, 'M', 0, 'o', 0, 'd', 0,
	'e', 0, 0, 0,
};
static const u8 enable_name[] = {
	'S', 0, 'e', 0, 'c', 0, 'u', 0, 'r', 0, 'e', 0, 'B', 0, 'o', 0,
	'o', 0, 't', 0, 'E', 0, 'n', 0, 'a', 0, 'b', 0, 'l', 0, 'e', 0,
	0, 0,
};
static const u8 custom_name[] = {
	'C', 0, 'u', 0, 's', 0, 't', 0, 'o', 0, 'm', 0, 'M', 0, 'o', 0,
	'd', 0, 'e', 0, 0, 0,
};
static const u8 certdb_name[] = {
	'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 0, 0,
};
static const u8 timestamp[16] = { 0xe8, 0x07, 1, 2, 3, 4, 5 };
static const struct payload_mm_authvar_store_limits limits = {
	.maximum_store_size = STORE_SIZE,
	.maximum_name_size = 128U,
	.maximum_data_size = 256U,
	.maximum_records = MAX_ENTRIES,
};
static const struct payload_mm_authvar_write_policy policy = {
	.maximum_name_size = 128U,
	.maximum_record_size = 256U,
	.maximum_data_size = 128U,
	.maximum_records = MAX_ENTRIES,
};

static void put16(u8 *bytes, size_t offset, uint16_t value)
{
	bytes[offset] = (uint8_t)value;
	bytes[offset + 1U] = (uint8_t)(value >> 8);
}

static void put32(u8 *bytes, size_t offset, uint32_t value)
{
	for (size_t i = 0U; i < sizeof(value); i++)
		bytes[offset + i] = (uint8_t)(value >> (8U * i));
}

static bool all_zero(const u8 *bytes, size_t size)
{
	u8 combined = 0U;

	while (size--)
		combined |= *bytes++;
	return combined == 0U;
}

enum payload_mm_verify_status payload_mm_sha256(const void *message,
						size_t message_size, uint8_t digest[PAYLOAD_MM_SHA256_SIZE])
{
	const u8 *bytes = message;

	hash_calls++;
	if (hash_calls == hash_fail_call)
		return PAYLOAD_MM_VERIFY_INVALID;
	memset(digest, 0, PAYLOAD_MM_SHA256_SIZE);
	for (size_t i = 0U; i < message_size; i++)
		digest[i % PAYLOAD_MM_SHA256_SIZE] ^= bytes[i];
	return PAYLOAD_MM_VERIFY_OK;
}

static size_t add_record(size_t at,
			 const struct payload_mm_authvar_record_descriptor *descriptor,
	const void *data, size_t data_size,
	enum payload_mm_authvar_record_timestamp mode, uint8_t state)
{
	const struct payload_mm_authvar_record_span span = {
		.data = data,
		.size = data_size,
	};
	u32 size;

	assert(payload_mm_authvar_record_encode(descriptor, &span, 1U, mode,
						store + at, sizeof(store) - at, &size));
	store[at + 2U] = state;
	return at + size;
}

static void init_source(void)
{
	const u8 vknv = 1U;
	struct payload_mm_authvar_record_descriptor descriptor = {
		.name = vknv_name,
		.name_size = sizeof(vknv_name),
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
	};

	memset(store, 0xff, sizeof(store));
	memcpy(store, store_guid, sizeof(store_guid));
	put32(store, 16U, sizeof(store));
	store[20] = 0x5aU;
	store[21] = 0xfeU;
	put16(store, 22U, 0U);
	put32(store, 24U, 0U);
	memcpy(descriptor.vendor_guid, vknv_guid, 16U);
	(void)add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, &descriptor,
		&vknv, sizeof(vknv),
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED);
	memset(entries, 0, sizeof(entries));
	index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = MAX_ENTRIES,
	};
	assert(payload_mm_authvar_store_scan(&index, store, sizeof(store),
					     &limits) == CB_SUCCESS);
}

static void init_private_source(bool target_exists, bool binding_exists)
{
	static const u8 vknv = 1U;
	static const u8 target_value[] = { 1U, 2U };
	static const u8 empty_certdb[] = { 4U, 0U, 0U, 0U };
	static const u8 private_binding[32] = { 0x5a };
	u8 database[128];
	size_t database_size = sizeof(empty_certdb);
	size_t at = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	struct payload_mm_authvar_record_descriptor descriptor = {
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
	};

	memcpy(database, empty_certdb, sizeof(empty_certdb));
	if (binding_exists)
		assert(payload_mm_authvar_certdb_compose(empty_certdb,
			sizeof(empty_certdb), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
			target_guid, target_name, sizeof(target_name) - 2U,
			private_binding, sizeof(private_binding), database,
			sizeof(database), &database_size) ==
			PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	memset(store, 0xff, sizeof(store));
	memcpy(store, store_guid, sizeof(store_guid));
	put32(store, 16U, sizeof(store));
	store[20] = 0x5aU;
	store[21] = 0xfeU;
	put16(store, 22U, 0U);
	put32(store, 24U, 0U);
	descriptor.name = vknv_name;
	descriptor.name_size = sizeof(vknv_name);
	memcpy(descriptor.vendor_guid, vknv_guid, 16U);
	at = add_record(at, &descriptor, &vknv, sizeof(vknv),
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED);
	if (target_exists) {
		descriptor.name = target_name;
		descriptor.name_size = sizeof(target_name);
		descriptor.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
		memcpy(descriptor.vendor_guid, target_guid, 16U);
		memcpy(descriptor.timestamp, timestamp, sizeof(timestamp));
		at = add_record(at, &descriptor, target_value,
			sizeof(target_value),
			PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED,
			PAYLOAD_MM_AUTHVAR_STATE_ADDED);
		descriptor.attributes &= ~PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
		memset(descriptor.timestamp, 0, sizeof(descriptor.timestamp));
	}
	descriptor.name = certdb_name;
	descriptor.name_size = sizeof(certdb_name);
	descriptor.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	memcpy(descriptor.vendor_guid, certdb_guid, 16U);
	(void)add_record(at, &descriptor, database, database_size,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED);
	memset(entries, 0, sizeof(entries));
	index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = MAX_ENTRIES,
	};
	assert(payload_mm_authvar_store_scan(&index, store, sizeof(store),
		&limits) == CB_SUCCESS);
}

static void add_source_certdb_binding(void)
{
	static const u8 binding[1] = { 0x77 };
	const struct payload_mm_authvar_store_entry *entry =
		payload_mm_authvar_store_find(&index, certdb_guid, certdb_name,
			sizeof(certdb_name));
	u8 database[256];
	size_t database_size;
	const void *source;

	assert(entry);
	source = payload_mm_authvar_store_data(&index, entry);
	assert(source && payload_mm_authvar_certdb_compose(source, entry->data_size,
		PAYLOAD_MM_AUTHVAR_CERTDB_ADD, other_guid, other_name,
		sizeof(other_name) - 2U, binding, sizeof(binding), database,
		sizeof(database), &database_size) == PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert(entry->data_offset + database_size <= sizeof(store));
	memcpy(store + entry->data_offset, database, database_size);
	put32(store, entry->record_offset + 40U, (uint32_t)database_size);
	memset(store + entry->data_offset + database_size, 0xff,
		sizeof(store) - entry->data_offset - database_size);
	memset(entries, 0, sizeof(entries));
	index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = MAX_ENTRIES,
	};
	assert(payload_mm_authvar_store_scan(&index, store, sizeof(store),
		&limits) == CB_SUCCESS);
}

static void append_target(u8 state, const uint8_t record_timestamp[16])
{
	static const u8 value[] = { 1U, 2U };
	struct payload_mm_authvar_record_descriptor descriptor = {
		.name = target_name,
		.name_size = sizeof(target_name),
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
	};

	memcpy(descriptor.vendor_guid, target_guid, 16U);
	memcpy(descriptor.timestamp, record_timestamp, 16U);
	(void)add_record(index.used_size, &descriptor, value, sizeof(value),
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED, state);
	memset(entries, 0, sizeof(entries));
	index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = MAX_ENTRIES,
	};
	assert(payload_mm_authvar_store_scan(&index, store, sizeof(store),
					     &limits) == CB_SUCCESS);
}

static void append_projection_record(const u8 guid[16], const u8 *name,
	size_t name_size, u32 attributes, const u8 *data, size_t data_size,
	enum payload_mm_authvar_record_timestamp mode)
{
	struct payload_mm_authvar_record_descriptor descriptor = {
		.name = name,
		.name_size = name_size,
		.attributes = attributes,
	};

	memcpy(descriptor.vendor_guid, guid, 16U);
	if (mode == PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED &&
	    attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED)
		memcpy(descriptor.timestamp, timestamp, sizeof(timestamp));
	(void)add_record(index.used_size, &descriptor, data, data_size, mode,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED);
	memset(entries, 0, sizeof(entries));
	index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = MAX_ENTRIES,
	};
	assert(payload_mm_authvar_store_scan(&index, store, sizeof(store),
		&limits) == CB_SUCCESS);
}

static void test_projection_oracle(void)
{
	static const u8 one = 1U;
	static const u8 mode_values[] = { 0U, 1U, 2U, 0xffU };
	struct payload_mm_authvar_store_entry candidate_entries[MAX_ENTRIES];
	struct payload_mm_authvar_store_entry setup_entries[MAX_ENTRIES];
	struct payload_mm_authvar_store_index built = {
		.entries = candidate_entries,
		.entry_capacity = MAX_ENTRIES,
	};
	struct payload_mm_authvar_store_index setup = {
		.entries = setup_entries,
		.entry_capacity = MAX_ENTRIES,
	};
	struct payload_mm_authvar_candidate_binding binding;
	const struct payload_mm_authvar_store_entry *pk;
	const struct payload_mm_authvar_store_entry *enable;
	const struct payload_mm_authvar_store_entry *custom;
	bool enabled;
	u8 setup_store[STORE_SIZE];

	init_source();
	append_projection_record(global_guid, pk_name, sizeof(pk_name),
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
		&one, sizeof(one), PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED);
	append_projection_record(enable_guid, enable_name, sizeof(enable_name),
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS,
		&one, sizeof(one), PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED);
	append_projection_record(custom_guid, custom_name, sizeof(custom_name),
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS,
		&one, sizeof(one), PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED);
	binding = (struct payload_mm_authvar_candidate_binding) {
		.generation = 1U,
		.token = 2U,
		.source_volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
			PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS,
	};
	enable = payload_mm_authvar_store_find(&index, enable_guid, enable_name,
		sizeof(enable_name));
	custom = payload_mm_authvar_store_find(&index, custom_guid, custom_name,
		sizeof(custom_name));
	assert(enable && custom);
	for (size_t i = 0U; i < ARRAY_SIZE(mode_values); i++) {
		store[enable->data_offset] = mode_values[i];
		store[custom->data_offset] = mode_values[i];
		assert(payload_mm_authvar_mode_enabled(&index,
			PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE,
			PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
				PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS,
			&enabled));
		assert(enabled == (mode_values[i] == 1U));
		assert(payload_mm_authvar_mode_enabled(&index,
			PAYLOAD_MM_AUTHVAR_MODE_KEY_CUSTOM_MODE,
			PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
				PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS,
			&enabled));
		assert(enabled == (mode_values[i] == 1U));
		memcpy(candidate, store, sizeof(candidate));
		memset(candidate_entries, 0, sizeof(candidate_entries));
		built = (struct payload_mm_authvar_store_index) {
			.entries = candidate_entries,
			.entry_capacity = MAX_ENTRIES,
		};
		assert(payload_mm_authvar_store_scan(&built, candidate,
			sizeof(candidate), &limits) == CB_SUCCESS);
		binding.source_volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS |
			(mode_values[i] == 1U ?
			 PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT : 0U);
		assert(payload_mm_authvar_candidate_projection_valid(&index, &built,
			&binding, binding.source_volatile_modes));
	}
	store[enable->data_offset] = one;
	store[custom->data_offset] = one;
	binding.source_volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	memcpy(candidate, store, sizeof(candidate));
	pk = payload_mm_authvar_store_find(&index, global_guid, pk_name,
		sizeof(pk_name));
	assert(pk);
	candidate[pk->record_offset + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED;
	assert(payload_mm_authvar_store_scan(&built, candidate, sizeof(candidate),
		&limits) == CB_SUCCESS);
	binding.at_runtime = 1U;
	assert(payload_mm_authvar_candidate_projection_valid(&index, &built,
		&binding, PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
	binding.at_runtime = 0U;
	assert(!payload_mm_authvar_candidate_projection_valid(&index, &built,
		&binding, PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
	assert(payload_mm_authvar_candidate_projection_valid(&index, &built,
		&binding, PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
	binding.source_volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	assert(payload_mm_authvar_candidate_projection_valid(&built, &built,
		&binding, PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
	binding.source_volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	binding.at_runtime = 1U;
	enable = payload_mm_authvar_store_find(&built, enable_guid, enable_name,
		sizeof(enable_name));
	assert(enable);
	candidate[enable->data_offset] = 0U;
	memset(candidate_entries, 0, sizeof(candidate_entries));
	built = (struct payload_mm_authvar_store_index) {
		.entries = candidate_entries,
		.entry_capacity = MAX_ENTRIES,
	};
	assert(payload_mm_authvar_store_scan(&built, candidate, sizeof(candidate),
		&limits) == CB_SUCCESS);
	assert(!payload_mm_authvar_candidate_projection_valid(&index, &built,
		&binding, PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
	binding.at_runtime = 0U;
	binding.source_volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	assert(payload_mm_authvar_candidate_projection_valid(&built, &built,
		&binding, PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
	memcpy(candidate, store, sizeof(candidate));
	enable = payload_mm_authvar_store_find(&index, enable_guid, enable_name,
		sizeof(enable_name));
	assert(enable);
	candidate[enable->record_offset + 2U] =
		PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED;
	memset(candidate_entries, 0, sizeof(candidate_entries));
	built = (struct payload_mm_authvar_store_index) {
		.entries = candidate_entries,
		.entry_capacity = MAX_ENTRIES,
	};
	assert(payload_mm_authvar_store_scan(&built, candidate, sizeof(candidate),
		&limits) == CB_SUCCESS);
	binding.source_volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	assert(!payload_mm_authvar_candidate_projection_valid(&index, &built,
		&binding, PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
	binding.at_runtime = 1U;
	assert(!payload_mm_authvar_candidate_projection_valid(&index, &built,
		&binding, PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
	assert(!payload_mm_authvar_candidate_projection_valid(&built, &index,
		&binding, PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
	assert(!payload_mm_authvar_candidate_projection_valid(&built, &built,
		&binding, PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
	pk = payload_mm_authvar_store_find(&built, global_guid, pk_name,
		sizeof(pk_name));
	assert(pk);
	memcpy(setup_store, candidate, sizeof(setup_store));
	setup_store[pk->record_offset + 2U] =
		PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED;
	assert(payload_mm_authvar_store_scan(&setup, setup_store,
		sizeof(setup_store),
		&limits) == CB_SUCCESS);
	assert(!payload_mm_authvar_candidate_projection_valid(&built, &setup,
		&binding, PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
	binding.source_volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	assert(!payload_mm_authvar_candidate_projection_valid(&setup, &built,
		&binding, PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
	init_source();
}

static struct payload_mm_authvar_bundle_plan target_write(void)
{
	static const u8 value[] = { 9U, 8U, 7U };
	struct payload_mm_authvar_bundle_plan bundle = {
		.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION,
		.mutation_count = 1U,
		.volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
			PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS,
	};
	struct payload_mm_authvar_bundle_mutation *item = &bundle.mutations[0];

	item->role = PAYLOAD_MM_AUTHVAR_BUNDLE_TARGET;
	item->mutation.kind = PAYLOAD_MM_AUTHVAR_MUTATION_WRITE;
	item->mutation.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	item->mutation.data_size = sizeof(value);
	memcpy(item->mutation.timestamp, timestamp, sizeof(timestamp));
	memcpy(item->vendor_guid, target_guid, sizeof(target_guid));
	item->name = target_name;
	item->name_size = sizeof(target_name);
	item->data = value;
	item->data_size = sizeof(value);
	return bundle;
}

static void add_enable(struct payload_mm_authvar_bundle_plan *bundle)
{
	static const u8 enabled = 1U;
	struct payload_mm_authvar_bundle_mutation *item =
		&bundle->mutations[bundle->mutation_count++];

	item->role = PAYLOAD_MM_AUTHVAR_BUNDLE_SECURE_BOOT_ENABLE;
	item->mutation.kind = PAYLOAD_MM_AUTHVAR_MUTATION_WRITE;
	item->mutation.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	item->mutation.data_size = 1U;
	memcpy(item->vendor_guid, enable_guid, 16U);
	item->name = enable_name;
	item->name_size = sizeof(enable_name);
	item->data = &enabled;
	item->data_size = 1U;
}

static void add_vendor_modified(struct payload_mm_authvar_bundle_plan *bundle)
{
	static const u8 modified;
	struct payload_mm_authvar_bundle_mutation *item =
		&bundle->mutations[bundle->mutation_count++];

	item->role = PAYLOAD_MM_AUTHVAR_BUNDLE_VENDOR_KEYS_NV;
	item->mutation.kind = PAYLOAD_MM_AUTHVAR_MUTATION_WRITE;
	item->mutation.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	item->mutation.data_size = 1U;
	memcpy(item->vendor_guid, vknv_guid, 16U);
	item->name = vknv_name;
	item->name_size = sizeof(vknv_name);
	item->data = &modified;
	item->data_size = 1U;
}

static struct payload_mm_authvar_candidate_binding source_binding(void)
{
	return (struct payload_mm_authvar_candidate_binding) {
		.generation = 1U,
		.token = 2U,
		.source_volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
			PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS,
	};
}

static void expect_safe_failure(const struct payload_mm_authvar_store_index *input,
	const struct payload_mm_authvar_bundle_plan *bundle,
	const struct payload_mm_authvar_write_policy *write_policy,
	const struct payload_mm_authvar_candidate_binding *binding,
	size_t capacity, uint64_t expected);

static struct payload_mm_authvar_bundle_plan private_certdb_plan(
	enum payload_mm_authvar_certdb_operation operation)
{
	static u8 replacement[256];
	static const u8 private_binding[32] = { 0x5a };
	struct payload_mm_authvar_bundle_plan bundle = target_write();
	struct payload_mm_authvar_bundle_mutation *target = &bundle.mutations[0];
	struct payload_mm_authvar_bundle_mutation *certdb = &bundle.mutations[1];
	const struct payload_mm_authvar_store_entry *source_certdb =
		payload_mm_authvar_store_find(&index, certdb_guid, certdb_name,
			sizeof(certdb_name));
	const void *source_data = payload_mm_authvar_store_data(&index,
		source_certdb);
	size_t replacement_size;

	assert(source_certdb && source_data);
	bundle.mutation_count = 2U;
	bundle.certdb_operation = operation;
	if (operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD) {
		bundle.private_binding_size = sizeof(private_binding);
		memcpy(bundle.private_binding, private_binding,
			sizeof(private_binding));
	} else {
		target->mutation = (struct payload_mm_authvar_policy_mutation) {
			.kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE,
		};
		target->data = NULL;
		target->data_size = 0U;
	}
	assert(payload_mm_authvar_certdb_compose(source_data,
		source_certdb->data_size, operation, target_guid, target_name,
		sizeof(target_name) - 2U,
		operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD ? private_binding : NULL,
		operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD ?
			sizeof(private_binding) : 0U,
		replacement, sizeof(replacement), &replacement_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert(replacement_size <= UINT32_MAX);
	*certdb = (struct payload_mm_authvar_bundle_mutation) {
		.role = PAYLOAD_MM_AUTHVAR_BUNDLE_CERTDB,
		.mutation = {
			.kind = PAYLOAD_MM_AUTHVAR_MUTATION_WRITE,
			.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
				PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
				PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
				PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
			.data_size = (uint32_t)replacement_size,
		},
		.name = certdb_name,
		.name_size = sizeof(certdb_name),
		.data = replacement,
		.data_size = replacement_size,
	};
	memcpy(certdb->vendor_guid, certdb_guid, 16U);
	return bundle;
}

static void test_private_certdb_candidate(void)
{
	struct payload_mm_authvar_candidate_binding binding = source_binding();
	struct payload_mm_authvar_candidate_result result;
	struct payload_mm_authvar_store_entry built_entries[MAX_ENTRIES];
	struct payload_mm_authvar_store_index built = {
		.entries = built_entries,
		.entry_capacity = MAX_ENTRIES,
	};
	struct payload_mm_authvar_certdb_binding stored;
	struct payload_mm_authvar_bundle_plan bundle;
	const struct payload_mm_authvar_store_entry *certdb;
	const void *data;

	for (enum payload_mm_authvar_certdb_operation operation =
		PAYLOAD_MM_AUTHVAR_CERTDB_ADD;
	     operation <= PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE; operation++) {
		init_private_source(operation == PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE,
			operation == PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE);
		bundle = private_certdb_plan(operation);
		memset(&result, 0, sizeof(result));
		assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
			&binding, candidate, sizeof(candidate), scratch,
			MAX_ENTRIES, &result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(payload_mm_authvar_store_scan(&built, candidate,
			sizeof(candidate), &limits) == CB_SUCCESS);
		certdb = payload_mm_authvar_store_find(&built, certdb_guid,
			certdb_name, sizeof(certdb_name));
		assert(certdb && certdb->attributes == 0x27U &&
			all_zero(candidate + certdb->record_offset + 16U, 16U));
		data = payload_mm_authvar_store_data(&built, certdb);
		assert(data);
		if (operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD)
			assert(payload_mm_authvar_certdb_find(data, certdb->data_size,
				target_guid, target_name, sizeof(target_name) - 2U,
				&stored) == PAYLOAD_MM_AUTHVAR_CERTDB_OK &&
				stored.size == 32U && stored.data[0] == 0x5aU);
		else
			assert(payload_mm_authvar_certdb_find(data, certdb->data_size,
				target_guid, target_name, sizeof(target_name) - 2U,
				&stored) == PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND);
	}

	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	((u8 *)(uintptr_t)bundle.mutations[1].data)[4] ^= 1U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	((u8 *)(uintptr_t)bundle.mutations[1].data)[4] ^= 1U;
	bundle.certdb_operation = PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	bundle.certdb_operation = PAYLOAD_MM_AUTHVAR_CERTDB_ADD;
	bundle.private_binding[32] = 1U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);

	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	bundle.mutations[1].vendor_guid[0] ^= 1U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	bundle.mutations[1].mutation.attributes ^=
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	bundle.mutations[1].mutation.timestamp[0] = 1U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	{
		struct payload_mm_authvar_bundle_mutation temporary =
			bundle.mutations[0];

		bundle.mutations[0] = bundle.mutations[1];
		bundle.mutations[1] = temporary;
	}
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	bundle.mutations[2] = bundle.mutations[1];
	bundle.mutation_count = 3U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);

	init_private_source(true, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	init_private_source(true, true);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE);
	init_private_source(false, false);
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	init_private_source(true, true);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE);
	{
		struct payload_mm_authvar_store_entry *target =
			(struct payload_mm_authvar_store_entry *)
			payload_mm_authvar_store_find(&index, target_guid,
				target_name, sizeof(target_name));

		assert(target);
		target->attributes &= ~PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
		put32(store, target->record_offset + 4U, target->attributes);
	}
	memset(&result, 0, sizeof(result));
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
		&binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	init_private_source(true, true);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE);
	{
		struct payload_mm_authvar_store_entry *target =
			(struct payload_mm_authvar_store_entry *)
			payload_mm_authvar_store_find(&index, target_guid,
				target_name, sizeof(target_name));

		assert(target);
		target->attributes &= ~PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
		put32(store, target->record_offset + 4U, target->attributes);
	}
	binding.at_runtime = true;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	binding.at_runtime = false;

	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	bundle.mutations[0].mutation.attributes &=
		~PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	memset(&result, 0, sizeof(result));
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
		&binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	binding.at_runtime = true;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	binding.at_runtime = false;

	init_private_source(true, true);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE);
	{
		const struct payload_mm_authvar_store_entry *target =
			payload_mm_authvar_store_find(&index, target_guid,
				target_name, sizeof(target_name));

		assert(target);
		memset(store + target->record_offset + 16U, 0, 16U);
	}
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	init_private_source(true, true);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE);
	{
		const struct payload_mm_authvar_store_entry *target =
			payload_mm_authvar_store_find(&index, target_guid,
				target_name, sizeof(target_name));

		assert(target);
		store[target->record_offset + 18U] = 13U;
	}
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);

	init_private_source(true, true);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE);
	{
		struct payload_mm_authvar_store_entry *target =
			(struct payload_mm_authvar_store_entry *)
			payload_mm_authvar_store_find(&index, target_guid,
				target_name, sizeof(target_name));

		assert(target);
		target->attributes &= ~PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
		put32(store, target->record_offset + 4U, target->attributes);
	}
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);

	/* Plans without a CERTDB role must carry no hidden CERTDB metadata. */
	init_source();
	bundle = target_write();
	bundle.certdb_operation = PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	bundle = target_write();
	bundle.private_binding_size = 32U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	bundle = target_write();
	bundle.private_binding[0] = 1U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);

	/* The source CERTDB metadata is independently part of the proof. */
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	{
		struct payload_mm_authvar_store_entry *source_certdb =
			(struct payload_mm_authvar_store_entry *)
			payload_mm_authvar_store_find(&index, certdb_guid,
				certdb_name, sizeof(certdb_name));

		assert(source_certdb);
		source_certdb->attributes ^= PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
		put32(store, source_certdb->record_offset + 4U,
			source_certdb->attributes);
	}
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	{
		const struct payload_mm_authvar_store_entry *source_certdb =
			payload_mm_authvar_store_find(&index, certdb_guid,
				certdb_name, sizeof(certdb_name));

		assert(source_certdb);
		store[source_certdb->record_offset + 16U] = 1U;
	}
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);

	/* Fixed role order, key and mode separation are strict. */
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	bundle.mutations[1].name = target_name;
	bundle.mutations[1].name_size = sizeof(target_name);
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	bundle.mutations[1].name_size -= 2U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	bundle.mutations[1].role = PAYLOAD_MM_AUTHVAR_BUNDLE_SECURE_BOOT_ENABLE;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);

	/* Exact recomposition preserves unrelated bindings and canonical length. */
	init_private_source(false, false);
	add_source_certdb_binding();
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	{
		struct payload_mm_authvar_certdb_binding unrelated;

		assert(payload_mm_authvar_certdb_find(bundle.mutations[1].data,
			bundle.mutations[1].data_size, other_guid, other_name,
			sizeof(other_name) - 2U, &unrelated) ==
			PAYLOAD_MM_AUTHVAR_CERTDB_OK);
		assert(unrelated.size == 1U);
		((u8 *)(uintptr_t)unrelated.data)[0] ^= 1U;
	}
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	((u8 *)(uintptr_t)bundle.mutations[1].data)[bundle.mutations[1].data_size] = 0U;
	bundle.mutations[1].data_size++;
	bundle.mutations[1].mutation.data_size++;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	put32((u8 *)(uintptr_t)bundle.mutations[1].data, 0U,
		(uint32_t)bundle.mutations[1].data_size - 1U);
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	bundle.private_binding[0] ^= 1U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);

	/* A CERTDB side effect cannot turn a standard or internal key private. */
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	memcpy(bundle.mutations[0].vendor_guid, global_guid, 16U);
	bundle.mutations[0].name = kek_name;
	bundle.mutations[0].name_size = sizeof(kek_name);
	{
		static const u8 empty[] = { 4U, 0U, 0U, 0U };
		static const u8 private_binding[32] = { 0x5a };
		size_t replacement_size;
		u8 *replacement = (u8 *)(uintptr_t)bundle.mutations[1].data;

		assert(payload_mm_authvar_certdb_compose(empty, sizeof(empty),
			PAYLOAD_MM_AUTHVAR_CERTDB_ADD, global_guid, kek_name,
			sizeof(kek_name) - 2U, private_binding,
			sizeof(private_binding), replacement, 256U,
			&replacement_size) == PAYLOAD_MM_AUTHVAR_CERTDB_OK);
		bundle.mutations[1].data_size = replacement_size;
		bundle.mutations[1].mutation.data_size =
			(uint32_t)replacement_size;
	}
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	memcpy(bundle.mutations[0].vendor_guid, certdb_guid, 16U);
	bundle.mutations[0].name = certdb_name;
	bundle.mutations[0].name_size = sizeof(certdb_name);
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);

	/* Exact store exhaustion stays atomic for the two-mutation candidate. */
	init_private_source(false, false);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	memset(&result, 0, sizeof(result));
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
		&binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(result.candidate_used_size > index.used_size);
	init_private_source(false, false);
	put32(store, 16U, result.candidate_used_size - 1U);
	memset(entries, 0, sizeof(entries));
	index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = MAX_ENTRIES,
	};
	assert(payload_mm_authvar_store_scan(&index, store, sizeof(store),
		&limits) == CB_SUCCESS);
	bundle = private_certdb_plan(PAYLOAD_MM_AUTHVAR_CERTDB_ADD);
	expect_safe_failure(&index, &bundle, &policy, &binding,
		sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES);
	init_source();
}

static void expect_safe_failure(const struct payload_mm_authvar_store_index *input,
				const struct payload_mm_authvar_bundle_plan *bundle,
	const struct payload_mm_authvar_write_policy *write_policy,
	const struct payload_mm_authvar_candidate_binding *binding,
	size_t capacity, uint64_t expected)
{
	struct payload_mm_authvar_candidate_result result;

	memcpy(source_copy, store, sizeof(store));
	memset(&result, 0xa5, sizeof(result));
	hash_calls = 0U;
	hash_fail_call = 0U;
	assert(payload_mm_authvar_candidate_build(input, bundle, write_policy,
						  binding, candidate, capacity, scratch, MAX_ENTRIES, &result) ==
		expected);
	assert(all_zero((const uint8_t *)&result, sizeof(result)));
	assert(!memcmp(store, source_copy, sizeof(store)));
}

static void test_build(void)
{
	struct payload_mm_authvar_bundle_plan bundle = target_write();
	const struct payload_mm_authvar_candidate_binding binding = {
		.generation = 7U,
		.token = 11U,
		.source_volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
			PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS,
	};
	struct payload_mm_authvar_candidate_result result;
	struct payload_mm_authvar_store_entry verify_entries[MAX_ENTRIES];
	struct payload_mm_authvar_store_index verify = {
		.entries = verify_entries,
		.entry_capacity = MAX_ENTRIES,
	};

	memcpy(source_copy, store, sizeof(store));
	hash_calls = 0U;
	hash_fail_call = 0U;
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(!memcmp(store, source_copy, sizeof(store)));
	assert(result.binding.generation == 7U && result.binding.token == 11U);
	assert(!memcmp(&result.policy, &policy, sizeof(policy)));
	assert(result.source_used_size == index.used_size);
	assert(result.candidate_used_size < sizeof(candidate));
	assert(result.candidate_record_count == 2U);
	assert(result.volatile_modes == bundle.volatile_modes);
	assert(all_zero(result.reserved, sizeof(result.reserved)));
	{
		u8 digest[PAYLOAD_MM_SHA256_SIZE];

		hash_calls = 0U;
		hash_fail_call = 0U;
		assert(payload_mm_sha256(store, sizeof(store), digest) ==
			PAYLOAD_MM_VERIFY_OK);
		assert(!memcmp(result.source_digest, digest, sizeof(digest)));
		assert(payload_mm_sha256(candidate, sizeof(candidate), digest) ==
			PAYLOAD_MM_VERIFY_OK);
		assert(!memcmp(result.candidate_digest, digest, sizeof(digest)));
	}
	assert(payload_mm_authvar_store_scan(&verify, candidate, sizeof(candidate),
					     &limits) == CB_SUCCESS);
	assert(verify.entry_count == 2U && !verify.dirty_tail_offset);
	assert(payload_mm_authvar_store_find(&verify, target_guid, target_name,
					     sizeof(target_name)) != NULL);
}

static void test_hash_and_capacity_failures(void)
{
	struct payload_mm_authvar_bundle_plan bundle = target_write();
	struct payload_mm_authvar_candidate_binding binding = source_binding();
	struct payload_mm_authvar_candidate_result result;

	for (hash_fail_call = 1U; hash_fail_call <= 2U; hash_fail_call++) {
		hash_calls = 0U;
		memset(&result, 0xa5, sizeof(result));
		assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
							  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
			&result) == PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
		assert(all_zero((const uint8_t *)&result, sizeof(result)));
	}
	hash_calls = 0U;
	hash_fail_call = 0U;
	memset(&result, 0xa5, sizeof(result));
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES - 1U,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(all_zero((const uint8_t *)&result, sizeof(result)));
}

static void test_alias_matrix(void)
{
	struct payload_mm_authvar_bundle_plan bundle = target_write();
	struct payload_mm_authvar_candidate_binding binding = source_binding();
	struct payload_mm_authvar_candidate_result result;
	u64 status;

	memset(&result, 0xa5, sizeof(result));
	status = payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						    &binding, store, sizeof(store), scratch, MAX_ENTRIES, &result);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(all_zero((const uint8_t *)&result, sizeof(result)));
	memset(&result, 0xa5, sizeof(result));
	status = payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						    &binding, candidate, sizeof(candidate), entries, MAX_ENTRIES, &result);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(all_zero((const uint8_t *)&result, sizeof(result)));

	bundle.mutations[0].data = candidate;
	memset(&result, 0xa5, sizeof(result));
	status = payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						    &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES, &result);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(all_zero((const uint8_t *)&result, sizeof(result)));

	bundle = target_write();
	bundle.mutations[0].name = (const void *)(UINTPTR_MAX - 1U);
	bundle.mutations[0].name_size = sizeof(target_name);
	memset(&result, 0xa5, sizeof(result));
	status = payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						    &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES, &result);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	/* An unrepresentable input range cannot be proven disjoint from result. */
	assert(!all_zero((const uint8_t *)&result, sizeof(result)));

	/* Candidate and scratch outputs must not cover any typed input. */
	bundle = target_write();
	memset(&result, 0xa5, sizeof(result));
	status = payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						    &binding, &index, sizeof(candidate), scratch, MAX_ENTRIES, &result);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(all_zero((const uint8_t *)&result, sizeof(result)));
	memset(&result, 0xa5, sizeof(result));
	status = payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						    &binding, candidate, sizeof(candidate),
		(struct payload_mm_authvar_store_entry *)&index, MAX_ENTRIES,
		&result);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(all_zero((const uint8_t *)&result, sizeof(result)));
	memset(&result, 0xa5, sizeof(result));
	status = payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						    &binding, scratch, sizeof(candidate), scratch, MAX_ENTRIES, &result);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(all_zero((const uint8_t *)&result, sizeof(result)));

	/* Partial item/output overlap is rejected before either output is used. */
	bundle = target_write();
	memcpy(candidate + 4U, target_name, sizeof(target_name));
	bundle.mutations[0].name = candidate + 4U;
	memset(&result, 0xa5, sizeof(result));
	status = payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						    &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES, &result);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(all_zero((const uint8_t *)&result, sizeof(result)));
	bundle = target_write();
	bundle.mutations[0].data = candidate + 20U;
	memset(&result, 0xa5, sizeof(result));
	status = payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						    &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES, &result);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(all_zero((const uint8_t *)&result, sizeof(result)));

	/* Unsafe result aliases and invalid top-level ranges remain untouched. */
	{
		struct payload_mm_authvar_candidate_binding saved = binding;

		status = payload_mm_authvar_candidate_build(&index, &bundle, &policy,
							    &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
			(struct payload_mm_authvar_candidate_result *)&binding);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(!memcmp(&binding, &saved, sizeof(binding)));
	}
	memset(&result, 0xa5, sizeof(result));
	status = payload_mm_authvar_candidate_build(
		(const struct payload_mm_authvar_store_index *)(UINTPTR_MAX - 3U),
		&bundle, &policy, &binding, candidate, sizeof(candidate), scratch,
		MAX_ENTRIES, &result);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(!all_zero((const uint8_t *)&result, sizeof(result)));
	memset(&result, 0xa5, sizeof(result));
	status = payload_mm_authvar_candidate_build(
		(const struct payload_mm_authvar_store_index *)((uintptr_t)&index + 1U),
		&bundle, &policy, &binding, candidate, sizeof(candidate), scratch,
		MAX_ENTRIES, &result);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(!all_zero((const uint8_t *)&result, sizeof(result)));
}

static void test_rejections(void)
{
	struct payload_mm_authvar_bundle_plan bundle = target_write();
	struct payload_mm_authvar_candidate_binding binding = {
		.generation = 1U,
		.token = 2U,
		.source_volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
			PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS,
	};
	struct payload_mm_authvar_candidate_result result;
	struct payload_mm_authvar_store_index forged = index;
	struct payload_mm_authvar_write_policy small = policy;

	memset(&result, 0xa5, sizeof(result));
	forged.used_size++;
	assert(payload_mm_authvar_candidate_build(&forged, &bundle, &policy,
						  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	assert(all_zero((const uint8_t *)&result, sizeof(result)));
	binding.token = 0U;
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	binding.token = 2U;
	small.maximum_data_size = 2U;
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &small,
						  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		(struct payload_mm_authvar_candidate_result *)candidate) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
}

static void test_bundle_roles_and_projection(void)
{
	struct payload_mm_authvar_candidate_binding binding = source_binding();
	struct payload_mm_authvar_candidate_result result;
	struct payload_mm_authvar_bundle_plan bundle = target_write();

	/* An unrelated target cannot forge any volatile projection bit. */
	bundle.volatile_modes |= PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;
	expect_safe_failure(&index, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	bundle = target_write();
	add_vendor_modified(&bundle);
	bundle.volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP;
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	binding.at_runtime = 1U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	binding.at_runtime = 0U;

	/* PK enrollment is the sole valid target for the enable role. */
	bundle = target_write();
	memcpy(bundle.mutations[0].vendor_guid, global_guid, 16U);
	bundle.mutations[0].name = pk_name;
	bundle.mutations[0].name_size = sizeof(pk_name);
	bundle.volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	add_enable(&bundle);
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	add_vendor_modified(&bundle);
	bundle.volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);

	/* Roles are fixed-order and unique. */
	{
		struct payload_mm_authvar_bundle_mutation saved = bundle.mutations[1];

		bundle.mutations[1] = bundle.mutations[2];
		bundle.mutations[2] = saved;
		expect_safe_failure(&index, &bundle, &policy, &binding,
				    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		bundle.mutations[2] = bundle.mutations[1];
		expect_safe_failure(&index, &bundle, &policy, &binding,
				    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	}
}

static void test_index_policy_and_binding(void)
{
	struct payload_mm_authvar_bundle_plan bundle = target_write();
	struct payload_mm_authvar_candidate_binding binding = source_binding();
	struct payload_mm_authvar_store_index forged;
	struct payload_mm_authvar_write_policy changed;

	forged = index; forged.used_size++;
	expect_safe_failure(&forged, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	forged = index; forged.record_count++;
	expect_safe_failure(&forged, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	forged = index; forged.store_size--;
	expect_safe_failure(&forged, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	forged = index; forged.dirty_tail_offset = index.used_size;
	expect_safe_failure(&forged, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	forged = index; forged.entry_count = 0U;
	expect_safe_failure(&forged, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	forged = index; forged.entries[0].record_offset += 4U;
	expect_safe_failure(&forged, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	forged.entries[0].record_offset -= 4U;
	forged = index; forged.entries[0].data_size++;
	expect_safe_failure(&forged, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	forged.entries[0].data_size--;
	store[index.entries[0].data_offset] = 0U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	store[index.entries[0].data_offset] = 1U;
	forged = index; forged.maximum_name_size -= 2U;
	expect_safe_failure(&forged, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	forged = index; forged.maximum_records--;
	expect_safe_failure(&forged, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);

	changed = policy; changed.maximum_name_size |= 1U;
	expect_safe_failure(&index, &bundle, &changed, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	changed = policy; changed.maximum_data_size = 0U;
	expect_safe_failure(&index, &bundle, &changed, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	changed = policy; changed.maximum_record_size =
		PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE;
	expect_safe_failure(&index, &bundle, &changed, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);

	binding.generation = 0U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	binding = source_binding(); binding.token = 0U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	binding = source_binding(); binding.source_volatile_modes = 0U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	binding = source_binding(); binding.reserved[0] = 1U;
	expect_safe_failure(&index, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	binding = source_binding();
	expect_safe_failure(&index, &bundle, &policy, &binding,
			    sizeof(candidate) - 1U, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
}

static void test_reserved_and_range_edges(void)
{
	struct payload_mm_authvar_bundle_plan bundle = target_write();
	struct payload_mm_authvar_candidate_binding binding = source_binding();
	size_t first = 0U;
	size_t second = 0U;

	memcpy(bundle.mutations[0].vendor_guid, global_guid, 16U);
	bundle.mutations[0].name = setup_name;
	bundle.mutations[0].name_size = sizeof(setup_name);
	expect_safe_failure(&index, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(!payload_mm_authvar_record_layout(4U, 4U,
						 (size_t *)(UINTPTR_MAX - sizeof(size_t) + 1U), &second));
	assert(!payload_mm_authvar_record_layout(4U, 4U, &first,
						 (size_t *)(UINTPTR_MAX - sizeof(size_t) + 1U)));
}

static void test_replace_delete_transition_and_dirty(void)
{
	static const u8 later_timestamp[16] = { 0xe9, 0x07, 1, 2, 3, 4, 5 };
	struct payload_mm_authvar_candidate_binding binding = source_binding();
	struct payload_mm_authvar_candidate_result result;
	struct payload_mm_authvar_bundle_plan bundle;
	struct payload_mm_authvar_store_entry verify_entries[MAX_ENTRIES];
	struct payload_mm_authvar_store_index verify = {
		.entries = verify_entries,
		.entry_capacity = MAX_ENTRIES,
	};

	init_source();
	append_target(PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION,
		      timestamp);
	bundle = target_write();
	memcpy(source_copy, store, sizeof(store));
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(!memcmp(source_copy, store, sizeof(store)));
	assert(payload_mm_authvar_store_scan(&verify, candidate, sizeof(candidate),
					     &limits) == CB_SUCCESS);
	assert(verify.entry_count == 2U);
	assert(!memcmp(payload_mm_authvar_store_name(&verify, &verify.entries[0]),
		       vknv_name, sizeof(vknv_name)));
	assert(!memcmp(payload_mm_authvar_store_name(&verify, &verify.entries[1]),
		       target_name, sizeof(target_name)));

	bundle.mutations[0].mutation.kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE;
	bundle.mutations[0].mutation.attributes = 0U;
	bundle.mutations[0].mutation.data_size = 0U;
	memset(bundle.mutations[0].mutation.timestamp, 0, 16U);
	bundle.mutations[0].data = NULL;
	bundle.mutations[0].data_size = 0U;
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);

	init_source();
	expect_safe_failure(&index, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	append_target(PAYLOAD_MM_AUTHVAR_STATE_ADDED, later_timestamp);
	bundle = target_write();
	expect_safe_failure(&index, &bundle, &policy, &binding,
			    sizeof(candidate), PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);

	/* An interrupted erased-state tail is discarded by canonical rebuild. */
	init_source();
	store[index.used_size] = 0U;
	memset(entries, 0, sizeof(entries));
	index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = MAX_ENTRIES,
	};
	assert(payload_mm_authvar_store_scan(&index, store, sizeof(store),
					     &limits) == CB_SUCCESS && index.dirty_tail_offset);
	bundle = target_write();
	memcpy(source_copy, store, sizeof(store));
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &policy,
						  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(!memcmp(source_copy, store, sizeof(store)));
	memset(verify_entries, 0, sizeof(verify_entries));
	verify = (struct payload_mm_authvar_store_index) {
		.entries = verify_entries,
		.entry_capacity = MAX_ENTRIES,
	};
	assert(payload_mm_authvar_store_scan(&verify, candidate, sizeof(candidate),
					     &limits) == CB_SUCCESS && !verify.dirty_tail_offset);
	init_source();
}

static void test_real_out_of_resources(void)
{
	struct payload_mm_authvar_bundle_plan bundle = target_write();
	struct payload_mm_authvar_candidate_binding binding = source_binding();
	struct payload_mm_authvar_candidate_result result;
	struct payload_mm_authvar_write_policy tight = policy;
	u32 compact_size;

	init_source();
	compact_size = index.used_size;
	put32(store, 16U, compact_size);
	memset(entries, 0, sizeof(entries));
	index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = MAX_ENTRIES,
	};
	assert(payload_mm_authvar_store_scan(&index, store, sizeof(store),
					     &limits) == CB_SUCCESS);
	tight.maximum_record_size = 100U;
	tight.maximum_data_size = 32U;
	memset(&result, 0xa5, sizeof(result));
	assert(payload_mm_authvar_candidate_build(&index, &bundle, &tight,
						  &binding, candidate, sizeof(candidate), scratch, MAX_ENTRIES,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES);
	assert(all_zero((const uint8_t *)&result, sizeof(result)));
	init_source();
}

int main(void)
{
	init_source();
	test_projection_oracle();
	test_private_certdb_candidate();
	test_build();
	test_rejections();
	test_bundle_roles_and_projection();
	test_index_policy_and_binding();
	test_reserved_and_range_edges();
	test_replace_delete_transition_and_dirty();
	test_hash_and_capacity_failures();
	test_alias_matrix();
	test_real_out_of_resources();
	return 0;
}
