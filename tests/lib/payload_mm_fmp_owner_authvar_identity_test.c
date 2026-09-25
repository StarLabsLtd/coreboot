/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_record.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>
#include <boot/payload_mm_authvar_store_semantics.h>
#include <string.h>

extern int dprintf(int fd, const char *format, ...);

#include "payload_mm_authvar_set_preflight.h"
#include "payload_mm_fmp_owner_authvar_internal.h"

#define STORE_SIZE 2048U
#define ENTRY_COUNT 8U

static const char *const key_names[] = {
	"FmpState", "FmpVersion", "FmpLsv", "LastAttemptStatus",
	"LastAttemptVersion",
};
static const uint8_t namespace_guid[16] = {
	0x37, 0x48, 0x58, 0x85, 0x03, 0x7b, 0x6c, 0x4b,
	0x94, 0x0c, 0x8b, 0x61, 0x86, 0xcb, 0xf7, 0xa1,
};
static uint8_t store[STORE_SIZE] __aligned(__BIGGEST_ALIGNMENT__);
static struct payload_mm_authvar_store_entry entries[ENTRY_COUNT];
static struct payload_mm_authvar_store_index index;
static bool authority_closed;
static bool protect_authority = true;
static const void *protected_storage_address;
static size_t protected_storage_size;
static const char *identity_mutation;
static uint64_t hardware_instance = 0x1234567812345678ULL;

bool payload_mm_authvar_buffers_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_address = (uintptr_t)left;
	uintptr_t right_address = (uintptr_t)right;

	return left_address < right_address + right_size &&
		right_address < left_address + left_size;
}

void mock_assert(const int result, const char *expression, const char *file,
	const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result) {
		dprintf(2, "%s:%d: assertion failed: %s\n", file, line, expression);
		__builtin_trap();
	}
}

enum cb_err payload_mm_fmp_state_identity_get_for_key(uint32_t key,
	struct payload_mm_fmp_state_identity *identity)
{
	static const char hex[] = "0123456789ABCDEF";
	const uint64_t instance = hardware_instance;
	size_t length;

	if (authority_closed || key >= ARRAY_SIZE(key_names) || !identity)
		return CB_ERR;
	memset(identity, 0, sizeof(*identity));
	memcpy(identity->namespace_guid.b, namespace_guid, sizeof(namespace_guid));
	length = strlen(key_names[key]);
	for (size_t i = 0; i < length; i++)
		identity->variable_name[i] = (uint16_t)key_names[key][i];
	for (size_t i = 0; instance && i < 16; i++)
		identity->variable_name[length + i] = (uint8_t)
			hex[(instance >> ((15U - i) * 4U)) & 0xfU];
	identity->variable_name_bytes =
		(uint32_t)((length + (instance ? 16U : 0U) + 1U) * sizeof(uint16_t));
	identity->hardware_instance = instance;
	identity->trusted_lowest_version = 7;
	if (identity_mutation) {
		if (!strcmp(identity_mutation, "zero-guid"))
			memset(identity->namespace_guid.b, 0,
				sizeof(identity->namespace_guid.b));
		else if (!strcmp(identity_mutation, "wrong-guid") && key == 1)
			identity->namespace_guid.b[0] ^= 1U;
		else if (!strcmp(identity_mutation, "embedded-nul"))
			identity->variable_name[1] = 0;
		else if (!strcmp(identity_mutation, "odd-size"))
			identity->variable_name_bytes--;
		else if (!strcmp(identity_mutation, "bad-suffix"))
			identity->variable_name[length] = 'a';
		else if (!strcmp(identity_mutation, "trailing"))
			identity->variable_name[length + 17U] = 1;
		else if (!strcmp(identity_mutation, "wrong-lsv") && key == 1)
			identity->trusted_lowest_version++;
		else if (!strcmp(identity_mutation, "wrong-instance") && key == 1)
			identity->hardware_instance++;
		else if (!strcmp(identity_mutation, "duplicate") && key == 1) {
			memset(identity->variable_name, 0,
				sizeof(identity->variable_name));
			for (size_t i = 0; i < strlen(key_names[0]); i++)
				identity->variable_name[i] = (uint8_t)key_names[0][i];
			for (size_t i = 0; i < 16; i++)
				identity->variable_name[strlen(key_names[0]) + i] =
					(uint8_t)hex[(instance >> ((15U - i) * 4U)) & 0xfU];
			identity->variable_name_bytes = (uint32_t)
				((strlen(key_names[0]) + 17U) * sizeof(uint16_t));
		}
	}
	return CB_SUCCESS;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	protected_storage_address = storage;
	protected_storage_size = size;
	return context == &authority_closed && storage != NULL && protect_authority;
}

static void put32(size_t offset, uint32_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		store[offset + i] = (uint8_t)(value >> (i * 8U));
}

static void empty_store(void)
{
	static const uint8_t store_guid[16] = {
		0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
		0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
	};
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = STORE_SIZE,
		.maximum_name_size = 128,
		.maximum_data_size = 128,
		.maximum_records = ENTRY_COUNT,
	};

	memset(store, 0xff, sizeof(store));
	memcpy(store, store_guid, sizeof(store_guid));
	put32(16, sizeof(store));
	store[20] = 0x5a;
	store[21] = 0xfe;
	store[22] = 0;
	store[23] = 0;
	put32(24, 0);
	memset(&index, 0, sizeof(index));
	index.entries = entries;
	index.entry_capacity = ARRAY_SIZE(entries);
	assert(payload_mm_authvar_store_scan(&index, store, sizeof(store),
		&limits) == CB_SUCCESS);
}

static void populate_reserved_keys(void)
{
	static const uint8_t state_data[PAYLOAD_MM_FMP_STATE_WIRE_SIZE];
	static const uint32_t legacy_data = 1U;
	uint32_t offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;

	for (uint32_t key = 0; key < ARRAY_SIZE(key_names); key++) {
		struct payload_mm_fmp_state_identity identity;
		struct payload_mm_authvar_record_descriptor descriptor = {
			.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
		};
		struct payload_mm_authvar_record_span span;
		uint32_t record_size;

		assert(payload_mm_fmp_owner_authvar_identity(key, &identity) ==
			CB_SUCCESS);
		memcpy(descriptor.vendor_guid, identity.namespace_guid.b,
			sizeof(descriptor.vendor_guid));
		descriptor.name = identity.variable_name;
		descriptor.name_size = identity.variable_name_bytes;
		span.data = key ? (const void *)&legacy_data : (const void *)state_data;
		span.size = key ? sizeof(legacy_data) : sizeof(state_data);
		assert(payload_mm_authvar_record_encode(&descriptor, &span, 1U,
			PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED, store + offset,
			sizeof(store) - offset, &record_size));
		store[offset + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
		offset += record_size;
	}
	{
		const struct payload_mm_authvar_store_limits limits = {
			.maximum_store_size = STORE_SIZE,
			.maximum_name_size = 128,
			.maximum_data_size = 128,
			.maximum_records = ENTRY_COUNT,
		};

		memset(&index, 0, sizeof(index));
		index.entries = entries;
		index.entry_capacity = ARRAY_SIZE(entries);
		assert(payload_mm_authvar_store_scan(&index, store, sizeof(store),
			&limits) == CB_SUCCESS && index.entry_count == ARRAY_SIZE(key_names));
	}
}

static void expect_public_get(void)
{
	for (uint32_t key = 0; key < ARRAY_SIZE(key_names); key++) {
		struct payload_mm_fmp_state_identity identity;
		struct payload_mm_authvar_get_result get;

		assert(payload_mm_fmp_owner_authvar_identity(key, &identity) ==
			CB_SUCCESS);
		assert(payload_mm_authvar_store_get(&index, identity.namespace_guid.b,
			identity.variable_name, identity.variable_name_bytes, 128U, false,
			&get) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(get.entry &&
			get.attributes == PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES &&
			get.required_data_size == (key ? sizeof(uint32_t) :
				PAYLOAD_MM_FMP_STATE_WIRE_SIZE));
		assert(payload_mm_authvar_store_data(&index, get.entry));
	}
}

static void expect_public_next(void)
{
	uint8_t zero_guid[16] = { 0 };
	const uint8_t *guid = zero_guid;
	const void *name = NULL;
	size_t name_size = 0U;
	uint32_t seen = 0U;

	for (size_t at = 0; at < ARRAY_SIZE(key_names); at++) {
		struct payload_mm_authvar_next_result next;
		uint32_t matched = UINT32_MAX;

		assert(payload_mm_authvar_store_get_next(&index, guid, name, name_size,
			128U, false, &next) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(next.entry && next.required_name_size == next.entry->name_size &&
			next.entry->attributes == PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES);
		name = payload_mm_authvar_store_name(&index, next.entry);
		assert(name);
		for (uint32_t key = 0; key < ARRAY_SIZE(key_names); key++) {
			struct payload_mm_fmp_state_identity identity;

			assert(payload_mm_fmp_owner_authvar_identity(key, &identity) ==
				CB_SUCCESS);
			if (!memcmp(next.entry->vendor_guid, identity.namespace_guid.b,
				sizeof(next.entry->vendor_guid)) &&
			    next.entry->name_size == identity.variable_name_bytes &&
			    !memcmp(name, identity.variable_name,
				identity.variable_name_bytes)) {
				assert(matched == UINT32_MAX);
				matched = key;
			}
		}
		assert(matched != UINT32_MAX && !(seen & (1U << matched)) &&
			payload_mm_fmp_owner_authvar_reservation(
				next.entry->vendor_guid, name, next.entry->name_size) ==
				PAYLOAD_MM_FMP_OWNER_AUTHVAR_RESERVED);
		seen |= 1U << matched;
		guid = next.entry->vendor_guid;
		name_size = next.entry->name_size;
	}
	{
		struct payload_mm_authvar_next_result next;

		assert(seen == (1U << ARRAY_SIZE(key_names)) - 1U);
		assert(payload_mm_authvar_store_get_next(&index, guid, name, name_size,
			128U, false, &next) == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
		assert(!next.entry && !next.required_name_size);
	}
}

static void expect_reserved(const struct payload_mm_fmp_state_identity *identity,
	uint32_t attributes, const void *data, size_t data_size)
{
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = attributes,
		.name = identity->variable_name,
		.name_size = identity->variable_name_bytes,
		.data = data,
		.data_size = data_size,
	};
	struct payload_mm_authvar_set_snapshot snapshot = {
		.request = &request,
		.index = &index,
	};
	struct payload_mm_authvar_set_plan plan;

	memcpy(request.vendor_guid, identity->namespace_guid.b,
		sizeof(request.vendor_guid));
	assert(payload_mm_authvar_set_preflight(&snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED);
}

static void expect_unrelated(uint64_t status,
	enum payload_mm_fmp_owner_authvar_reservation reservation)
{
	static const uint8_t unrelated_guid[16] = { 0xa5U };
	static const uint16_t unrelated_name[] = { 'U', 'n', 'r', 'e', 'l', 0 };
	static const uint8_t value = 1U;
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
		.name = unrelated_name,
		.name_size = sizeof(unrelated_name),
		.data = &value,
		.data_size = sizeof(value),
	};
	struct payload_mm_authvar_set_snapshot snapshot = {
		.request = &request,
		.index = &index,
	};
	struct payload_mm_authvar_set_plan plan;

	memcpy(request.vendor_guid, unrelated_guid, sizeof(request.vendor_guid));
	assert(payload_mm_fmp_owner_authvar_reservation(unrelated_guid,
		unrelated_name, sizeof(unrelated_name)) == reservation);
	assert(payload_mm_authvar_set_preflight(&snapshot, &plan) == status);
}

int main(int argc, char **argv)
{
	static const uint8_t value = 1;
	static const uint8_t auth2[40] = { 0 };
	struct payload_mm_fmp_state_identity identity;

	if (argc == 2 && !strcmp(argv[1], "zero-instance"))
		hardware_instance = 0U;
	empty_store();
	if (argc == 2 && !strcmp(argv[1], "uninstalled")) {
		expect_unrelated(PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED,
			PAYLOAD_MM_FMP_OWNER_AUTHVAR_AUTHORITY_INVALID);
		return 0;
	}
	if (argc == 2 && !strncmp(argv[1], "invalid-", 8)) {
		identity_mutation = argv[1] + 8;
		assert(payload_mm_fmp_owner_authvar_identity_install(protected_storage,
			&authority_closed) == CB_ERR);
		assert(payload_mm_fmp_owner_authvar_reservation(namespace_guid,
			(const uint16_t[]){ 'A', 0 }, 4) ==
			PAYLOAD_MM_FMP_OWNER_AUTHVAR_AUTHORITY_INVALID);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "failed-install")) {
		struct payload_mm_authvar_policy_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
			.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
			.name = (const uint16_t[]){ 'A', 0 },
			.name_size = 4,
			.data = &value,
			.data_size = sizeof(value),
		};
		struct payload_mm_authvar_set_snapshot snapshot = {
			.request = &request, .index = &index,
		};
		struct payload_mm_authvar_set_plan plan;

		protect_authority = false;
		assert(payload_mm_fmp_owner_authvar_identity_install(protected_storage,
			&authority_closed) == CB_ERR);
		assert(payload_mm_authvar_set_preflight(&snapshot, &plan) ==
			PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED);
		return 0;
	}
	assert(payload_mm_fmp_owner_authvar_identity_install(protected_storage,
		&authority_closed) == CB_SUCCESS);
	{
		size_t protected_size;
		const uint8_t *protected =
			payload_mm_fmp_owner_authvar_test_storage(&protected_size);

		assert(protected && protected_storage_address == protected &&
			protected_storage_size == protected_size && protected_size >= 8U &&
			payload_mm_fmp_owner_authvar_storage_overlaps(protected,
				protected_size) &&
			payload_mm_fmp_owner_authvar_storage_overlaps(
				protected + protected_size - 4U, 8U) &&
			!payload_mm_fmp_owner_authvar_storage_overlaps(&identity,
				sizeof(identity)));
	}
	assert(payload_mm_fmp_owner_authvar_identity_install(protected_storage,
		&authority_closed) == CB_ERR);
	authority_closed = true;
	populate_reserved_keys();
	expect_public_get();
	expect_public_next();
	expect_unrelated(PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_FMP_OWNER_AUTHVAR_NOT_RESERVED);
	for (uint32_t key = 0; key < ARRAY_SIZE(key_names); key++) {
		assert(payload_mm_fmp_owner_authvar_identity(key, &identity) ==
			CB_SUCCESS);
		assert(identity.hardware_instance == hardware_instance &&
			identity.variable_name_bytes ==
			(strlen(key_names[key]) + (hardware_instance ? 17U : 1U)) *
				sizeof(uint16_t));
		expect_reserved(&identity, PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
			&value, sizeof(value));
		expect_reserved(&identity, 0, NULL, 0);
		expect_reserved(&identity,
			PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
			auth2, sizeof(auth2));
		expect_reserved(&identity,
			PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES |
			PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE,
			auth2, sizeof(auth2));
		assert(payload_mm_fmp_owner_authvar_reservation(
			identity.namespace_guid.b, identity.variable_name,
			identity.variable_name_bytes) ==
			PAYLOAD_MM_FMP_OWNER_AUTHVAR_RESERVED);
		identity.variable_name[0] ^= 1U;
		assert(payload_mm_fmp_owner_authvar_reservation(
			identity.namespace_guid.b, identity.variable_name,
			identity.variable_name_bytes) ==
			PAYLOAD_MM_FMP_OWNER_AUTHVAR_NOT_RESERVED);
	}
	assert(payload_mm_fmp_state_identity_get_for_key(0, &identity) == CB_ERR);
	if (argc == 2 && !strcmp(argv[1], "corrupt-live"))
		payload_mm_fmp_owner_authvar_test_corrupt_identity(false);
	else if (argc == 2 && !strcmp(argv[1], "corrupt-seal"))
		payload_mm_fmp_owner_authvar_test_corrupt_identity(true);
	else if (argc == 2 && !strcmp(argv[1], "corrupt-control"))
		payload_mm_fmp_owner_authvar_test_corrupt_control(false);
	else
		return 0;
	assert(payload_mm_fmp_owner_authvar_reservation(namespace_guid,
		(const uint16_t[]){ 'A', 0 }, 4) ==
		PAYLOAD_MM_FMP_OWNER_AUTHVAR_AUTHORITY_INVALID);
	expect_unrelated(PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED,
		PAYLOAD_MM_FMP_OWNER_AUTHVAR_AUTHORITY_INVALID);
	return 0;
}
