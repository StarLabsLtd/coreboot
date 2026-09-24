/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_route.h>
#include <boot/payload_mm_authvar_store.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "payload_mm_authvar_set_preflight.h"

extern int dprintf(int file, const char *format, ...);

#define expect(condition) do { \
	if (!(condition)) { \
		dprintf(2, "SET preflight assertion failed at line %d\n", __LINE__); \
		abort(); \
	} \
} while (0)
#define STORE_SIZE 512U
#define ENTRY_COUNT 4U

static uint8_t store[STORE_SIZE] __aligned(__BIGGEST_ALIGNMENT__);
static struct payload_mm_authvar_store_entry entries[ENTRY_COUNT]
	__aligned(__BIGGEST_ALIGNMENT__);
static struct payload_mm_authvar_store_index index;
static const struct payload_mm_authvar_store_limits limits = {
	.maximum_store_size = STORE_SIZE,
	.maximum_name_size = 128U,
	.maximum_data_size = 128U,
	.maximum_records = ENTRY_COUNT,
};
static const uint8_t store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
};
static const uint8_t private_guid[16] = { 1U };
static const uint8_t global_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static const uint8_t vendor_guid[16] = {
	0xe0, 0xe4, 0x73, 0x90, 0xec, 0x60, 0x6e, 0x4b,
	0x99, 0x03, 0x4c, 0x22, 0x3c, 0x26, 0x0f, 0x3c,
};
static const uint8_t enable_guid[16] = {
	0xc7, 0x0b, 0xa3, 0xf0, 0x08, 0xaf, 0x56, 0x45,
	0x99, 0xc4, 0x00, 0x10, 0x09, 0xc9, 0x3a, 0x44,
};
static const uint8_t custom_guid[16] = {
	0x0c, 0xec, 0x76, 0xc0, 0x28, 0x70, 0x99, 0x43,
	0xa0, 0x72, 0x71, 0xee, 0x5c, 0x44, 0x8b, 0x9f,
};
static const uint8_t cert_guid[16] = {
	0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
	0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
};
static const uint8_t pkcs7_guid[16] = {
	0x9d, 0xd2, 0xaf, 0x4a, 0xdf, 0x68, 0xee, 0x49,
	0x8a, 0xa9, 0x34, 0x7d, 0x37, 0x56, 0x65, 0xa7,
};
static const uint16_t private_name[] = { 'A', 0U };
static const uint16_t near_name[] = { 'P', 'K', 'x', 0U };
static const uint16_t pk_name[] = { 'P', 'K', 0U };
static const uint16_t secure_boot_name[] = {
	'S', 'e', 'c', 'u', 'r', 'e', 'B', 'o', 'o', 't', 0U,
};
static const uint32_t nv_bs = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
	PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
static const uint32_t nv_bs_rt = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
	PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
	PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;

static void put16(uint8_t *bytes, size_t offset, uint16_t value)
{
	bytes[offset] = (uint8_t)value;
	bytes[offset + 1U] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, size_t offset, uint32_t value)
{
	for (size_t i = 0U; i < sizeof(value); i++)
		bytes[offset + i] = (uint8_t)(value >> (8U * i));
}

static void make_empty_store(void)
{
	memset(store, 0xff, sizeof(store));
	memcpy(store, store_guid, sizeof(store_guid));
	put32(store, 16U, sizeof(store));
	store[20] = 0x5aU;
	store[21] = 0xfeU;
	put16(store, 22U, 0U);
	put32(store, 24U, 0U);
	memset(entries, 0, sizeof(entries));
	index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = ENTRY_COUNT,
	};
	expect(payload_mm_authvar_store_scan(&index, store, sizeof(store),
		&limits) == CB_SUCCESS);
}

static void make_existing(uint32_t attributes)
{
	static const uint8_t value = 0x5aU;
	const size_t offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	const size_t name_offset = offset + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE;
	const size_t data_offset = (name_offset + sizeof(private_name) + 3U) & ~3U;
	uint8_t timestamp[16] = { 0 };

	make_empty_store();
	memset(store + offset, 0, PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE);
	put16(store, offset, PAYLOAD_MM_AUTHVAR_RECORD_START_ID);
	store[offset + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	store[offset + 3U] = 0U;
	put32(store, offset + 4U, attributes);
	if (attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED) {
		put16(timestamp, 0U, 2026U);
		timestamp[2] = 9U;
		timestamp[3] = 24U;
	}
	memcpy(store + offset + 16U, timestamp, sizeof(timestamp));
	put32(store, offset + 36U, sizeof(private_name));
	put32(store, offset + 40U, sizeof(value));
	memcpy(store + offset + 44U, private_guid, sizeof(private_guid));
	memcpy(store + name_offset, private_name, sizeof(private_name));
	store[data_offset] = value;
	memset(entries, 0, sizeof(entries));
	index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = ENTRY_COUNT,
	};
	expect(payload_mm_authvar_store_scan(&index, store, sizeof(store),
		&limits) == CB_SUCCESS);
}

static size_t make_auth2(uint8_t *data, const void *payload, size_t payload_size)
{
	memset(data, 0, 41U + payload_size);
	put16(data, 0U, 2026U);
	data[2] = 9U;
	data[3] = 24U;
	data[6] = 1U;
	put32(data, 16U, 25U);
	put16(data, 20U, 0x0200U);
	put16(data, 22U, 0x0ef1U);
	memcpy(data + 24U, pkcs7_guid, sizeof(pkcs7_guid));
	data[40] = 0x30U;
	if (payload_size)
		memcpy(data + 41U, payload, payload_size);
	return 41U + payload_size;
}

static struct payload_mm_authvar_policy_request request_for(const void *name,
	size_t name_size, uint32_t attributes, const void *data, size_t data_size)
{
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = attributes,
		.name = name,
		.name_size = name_size,
		.data = data,
		.data_size = data_size,
	};

	memcpy(request.vendor_guid, private_guid, sizeof(request.vendor_guid));
	return request;
}

static void check(struct payload_mm_authvar_policy_request *request,
	bool at_runtime, uint64_t expected_status,
	enum payload_mm_authvar_set_kind expected_kind)
{
	struct payload_mm_authvar_set_snapshot snapshot = {
		.request = request,
		.index = &index,
		.at_runtime = at_runtime,
	};
	struct payload_mm_authvar_set_plan plan;
	uint64_t status;

	memset(&plan, 0xa5, sizeof(plan));
	status = payload_mm_authvar_set_preflight(&snapshot, &plan);
	expect(status == expected_status);
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		expect(plan.kind == expected_kind);
	else
		expect(!memcmp(&plan, &(struct payload_mm_authvar_set_plan) { 0 },
			sizeof(plan)));
}

static void check_post(struct payload_mm_authvar_policy_request *request,
	bool at_runtime, uint64_t expected_post)
{
	struct payload_mm_authvar_set_snapshot snapshot = {
		.request = request,
		.index = &index,
		.at_runtime = at_runtime,
	};
	struct payload_mm_authvar_set_plan plan;

	memset(&plan, 0xa5, sizeof(plan));
	expect(payload_mm_authvar_set_preflight(&snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	expect(plan.kind == PAYLOAD_MM_AUTHVAR_SET_AUTH2);
	expect(plan.post_auth_status == expected_post);
}

static void ordinary_matrix(void)
{
	static const uint8_t value = 1U;
	struct payload_mm_authvar_policy_request request;

	make_empty_store();
	request = request_for(private_name, sizeof(private_name), nv_bs,
		&value, sizeof(value));
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_SET_ORDINARY_WRITE);
	request.data_size = 0U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND, 0);
	request.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_SET_NOOP);
	request.data_size = sizeof(value);
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_SET_ORDINARY_WRITE);
	request.attributes = 0U;
	request.data_size = 0U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND, 0);
	request.data_size = sizeof(value);
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND, 0);
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND, 0);
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND, 0);

	make_existing(nv_bs);
	request = request_for(private_name, sizeof(private_name), nv_bs,
		&value, sizeof(value));
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_SET_ORDINARY_WRITE);
	request.data_size = 0U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_SET_ORDINARY_DELETE);
	request.attributes = 0U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_SET_ORDINARY_DELETE);
	request.data = &value;
	request.data_size = sizeof(value);
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_SET_ORDINARY_DELETE);
	request.attributes = nv_bs_rt;
	request.data_size = sizeof(value);
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);

	make_existing(nv_bs_rt);
	request = request_for(private_name, sizeof(private_name), nv_bs_rt,
		&value, sizeof(value));
	check(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_SET_ORDINARY_WRITE);
	request.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	request.data_size = 0U;
	check(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_SET_NOOP);

	make_existing(nv_bs);
	request = request_for(private_name, sizeof(private_name), nv_bs_rt,
		&value, sizeof(value));
	check(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED, 0);
	request.attributes = 0U;
	request.data_size = 0U;
	check(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED, 0);
}

static void attribute_and_auth2_matrix(void)
{
	static const uint8_t value = 1U;
	uint8_t auth2[42];
	struct payload_mm_authvar_policy_request request;
	const uint32_t time_attributes = nv_bs |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;

	make_empty_store();
	request = request_for(private_name, sizeof(private_name),
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS, &value, sizeof(value));
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED, 0);
	request.data_size = 0U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND, 0);
	request.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_SET_NOOP);
	request.data_size = sizeof(value);
	check(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	check(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
	request.attributes = nv_bs | PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
	request.attributes = nv_bs | PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED, 0);
	request.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED, 0);
	request.attributes = nv_bs;
	check(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);

	request.attributes = time_attributes;
	request.data = auth2;
	request.data_size = 8U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, 0);
	request.data_size = make_auth2(auth2, &value, sizeof(value));
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_SET_AUTH2);
	check_post(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	check_post(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	check_post(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	check_post(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
	check_post(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	request.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	check_post(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	request.attributes = time_attributes;
	auth2[24] ^= 1U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, 0);

	make_existing(time_attributes);
	request = request_for(private_name, sizeof(private_name), 0U, NULL, 0U);
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED, 0);
	request.attributes = nv_bs;
	request.data = &value;
	request.data_size = sizeof(value);
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
	request.attributes = time_attributes;
	request.data = auth2;
	request.data_size = 8U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, 0);

	/* Structural Auth2 failure precedes lookup; metadata follows lookup gates. */
	make_existing(nv_bs);
	request = request_for(private_name, sizeof(private_name), nv_bs_rt |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED, auth2, 8U);
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, 0);
	request.data_size = make_auth2(auth2, &value, sizeof(value));
	auth2[24] ^= 1U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);

	make_existing(nv_bs);
	request = request_for(private_name, sizeof(private_name), nv_bs |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED, auth2, 8U);
	check(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, 0);
	request.data_size = make_auth2(auth2, &value, sizeof(value));
	auth2[24] ^= 1U;
	check(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED, 0);

	/* Wrong-size counter auth is early; exact-size auth reaches lookup gates. */
	make_existing(nv_bs);
	request = request_for(private_name, sizeof(private_name), nv_bs |
		PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE, auth2, 1U);
	check(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED, 0);
	request.data_size = PAYLOAD_MM_AUTHVAR_COUNTER_AUTH_SIZE;
	check(&request, true, PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED, 0);
	request.attributes = nv_bs_rt |
		PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
}

static void protected_keys(void)
{
	static const uint8_t value = 1U;
	uint8_t auth2[42];
	struct payload_mm_authvar_policy_request request;

	make_empty_store();
	request = request_for(secure_boot_name, sizeof(secure_boot_name), nv_bs,
		&value, sizeof(value));
	memcpy(request.vendor_guid, global_guid, sizeof(global_guid));
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED, 0);
	request.attributes = nv_bs | PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	request.data = auth2;
	request.data_size = 8U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, 0);
	request.data_size = make_auth2(auth2, &value, sizeof(value));
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED, 0);

	request = request_for(pk_name, sizeof(pk_name), nv_bs,
		&value, sizeof(value));
	memcpy(request.vendor_guid, global_guid, sizeof(global_guid));
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
	request.name = near_name;
	request.name_size = sizeof(near_name);
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_SET_ORDINARY_WRITE);
}

static void reserved_matrix(void)
{
	static const struct {
		const uint8_t *guid;
		const char *name;
	} reserved[] = {
		{ global_guid, "SetupMode" }, { global_guid, "SignatureSupport" },
		{ global_guid, "SecureBoot" }, { global_guid, "KEKDefault" },
		{ global_guid, "PKDefault" }, { global_guid, "dbDefault" },
		{ global_guid, "dbxDefault" }, { global_guid, "dbtDefault" },
		{ global_guid, "VendorKeys" }, { vendor_guid, "VendorKeysNv" },
		{ enable_guid, "SecureBootEnable" },
		{ global_guid, "AuditMode" }, { global_guid, "DeployedMode" },
		{ custom_guid, "CustomMode" }, { cert_guid, "certdb" },
		{ cert_guid, "certdbv" },
	};
	static const uint8_t value = 1U;
	uint8_t name[64];
	struct payload_mm_authvar_policy_request request;

	make_empty_store();
	for (size_t key = 0U; key < ARRAY_SIZE(reserved); key++) {
		size_t length = strlen(reserved[key].name);

		expect(2U * (length + 1U) <= sizeof(name));
		for (size_t i = 0U; i < length; i++) {
			name[2U * i] = (uint8_t)reserved[key].name[i];
			name[2U * i + 1U] = 0U;
		}
		name[2U * length] = name[2U * length + 1U] = 0U;
		request = request_for(name, 2U * (length + 1U), nv_bs,
			&value, sizeof(value));
		memcpy(request.vendor_guid, reserved[key].guid,
			sizeof(request.vendor_guid));
		check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED, 0);
		memcpy(request.vendor_guid, private_guid,
			sizeof(request.vendor_guid));
		check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
			PAYLOAD_MM_AUTHVAR_SET_ORDINARY_WRITE);
	}
}

static void name_boundaries(void)
{
	static uint8_t name[PAYLOAD_MM_AUTHVAR_ROUTE_MAX_NAME_SIZE + 4U];
	static const uint8_t value = 1U;
	struct payload_mm_authvar_policy_request request;

	make_empty_store();
	memset(name, 0, sizeof(name));
	request = request_for(name, 2U, nv_bs, &value, sizeof(value));
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
	name[0] = 'A';
	request.name_size = 3U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
	request.name_size = 4U;
	name[2] = 'B';
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
	name[2] = name[3] = 0U;
	name[0] = name[1] = 0U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
	for (size_t i = 0U; i < PAYLOAD_MM_AUTHVAR_ROUTE_MAX_NAME_SIZE; i += 2U) {
		name[i] = 'A';
		name[i + 1U] = 0U;
	}
	name[PAYLOAD_MM_AUTHVAR_ROUTE_MAX_NAME_SIZE] = 0U;
	name[PAYLOAD_MM_AUTHVAR_ROUTE_MAX_NAME_SIZE + 1U] = 0U;
	request.name_size = PAYLOAD_MM_AUTHVAR_ROUTE_MAX_NAME_SIZE + 2U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_SET_ORDINARY_WRITE);
	request.name_size += 2U;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
	request.name = private_name;
	request.name_size = sizeof(private_name);
	request.attributes = UINT32_MAX;
	check(&request, false, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, 0);
}

static void hostile_inputs(void)
{
	static const uint8_t value = 1U;
	union {
		struct payload_mm_authvar_set_plan aligned;
		uint8_t bytes[sizeof(struct payload_mm_authvar_set_plan) + 1U];
	} misaligned;
	struct payload_mm_authvar_policy_request request;
	struct payload_mm_authvar_set_snapshot snapshot;
	struct payload_mm_authvar_set_plan plan;
	struct payload_mm_authvar_set_plan saved;
	struct payload_mm_authvar_policy_request request_saved;
	struct payload_mm_authvar_set_snapshot snapshot_saved;
	struct payload_mm_authvar_store_index index_saved;
	uint8_t store_saved[sizeof(store)];
	struct payload_mm_authvar_store_entry entries_saved[ENTRY_COUNT];

	make_empty_store();
	request = request_for(private_name, sizeof(private_name), nv_bs,
		&value, sizeof(value));
	snapshot = (struct payload_mm_authvar_set_snapshot) {
		.request = &request,
		.index = &index,
	};
	memset(&plan, 0xa5, sizeof(plan));
	saved = plan;
	expect(payload_mm_authvar_set_preflight(NULL, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(&plan, &saved, sizeof(plan)));
	expect(payload_mm_authvar_set_preflight(&snapshot, NULL) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	memset(&misaligned, 0xa5, sizeof(misaligned));
	expect(payload_mm_authvar_set_preflight(&snapshot,
		(void *)(misaligned.bytes + 1U)) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	for (size_t i = 0U; i < sizeof(misaligned.bytes); i++)
		expect(misaligned.bytes[i] == 0xa5U);

	request.name = &plan;
	request.name_size = sizeof(plan);
	expect(payload_mm_authvar_set_preflight(&snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(&plan, &saved, sizeof(plan)));
	request = request_for(private_name, sizeof(private_name), nv_bs,
		&value, sizeof(value));
	request.data = &plan;
	request.data_size = sizeof(plan);
	expect(payload_mm_authvar_set_preflight(&snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(&plan, &saved, sizeof(plan)));

	request = request_for(private_name, sizeof(private_name), nv_bs,
		&value, sizeof(value));
	snapshot.request = &request;
	snapshot.index = &index;
	snapshot_saved = snapshot;
	expect(payload_mm_authvar_set_preflight(&snapshot,
		(struct payload_mm_authvar_set_plan *)&snapshot) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(&snapshot, &snapshot_saved, sizeof(snapshot)));
	request_saved = request;
	expect(payload_mm_authvar_set_preflight(&snapshot,
		(struct payload_mm_authvar_set_plan *)&request) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(&request, &request_saved, sizeof(request)));
	index_saved = index;
	expect(payload_mm_authvar_set_preflight(&snapshot,
		(struct payload_mm_authvar_set_plan *)&index) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(&index, &index_saved, sizeof(index)));
	memcpy(store_saved, store, sizeof(store));
	expect(payload_mm_authvar_set_preflight(&snapshot,
		(struct payload_mm_authvar_set_plan *)store) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(store, store_saved, sizeof(store)));
	make_existing(nv_bs);
	request = request_for(private_name, sizeof(private_name), nv_bs,
		&value, sizeof(value));
	snapshot.request = &request;
	snapshot.index = &index;
	memcpy(entries_saved, entries, sizeof(entries));
	expect(payload_mm_authvar_set_preflight(&snapshot,
		(struct payload_mm_authvar_set_plan *)entries) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(entries, entries_saved, sizeof(entries)));

	request = request_for(private_name, sizeof(private_name), nv_bs,
		&value, sizeof(value));
	snapshot.request = (const void *)(misaligned.bytes + 1U);
	memset(&plan, 0xa5, sizeof(plan));
	expect(payload_mm_authvar_set_preflight(&snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(&plan, &(struct payload_mm_authvar_set_plan) { 0 },
		sizeof(plan)));
	snapshot.request = &request;
	request.name = (const void *)(uintptr_t)(UINTPTR_MAX - 1U);
	request.name_size = 4U;
	memset(&plan, 0xa5, sizeof(plan));
	expect(payload_mm_authvar_set_preflight(&snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(&plan, &saved, sizeof(plan)));
	request = request_for(private_name, sizeof(private_name), nv_bs,
		(const void *)(uintptr_t)(UINTPTR_MAX - 1U), 4U);
	memset(&plan, 0xa5, sizeof(plan));
	expect(payload_mm_authvar_set_preflight(&snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(&plan, &saved, sizeof(plan)));
	request = request_for(private_name, sizeof(private_name), nv_bs,
		&value, sizeof(value));
	snapshot.index = (const void *)(uintptr_t)(UINTPTR_MAX - 1U);
	memset(&plan, 0xa5, sizeof(plan));
	expect(payload_mm_authvar_set_preflight(&snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	expect(!memcmp(&plan, &(struct payload_mm_authvar_set_plan) { 0 },
		sizeof(plan)));
}

enum matrix_data {
	MATRIX_EMPTY,
	MATRIX_BYTE,
	MATRIX_AUTH2_VALID,
	MATRIX_AUTH2_INVALID,
	MATRIX_COUNTER_EXACT,
	MATRIX_COUNTER_WRONG,
	MATRIX_DATA_COUNT,
};

struct matrix_result {
	uint64_t status;
	enum payload_mm_authvar_set_kind kind;
	uint64_t post_auth_status;
};

static struct matrix_result matrix_oracle(uint32_t attributes, bool at_runtime,
	uint32_t existing_attributes, enum matrix_data data)
{
	const bool exists = existing_attributes != UINT32_MAX;
	const bool empty = data == MATRIX_EMPTY;
	const bool auth2_valid = data == MATRIX_AUTH2_VALID;
	const size_t data_size = data == MATRIX_COUNTER_EXACT ?
		PAYLOAD_MM_AUTHVAR_COUNTER_AUTH_SIZE :
		(data == MATRIX_COUNTER_WRONG ?
		 PAYLOAD_MM_AUTHVAR_COUNTER_AUTH_SIZE - 1U : (empty ? 0U : 1U));
	struct matrix_result result = {
		.status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
	};
	const uint32_t access = PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	const bool counter = attributes &
		PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE;
	const bool time = attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;

	/* EDK2 Variable.c performs these attribute/envelope gates before lookup. */
	if ((attributes & PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS) &&
	    !(attributes & PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS))
		result.status = counter ? PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED :
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	else if ((attributes & PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED) ==
		 PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE)
		result.status = counter ? PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED :
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	else if (counter && time)
		result.status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	else if (attributes & PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR)
		result.status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	else if (counter && data_size != PAYLOAD_MM_AUTHVAR_COUNTER_AUTH_SIZE)
		result.status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	else if (time && !auth2_valid)
		result.status = PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	if (result.status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return result;

	/* Lookup gates precede AuthService metadata and UpdateVariable storage. */
	if (at_runtime && exists &&
	    !(existing_attributes & PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS))
		result.status = PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	else if (exists && attributes &&
		 (attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE) !=
		 existing_attributes)
		result.status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	else if (exists && !counter && !time &&
		 existing_attributes & (PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED))
		result.status = PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	else if (counter)
		result.status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	if (result.status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return result;

	if (time) {
		result.kind = PAYLOAD_MM_AUTHVAR_SET_AUTH2;
		if (!(attributes & access))
			result.post_auth_status = PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
		else if (at_runtime &&
			 (attributes & (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
				PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)) !=
			 (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
				PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS))
			result.post_auth_status =
				PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		else if (!(attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE))
			result.post_auth_status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
		return result;
	}
	if ((attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE) && empty) {
		result.kind = PAYLOAD_MM_AUTHVAR_SET_NOOP;
		return result;
	}
	if (!(attributes & access) || empty) {
		if (!exists)
			result.status = PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
		else
			result.kind = PAYLOAD_MM_AUTHVAR_SET_ORDINARY_DELETE;
		return result;
	}
	if (at_runtime &&
	    (attributes & (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)) !=
	    (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS))
		result.status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	else if (!(attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE))
		result.status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	else
		result.kind = PAYLOAD_MM_AUTHVAR_SET_ORDINARY_WRITE;
	return result;
}

static void exhaustive_attribute_matrix(void)
{
	static const uint32_t existing_attributes[] = {
		UINT32_MAX,
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS,
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
	};
	static const uint8_t value = 1U;
	uint8_t auth2[42];
	uint8_t invalid_auth2[8] = { 0 };
	uint8_t counter_auth[PAYLOAD_MM_AUTHVAR_COUNTER_AUTH_SIZE] = { 0 };
	struct payload_mm_authvar_policy_request request;

	make_auth2(auth2, &value, sizeof(value));
	for (size_t existing = 0U; existing < ARRAY_SIZE(existing_attributes);
	     existing++) {
		if (existing_attributes[existing] == UINT32_MAX)
			make_empty_store();
		else
			make_existing(existing_attributes[existing]);
		for (uint32_t attributes = 0U; attributes < 128U; attributes++) {
			for (unsigned int runtime = 0U; runtime < 2U; runtime++) {
				for (enum matrix_data data = MATRIX_EMPTY;
				     data < MATRIX_DATA_COUNT; data++) {
					struct payload_mm_authvar_set_snapshot snapshot;
					struct payload_mm_authvar_set_plan plan;
					struct matrix_result expected;

					request = request_for(private_name,
						sizeof(private_name), attributes,
						&value, sizeof(value));
					if (data == MATRIX_EMPTY) {
						request.data = NULL;
						request.data_size = 0U;
					} else if (data == MATRIX_AUTH2_VALID) {
						request.data = auth2;
						request.data_size = sizeof(auth2);
					} else if (data == MATRIX_AUTH2_INVALID) {
						request.data = invalid_auth2;
						request.data_size = sizeof(invalid_auth2);
					} else if (data == MATRIX_COUNTER_EXACT) {
						request.data = counter_auth;
						request.data_size = sizeof(counter_auth);
					} else if (data == MATRIX_COUNTER_WRONG) {
						request.data = counter_auth;
						request.data_size = sizeof(counter_auth) - 1U;
					}
					snapshot = (struct payload_mm_authvar_set_snapshot) {
						.request = &request,
						.index = &index,
						.at_runtime = runtime,
					};
					expected = matrix_oracle(attributes, runtime,
						existing_attributes[existing], data);
					memset(&plan, 0xa5, sizeof(plan));
					expect(payload_mm_authvar_set_preflight(&snapshot,
						&plan) == expected.status);
					if (expected.status ==
					    PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
						expect(plan.kind == expected.kind);
						expect(plan.post_auth_status ==
							expected.post_auth_status);
					} else {
						expect(!memcmp(&plan,
							&(struct payload_mm_authvar_set_plan) { 0 },
							sizeof(plan)));
					}
				}
			}
		}
	}
}

int main(void)
{
	ordinary_matrix();
	attribute_and_auth2_matrix();
	protected_keys();
	reserved_matrix();
	name_boundaries();
	hostile_inputs();
	exhaustive_attribute_matrix();
	return 0;
}
