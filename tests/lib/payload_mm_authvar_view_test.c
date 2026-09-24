/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_bundle.h>
#include <boot/payload_mm_authvar_view.h>
#include <commonlib/helpers.h>

#include <stdlib.h>
#include <string.h>

#include "payload_mm_authvar_store_edk2_2609_fixture.h"

#define expect(condition) do { \
	if (!(condition)) \
		abort(); \
} while (0)

static const uint8_t global_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static const uint8_t cert_db_guid[16] = {
	0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
	0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
};
static const uint8_t private_guid[16];
static const uint8_t setup_name[] = {
	'S', 0, 'e', 0, 't', 0, 'u', 0, 'p', 0, 'M', 0, 'o', 0, 'd', 0,
	'e', 0, 0, 0,
};
static const uint8_t signature_name[] = {
	'S', 0, 'i', 0, 'g', 0, 'n', 0, 'a', 0, 't', 0, 'u', 0, 'r', 0,
	'e', 0, 'S', 0, 'u', 0, 'p', 0, 'p', 0, 'o', 0, 'r', 0, 't', 0,
	0, 0,
};
static const uint8_t secure_name[] = {
	'S', 0, 'e', 0, 'c', 0, 'u', 0, 'r', 0, 'e', 0, 'B', 0, 'o', 0,
	'o', 0, 't', 0, 0, 0,
};
static const uint8_t certdbv_name[] = {
	'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 'v', 0, 0, 0,
};
static const uint8_t vendor_name[] = {
	'V', 0, 'e', 0, 'n', 0, 'd', 0, 'o', 0, 'r', 0, 'K', 0, 'e', 0,
	'y', 0, 's', 0, 0, 0,
};
static const uint8_t persistent_name[] = { 'F', 0, 'x', 0, 0, 0 };
static const uint8_t long_persistent_name[] = {
	'L', 0, 'o', 0, 'n', 0, 'g', 0, 'P', 0, 'e', 0, 'r', 0, 's', 0,
	'i', 0, 's', 0, 't', 0, 'e', 0, 'n', 0, 't', 0, 'X', 0, 0, 0,
};
static const uint8_t signature_data[] = {
	0x12, 0xa5, 0x6c, 0x82, 0x10, 0xcf, 0xc9, 0x4a,
	0xb1, 0x87, 0xbe, 0x01, 0x49, 0x66, 0x31, 0xbd,
	0x26, 0x16, 0xc4, 0xc1, 0x4c, 0x50, 0x92, 0x40,
	0xac, 0xa9, 0x41, 0xf9, 0x36, 0x93, 0x43, 0x28,
	0x07, 0x53, 0x3e, 0xff, 0xd0, 0x9f, 0xc9, 0x48,
	0x85, 0xf1, 0x8a, 0xd5, 0x6c, 0x70, 0x1e, 0x01,
	0xae, 0x0f, 0x3e, 0x09, 0xc4, 0xa6, 0x50, 0x4f,
	0x9f, 0x1b, 0xd4, 0x1e, 0x2b, 0x89, 0xc1, 0x9a,
	0xe8, 0x66, 0x57, 0x3c, 0x9c, 0x26, 0x34, 0x4e,
	0xaa, 0x14, 0xed, 0x77, 0x6e, 0x85, 0xb3, 0xb6,
	0xa1, 0x59, 0xc0, 0xa5, 0xe4, 0x94, 0xa7, 0x4a,
	0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72,
};
static const uint8_t certdbv_data[] = { 4, 0, 0, 0 };

struct expected_variable {
	const uint8_t *guid;
	const uint8_t *name;
	size_t name_size;
	const uint8_t *data;
	size_t data_size;
	uint32_t attributes;
	uint8_t mode;
};

static const struct expected_variable expected[] = {
	{ global_guid, setup_name, sizeof(setup_name), NULL, 1U, 0x06,
		PAYLOAD_MM_AUTHVAR_MODE_SETUP },
	{ global_guid, signature_name, sizeof(signature_name), signature_data,
		sizeof(signature_data), 0x06, 0U },
	{ global_guid, secure_name, sizeof(secure_name), NULL, 1U, 0x06,
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT },
	{ cert_db_guid, certdbv_name, sizeof(certdbv_name), certdbv_data,
		sizeof(certdbv_data), 0x26, 0U },
	{ global_guid, vendor_name, sizeof(vendor_name), NULL, 1U, 0x06,
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS },
};

static uint8_t store[512];
static struct payload_mm_authvar_store_entry entries[8];
static struct payload_mm_authvar_store_index store_index;
static struct payload_mm_authvar_view view;

static void collision_store(const struct expected_variable *collision,
	uint8_t state);

static void scan_fixture(uint32_t attributes)
{
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = sizeof(edk2_2609_store_fixture),
		.maximum_name_size = 128U,
		.maximum_data_size = 128U,
		.maximum_records = ARRAY_SIZE(entries),
	};

	memcpy(store, edk2_2609_store_fixture,
		sizeof(edk2_2609_store_fixture));
	store[32] = (uint8_t)attributes;
	store[33] = (uint8_t)(attributes >> 8);
	store[34] = (uint8_t)(attributes >> 16);
	store[35] = (uint8_t)(attributes >> 24);
	memset(entries, 0, sizeof(entries));
	store_index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = ARRAY_SIZE(entries),
	};
	expect(payload_mm_authvar_store_scan(&store_index, store,
		sizeof(edk2_2609_store_fixture),
		&limits) == CB_SUCCESS);
}

static void expect_value(const struct payload_mm_authvar_view_value *value,
	const struct expected_variable *oracle, uint8_t modes)
{
	uint8_t scalar = !!(modes & oracle->mode);
	const uint8_t *data = oracle->data ? oracle->data : &scalar;

	expect(!memcmp(value->vendor_guid, oracle->guid, 16U));
	expect(value->name_size == oracle->name_size);
	expect(!memcmp(value->name, oracle->name, oracle->name_size));
	expect(value->data_size == oracle->data_size);
	expect(!memcmp(value->data, data, oracle->data_size));
	expect(value->attributes == oracle->attributes);
}

static void test_get_and_modes(void)
{
	struct payload_mm_authvar_view_value value;

	for (uint8_t modes = 0U; modes < 8U; modes++) {
		for (size_t at_runtime = 0U; at_runtime < 2U; at_runtime++) {
			expect(payload_mm_authvar_view_init(&view, &store_index, modes,
				at_runtime) == CB_SUCCESS);
			for (size_t key = 0U; key < ARRAY_SIZE(expected); key++) {
				uint64_t status;

				memset(&value, 0xa5, sizeof(value));
				status = payload_mm_authvar_view_get(&view,
					expected[key].guid, expected[key].name,
					expected[key].name_size,
					(uint32_t)expected[key].data_size, &value);
				expect(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
				expect_value(&value, &expected[key], modes);

				memset(&value, 0xa5, sizeof(value));
				status = payload_mm_authvar_view_get(&view,
					expected[key].guid, expected[key].name,
					expected[key].name_size,
					(uint32_t)expected[key].data_size - 1U, &value);
				expect(status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
				expect(!value.vendor_guid && !value.name && !value.data);
				expect(!value.name_size);
				expect(value.data_size == expected[key].data_size);
				expect(value.attributes == expected[key].attributes);
			}
		}
	}
	/* Persistent state cannot rederive the sealed SecureBoot projection. */
	expect(payload_mm_authvar_view_init(&view, &store_index,
		PAYLOAD_MM_AUTHVAR_MODE_SETUP | PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT,
		true) == CB_SUCCESS);
	expect(payload_mm_authvar_view_get(&view, global_guid, secure_name,
		sizeof(secure_name), 1U, &value) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	expect(*(const uint8_t *)value.data == 1U);

	/* Persistent GET retains the lower semantic layer's exact publication. */
	memset(&value, 0xa5, sizeof(value));
	expect(payload_mm_authvar_view_get(&view, private_guid, persistent_name,
		sizeof(persistent_name), 3U, &value) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	expect(value.data_size == 3U && value.attributes == 7U);
	expect(!memcmp(value.data, (uint8_t[]) { 0xde, 0xad, 0xbe }, 3U));
	memset(&value, 0xa5, sizeof(value));
	expect(payload_mm_authvar_view_get(&view, private_guid, persistent_name,
		sizeof(persistent_name), 2U, &value) ==
		PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
	expect(value.data_size == 3U && value.attributes == 7U);
	expect(!value.vendor_guid && !value.name && !value.data && !value.name_size);
	scan_fixture(PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS);
	expect(payload_mm_authvar_view_init(&view, &store_index, 0U, true) ==
		CB_SUCCESS);
	memset(&value, 0xa5, sizeof(value));
	expect(payload_mm_authvar_view_get(&view, private_guid, persistent_name,
		sizeof(persistent_name), 3U, &value) ==
		PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	expect(!memcmp(&value, &(struct payload_mm_authvar_view_value) { 0 },
		sizeof(value)));
	scan_fixture(7U);
}

static void test_next(void)
{
	static const uint8_t zero_guid[16];
	struct payload_mm_authvar_view_value value;
	const uint8_t *cursor_guid = zero_guid;
	const void *cursor_name = NULL;
	size_t cursor_size = 0U;
	uint64_t status;

	expect(payload_mm_authvar_view_init(&view, &store_index, 5U, false) ==
		CB_SUCCESS);
	for (size_t key = 0U; key < ARRAY_SIZE(expected); key++) {
		memset(&value, 0xa5, sizeof(value));
		expect(payload_mm_authvar_view_get_next(&view, cursor_guid,
			cursor_name, cursor_size, 128U, &value) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		expect_value(&value, &expected[key], 5U);
		cursor_guid = value.vendor_guid;
		cursor_name = value.name;
		cursor_size = value.name_size;
	}
	status = payload_mm_authvar_view_get_next(&view, cursor_guid, cursor_name,
		cursor_size, 128U, &value);
	expect(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	expect(!memcmp(value.vendor_guid, private_guid, 16U));
	expect(value.name_size == sizeof(persistent_name));
	expect(!memcmp(value.name, persistent_name, sizeof(persistent_name)));
	cursor_guid = value.vendor_guid;
	cursor_name = value.name;
	cursor_size = value.name_size;
	expect(payload_mm_authvar_view_get_next(&view, cursor_guid, cursor_name,
		cursor_size, 128U, &value) == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);

	memset(&value, 0xa5, sizeof(value));
	expect(payload_mm_authvar_view_get_next(&view, zero_guid, NULL, 0U,
		sizeof(setup_name) - 1U, &value) ==
		PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
	expect(value.name_size == sizeof(setup_name));
	expect(!value.vendor_guid && !value.name && !value.data);

	/* Every synthetic cursor names its exact successor. */
	for (size_t key = 0U; key + 1U < ARRAY_SIZE(expected); key++) {
		expect(payload_mm_authvar_view_get_next(&view, expected[key].guid,
			expected[key].name, expected[key].name_size, 128U, &value) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		expect_value(&value, &expected[key + 1U], 5U);
	}

	/* Runtime hides a boot-service-only persistent record, not synthetics. */
	scan_fixture(PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS);
	expect(payload_mm_authvar_view_init(&view, &store_index, 0U, true) ==
		CB_SUCCESS);
	expect(payload_mm_authvar_view_get_next(&view, global_guid, vendor_name,
		sizeof(vendor_name), 128U, &value) ==
		PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);

	/* The synthetic-to-persistent boundary retains NEXT sizing semantics. */
	const struct expected_variable long_variable = {
		.guid = private_guid,
		.name = long_persistent_name,
		.name_size = sizeof(long_persistent_name),
		.attributes = 7U,
	};
	collision_store(&long_variable, PAYLOAD_MM_AUTHVAR_STATE_ADDED);
	expect(payload_mm_authvar_view_init(&view, &store_index, 0U, false) ==
		CB_SUCCESS);
	memset(&value, 0xa5, sizeof(value));
	expect(payload_mm_authvar_view_get_next(&view, global_guid, vendor_name,
		sizeof(vendor_name), sizeof(vendor_name), &value) ==
		PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
	expect(value.name_size == sizeof(long_persistent_name));
	expect(!value.vendor_guid && !value.name && !value.data);

	memset(&value, 0xa5, sizeof(value));
	expect(payload_mm_authvar_view_get_next(&view, private_guid,
		(uint8_t[]) { 'N', 0, 0, 0 }, 4U, 128U, &value) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(&value, &(struct payload_mm_authvar_view_value) { 0 },
		sizeof(value)));
}

static void put32(uint8_t *bytes, uint32_t value)
{
	for (size_t i = 0U; i < 4U; i++)
		bytes[i] = (uint8_t)(value >> (8U * i));
}

static void put16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void collision_store(const struct expected_variable *collision,
	uint8_t state)
{
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = sizeof(store),
		.maximum_name_size = 128U,
		.maximum_data_size = 128U,
		.maximum_records = ARRAY_SIZE(entries),
	};
	size_t data_offset;

	memset(store, 0xff, sizeof(store));
	memcpy(store, edk2_2609_store_fixture, PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE);
	put32(store + 16U, sizeof(store));
	put16(store + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		PAYLOAD_MM_AUTHVAR_RECORD_START_ID);
	store[PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 3U] = 0U;
	put32(store + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 4U,
		collision->attributes);
	memset(store + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 8U, 0, 36U);
	put32(store + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 36U,
		(uint32_t)collision->name_size);
	put32(store + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 40U, 1U);
	memcpy(store + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 44U,
		collision->guid, 16U);
	memcpy(store + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE +
		PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE, collision->name,
		collision->name_size);
	data_offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE +
		PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + collision->name_size;
	data_offset = (data_offset + 3U) & ~(size_t)3U;
	store[data_offset] = 0U;
	store[PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 2U] = state;
	memset(entries, 0, sizeof(entries));
	store_index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = ARRAY_SIZE(entries),
	};
	expect(payload_mm_authvar_store_scan(&store_index, store, sizeof(store),
		&limits) == CB_SUCCESS);
}

static void test_collisions_and_reserved(void)
{
	for (size_t key = 0U; key < ARRAY_SIZE(expected); key++) {
		collision_store(&expected[key], PAYLOAD_MM_AUTHVAR_STATE_ADDED);
		expect(payload_mm_authvar_view_init(&view, &store_index, 0U, false) ==
			CB_ERR_ARG);
		expect(payload_mm_authvar_view_key_reserved(expected[key].guid,
			expected[key].name, expected[key].name_size));
		collision_store(&expected[key], PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED);
		expect(payload_mm_authvar_view_init(&view, &store_index, 0U, false) ==
			CB_SUCCESS);
	}
	expect(!payload_mm_authvar_view_key_reserved(private_guid, setup_name,
		sizeof(setup_name)));
	expect(!payload_mm_authvar_view_key_reserved(global_guid, setup_name,
		sizeof(setup_name) - 2U));
}

static void test_query_and_admission(void)
{
	const struct payload_mm_authvar_store_policy policy = {
		.maximum_storage = sizeof(edk2_2609_store_fixture) -
			PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		.maximum_record_size = sizeof(edk2_2609_store_fixture),
	};
	struct payload_mm_authvar_query_result direct;
	struct payload_mm_authvar_query_result projected;
	struct payload_mm_authvar_view_value value;
	uint64_t direct_status;
	uint64_t projected_status;
	uint8_t before[sizeof(value)];
	union {
		struct payload_mm_authvar_view_value alignment;
		uint8_t bytes[64];
	} alias;
	union {
		struct payload_mm_authvar_view alignment;
		uint8_t bytes[sizeof(struct payload_mm_authvar_view) + 1U];
	} bad_view;
	union {
		struct payload_mm_authvar_view_value alignment;
		uint8_t bytes[sizeof(struct payload_mm_authvar_view_value) + 1U];
	} bad_value;
	union {
		struct payload_mm_authvar_store_policy alignment;
		uint8_t bytes[sizeof(struct payload_mm_authvar_store_policy) + 1U];
	} bad_policy;
	uint8_t view_before[sizeof(view)];
	uint8_t index_before[sizeof(store_index)];
	uint8_t entries_before[sizeof(entries)];
	uint8_t query_before[sizeof(projected)];

	scan_fixture(7U);
	expect(payload_mm_authvar_view_init(&view, &store_index, 7U, false) ==
		CB_SUCCESS);
	for (uint32_t attributes = 0U; attributes < 128U; attributes++) {
		memset(&direct, 0xa5, sizeof(direct));
		memset(&projected, 0xa5, sizeof(projected));
		direct_status = payload_mm_authvar_store_query(&store_index, &policy,
			attributes, false, &direct);
		projected_status = payload_mm_authvar_view_query(&view, &policy,
			attributes, &projected);
		expect(direct_status == projected_status);
		expect(!memcmp(&direct, &projected, sizeof(direct)));
	}

	/* Unknown mode bits and aliased outputs are rejected without publication. */
	memset(&view, 0xa5, sizeof(view));
	expect(payload_mm_authvar_view_init(&view, &store_index, 8U, false) ==
		CB_ERR_ARG);
	expect(!memcmp(&view, (uint8_t[sizeof(view)]) { [0 ... sizeof(view) - 1] = 0xa5 },
		sizeof(view)));
	expect(payload_mm_authvar_view_init(&view, &store_index, 0U, false) ==
		CB_SUCCESS);
	memset(&value, 0xa5, sizeof(value));
	memcpy(before, &value, sizeof(value));
	expect(payload_mm_authvar_view_get(&view, global_guid,
		(const void *)(UINTPTR_MAX - 1U), 4U, 1U, &value) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(before, &value, sizeof(value)));

	memset(&alias, 0, sizeof(alias));
	memcpy(alias.bytes, setup_name, sizeof(setup_name));
	memcpy(before, &alias.alignment, sizeof(before));
	expect(payload_mm_authvar_view_get(&view, global_guid, alias.bytes,
		sizeof(setup_name), 1U, &alias.alignment) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(before, &alias.alignment, sizeof(before)));

	memcpy(before, store, sizeof(before));
	expect(payload_mm_authvar_view_get(&view, global_guid, setup_name,
		sizeof(setup_name), 1U,
		(struct payload_mm_authvar_view_value *)(void *)store) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(before, store, sizeof(before)));

	/* Malformed and misaligned descriptors never publish or clobber inputs. */
	memset(&value, 0xa5, sizeof(value));
	memcpy(before, &value, sizeof(value));
	expect(payload_mm_authvar_view_get(
		(const struct payload_mm_authvar_view *)(bad_view.bytes + 1U),
		global_guid, setup_name, sizeof(setup_name), 1U, &value) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(before, &value, sizeof(value)));
	expect(payload_mm_authvar_view_get(&view, global_guid, setup_name,
		sizeof(setup_name), 1U,
		(struct payload_mm_authvar_view_value *)(bad_value.bytes + 1U)) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);

	memcpy(view_before, &view, sizeof(view));
	expect(payload_mm_authvar_view_get(&view, global_guid, setup_name,
		sizeof(setup_name), 1U,
		(struct payload_mm_authvar_view_value *)(void *)&view) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(view_before, &view, sizeof(view)));
	memcpy(index_before, &store_index, sizeof(store_index));
	expect(payload_mm_authvar_view_get(&view, global_guid, setup_name,
		sizeof(setup_name), 1U,
		(struct payload_mm_authvar_view_value *)(void *)&store_index) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(index_before, &store_index, sizeof(store_index)));
	expect(!((uintptr_t)entries % _Alignof(struct payload_mm_authvar_view_value)));
	memcpy(entries_before, entries, sizeof(entries));
	expect(payload_mm_authvar_view_get(&view, global_guid, setup_name,
		sizeof(setup_name), 1U,
		(struct payload_mm_authvar_view_value *)(void *)entries) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(entries_before, entries, sizeof(entries)));

	memcpy(bad_policy.bytes + 1U, &policy, sizeof(policy));
	memset(&projected, 0xa5, sizeof(projected));
	memcpy(query_before, &projected, sizeof(projected));
	expect(payload_mm_authvar_view_query(&view,
		(const struct payload_mm_authvar_store_policy *)(bad_policy.bytes + 1U),
		7U, &projected) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(&projected, query_before, sizeof(projected)));

	/* Init also rejects output aliases without changing the source object. */
	memcpy(index_before, &store_index, sizeof(store_index));
	expect(payload_mm_authvar_view_init(
		(struct payload_mm_authvar_view *)(void *)&store_index,
		&store_index, 0U, false) == CB_ERR_ARG);
	expect(!memcmp(index_before, &store_index, sizeof(store_index)));
}

int main(void)
{
	scan_fixture(7U);
	test_get_and_modes();
	test_next();
	test_collisions_and_reserved();
	test_query_and_admission();
	return 0;
}
