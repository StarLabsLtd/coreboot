/* SPDX-License-Identifier: GPL-2.0-only */

#include <pthread.h>
#include <sched.h>

#define payload_mm_authvar_media_begin fixture_media_begin
#define payload_mm_authvar_media_read fixture_media_read
#define payload_mm_authvar_media_end fixture_media_end
#define payload_mm_authvar_media_buffer_disjoint fixture_media_buffer_disjoint
#ifndef EXECUTOR_SOURCE_INCLUDE
#define EXECUTOR_SOURCE_INCLUDE "payload_mm_authvar_read_transaction_source.inc"
#endif
#define main executor_fixture_main
#include "payload_mm_authvar_executor_test.c"
#undef main

static const uint8_t read_name[] = { 'R', 0, 0, 0 };
static const uint8_t read_value[] = { 0x11, 0x22, 0x33, 0x44 };
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
static const uint8_t setup_mode_name[] = {
	'S', 0, 'e', 0, 't', 0, 'u', 0, 'p', 0, 'M', 0, 'o', 0, 'd', 0,
	'e', 0, 0, 0,
};
static const uint8_t signature_support_name[] = {
	'S', 0, 'i', 0, 'g', 0, 'n', 0, 'a', 0, 't', 0, 'u', 0, 'r', 0,
	'e', 0, 'S', 0, 'u', 0, 'p', 0, 'p', 0, 'o', 0, 'r', 0, 't', 0,
	0, 0,
};
static const uint8_t secure_boot_name[] = {
	'S', 0, 'e', 0, 'c', 0, 'u', 0, 'r', 0, 'e', 0, 'B', 0, 'o', 0,
	'o', 0, 't', 0, 0, 0,
};
static const uint8_t cert_db_volatile_name[] = {
	'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 'v', 0, 0, 0,
};
static const uint8_t vendor_keys_name[] = {
	'V', 0, 'e', 0, 'n', 0, 'd', 0, 'o', 0, 'r', 0, 'K', 0, 'e', 0,
	'y', 0, 's', 0, 0, 0,
};
static const uint8_t cert_db_guid[16] = {
	0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
	0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
};
static const uint8_t signature_support[] = {
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
static uint32_t view_used_offset;
#endif
static struct payload_mm_authvar_read_result *observed_result;
static uint8_t *observed_output;
static size_t observed_output_size;
static bool observe_end;
static bool nested_read_on_begin;
static uint64_t nested_read_status;
static struct payload_mm_authvar_read_result nested_read_result;
static bool callback_read_attempted;
static bool observe_gate_release;
static uint32_t gate_watcher_ready;
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
static bool force_view_get_device_error;
static bool force_view_query_out_of_range;

uint64_t __real_payload_mm_authvar_view_get(
	const struct payload_mm_authvar_view *view, const uint8_t vendor_guid[16],
	const void *name, size_t name_size, uint32_t data_capacity,
	struct payload_mm_authvar_view_value *value);

uint64_t __wrap_payload_mm_authvar_view_get(
	const struct payload_mm_authvar_view *view, const uint8_t vendor_guid[16],
	const void *name, size_t name_size, uint32_t data_capacity,
	struct payload_mm_authvar_view_value *value)
{
	if (force_view_get_device_error) {
		memset(value, 0, sizeof(*value));
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	return __real_payload_mm_authvar_view_get(view, vendor_guid, name,
		name_size, data_capacity, value);
}

uint64_t __real_payload_mm_authvar_view_query(
	const struct payload_mm_authvar_view *view,
	const struct payload_mm_authvar_store_policy *policy, uint32_t attributes,
	struct payload_mm_authvar_query_result *result);

uint64_t __wrap_payload_mm_authvar_view_query(
	const struct payload_mm_authvar_view *view,
	const struct payload_mm_authvar_store_policy *policy, uint32_t attributes,
	struct payload_mm_authvar_query_result *result)
{
	if (force_view_query_out_of_range) {
		memset(result, 0, sizeof(*result));
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	}
	return __real_payload_mm_authvar_view_query(view, policy, attributes,
		result);
}
#endif

static void *watch_gate_release(void *argument)
{
	struct payload_mm_authvar_read_result *result = argument;

	while (__atomic_load_n(&executor.busy, __ATOMIC_ACQUIRE) != 1U)
		;
	__atomic_store_n(&gate_watcher_ready, 1U, __ATOMIC_RELEASE);
	while (__atomic_load_n(&executor.busy, __ATOMIC_ACQUIRE) != 0U)
		;
	assert(__atomic_load_n(&result->completion, __ATOMIC_ACQUIRE) ==
		PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
	return NULL;
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_begin(
	uint64_t *generation, uint64_t *token)
{
	if (observe_gate_release)
		while (!__atomic_load_n(&gate_watcher_ready, __ATOMIC_ACQUIRE))
			;
	if (nested_read_on_begin) {
		uint8_t nested_output[8];
		struct payload_mm_authvar_read_request nested = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
			.name = read_name,
			.name_size = sizeof(read_name),
			.result_data = nested_output,
			.data_capacity = sizeof(nested_output),
		};

		nested_read_on_begin = false;
		memcpy(nested.vendor_guid, caller_guid, sizeof(caller_guid));
		memset(&nested_read_result, 0xa5, sizeof(nested_read_result));
		nested_read_status = payload_mm_authvar_read_transaction(&nested,
			&nested_read_result);
		for (size_t i = 0; i < sizeof(nested_read_result); i++)
			assert(((const uint8_t *)&nested_read_result)[i] == 0xa5);
	}
	return fixture_media_begin(generation, token);
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_read(
	uint64_t generation, uint64_t token, uint32_t offset, void *buffer,
	size_t size)
{
	return fixture_media_read(generation, token, offset, buffer, size);
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_end(
	uint64_t generation, uint64_t token)
{
	if (observe_end) {
		assert(observed_result->completion == PAYLOAD_MM_AUTHVAR_SERVICE_PENDING);
		assert(observed_result->status == PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING);
		for (size_t i = 0; i < observed_output_size; i++)
			assert(observed_output[i] == 0xa5);
	}
	return fixture_media_end(generation, token);
}

bool payload_mm_authvar_media_buffer_disjoint(const void *buffer, size_t size)
{
	if (buffer && size && payload_mm_authvar_buffers_overlap(buffer, size,
		media, MEDIA_SIZE))
		return false;
	return fixture_media_buffer_disjoint(buffer, size);
}

static void install_without_provider(void)
{
	static const struct payload_mm_authvar_executor_limits limits = {
		.maximum_store_size = REGION_SIZE,
		.maximum_name_size = 128,
		.maximum_data_size = 2048,
		.maximum_record_size = 8192,
		.maximum_records = 64,
	};

	assert(payload_mm_authvar_executor_install(arena, sizeof(arena), &limits) ==
		CB_SUCCESS);
}

static void seed_record(uint32_t attributes)
{
	uint8_t *record = media + FV_HEADER_SIZE +
		PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	const size_t data_offset = PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
		sizeof(read_name);

	memset(record, 0, data_offset + sizeof(read_value));
	put16(record, PAYLOAD_MM_AUTHVAR_RECORD_START_ID);
	record[2] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	put32(record + 4U, attributes);
	put32(record + 36U, sizeof(read_name));
	put32(record + 40U, sizeof(read_value));
	memcpy(record + 44U, caller_guid, sizeof(caller_guid));
	memcpy(record + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE, read_name,
		sizeof(read_name));
	memcpy(record + data_offset, read_value, sizeof(read_value));
}

static struct payload_mm_authvar_read_request get_request(void *data,
	size_t capacity)
{
	struct payload_mm_authvar_read_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		.name = read_name,
		.name_size = sizeof(read_name),
		.result_data = data,
		.data_capacity = capacity,
	};

	memcpy(request.vendor_guid, caller_guid, sizeof(caller_guid));
	return request;
}

static bool zero_buffer(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;

	for (size_t i = 0; i < size; i++)
		if (bytes[i])
			return false;
	return true;
}

static void assert_complete(const struct payload_mm_authvar_read_result *result,
	uint64_t status)
{
	assert(result->status == status);
	assert(result->completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
	assert(!result->reserved);
	assert(zero_buffer(arena, executor.sealed.required_size));
	assert(begin_count == end_count);
}

static bool filled_with(const void *buffer, uint8_t value, size_t size)
{
	const uint8_t *bytes = buffer;

	for (size_t i = 0; i < size; i++)
		if (bytes[i] != value)
			return false;
	return true;
}

static void assert_empty_metadata(const struct payload_mm_authvar_read_result *result)
{
	assert(!result->maximum_storage && !result->remaining_storage &&
		!result->maximum_variable && !result->required_name_size &&
		!result->required_data_size && !result->attributes &&
		zero_buffer(result->vendor_guid, sizeof(result->vendor_guid)));
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
static void expect_view_get(const uint8_t guid[16], const uint8_t *name,
	size_t name_size, const void *expected, size_t expected_size,
	uint32_t attributes)
{
	struct payload_mm_authvar_read_result result;
	uint8_t output[128];
	struct payload_mm_authvar_read_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		.name = name,
		.name_size = name_size,
		.result_data = output,
		.data_capacity = sizeof(output),
	};

	memcpy(request.vendor_guid, guid, sizeof(request.vendor_guid));
	memset(output, 0xa5, sizeof(output));
	memset(&result, 0xa5, sizeof(result));
	assert(payload_mm_authvar_read_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(result.required_data_size == expected_size &&
		result.attributes == attributes &&
		!memcmp(output, expected, expected_size));
	assert(zero_buffer(output + expected_size,
		sizeof(output) - expected_size));
}

static void arm_view_reconcile(uint8_t modes)
{
	executor.volatile_modes = modes;
	executor.sealed_volatile_modes = modes;
	executor.volatile_modes_valid = true;
	executor.sealed_volatile_modes_valid = true;
	executor.modes_need_reconcile = true;
	executor.sealed_modes_need_reconcile = true;
}
#endif

static void alias_matrix(void)
{
	for (unsigned int test = 0; test < 18U; test++) {
		uint8_t blob[512] __aligned(__BIGGEST_ALIGNMENT__);
		uint8_t before[sizeof(blob)];
		struct payload_mm_authvar_read_request *request = (void *)blob;
		struct payload_mm_authvar_read_result *result = (void *)(blob + 96U);
		uint8_t *name = blob + 192U;
		uint8_t *output = blob + 256U;

		memset(blob, 0xa5, sizeof(blob));
		memcpy(name, read_name, sizeof(read_name));
		*request = get_request(output, 16U);
		request->name = name;
		switch (test) {
		case 0:
			result = (void *)request;
			break;
		case 1:
			result = (void *)(blob + 8U);
			break;
		case 2:
			request->name = request;
			break;
		case 3:
			request->name = (uint8_t *)request + 1U;
			break;
		case 4:
			request->result_data = request;
			break;
		case 5:
			request->result_data = (uint8_t *)request + 1U;
			break;
		case 6:
			request->name = result;
			break;
		case 7:
			request->name = (uint8_t *)result + 1U;
			break;
		case 8:
			request->result_data = result;
			break;
		case 9:
			request->result_data = (uint8_t *)result + 1U;
			break;
		case 10:
			request->result_data = name;
			break;
		case 11:
			request->result_data = name + 1U;
			break;
		case 12:
			request->result_data = arena;
			break;
		case 13:
			request->result_data = &executor;
			break;
		case 14:
			request->result_data = media;
			break;
		case 15:
			request->result_data = (void *)(uintptr_t)(UINTPTR_MAX - 1U);
			break;
		case 16:
			result = (void *)(blob + 97U);
			break;
		case 17:
			request = (void *)(blob + 1U);
			break;
		default:
			assert(false);
		}
		memcpy(before, blob, sizeof(blob));
		assert(payload_mm_authvar_read_transaction(request, result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(!memcmp(blob, before, sizeof(blob)));
	}
	for (unsigned int partial = 0; partial < 2U; partial++) {
		uint8_t blob[256] __aligned(__BIGGEST_ALIGNMENT__);
		uint8_t before[sizeof(blob)];
		struct payload_mm_authvar_read_request *request = (void *)blob;
		struct payload_mm_authvar_read_result *result = (void *)(blob + 96U);
		uint8_t *name = blob + 192U;

		memset(blob, 0xa5, sizeof(blob));
		memcpy(name, read_name, sizeof(read_name));
		*request = (struct payload_mm_authvar_read_request) {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
			.name = name,
			.name_size = sizeof(read_name),
			.result_name = name + partial,
			.name_capacity = 16U,
		};
		memcpy(request->vendor_guid, caller_guid, sizeof(caller_guid));
		memcpy(before, blob, sizeof(blob));
		assert(payload_mm_authvar_read_transaction(request, result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(!memcmp(blob, before, sizeof(blob)));
	}
	assert(!begin_count && !end_count);
}

static uint64_t authorize_with_read(
	const struct payload_mm_authvar_policy_request *request,
	const struct payload_mm_authvar_policy_view *view,
	struct payload_mm_authvar_policy_mutation *mutation, void *data,
	size_t capacity)
{
	uint8_t output[8];
	uint8_t output_before[sizeof(output)];
	struct payload_mm_authvar_read_request nested = get_request(output,
		sizeof(output));
	struct payload_mm_authvar_read_result result;
	uint8_t result_before[sizeof(result)];

	(void)request;
	(void)view;
	assert(capacity >= sizeof(read_value));
	memset(output, 0xa5, sizeof(output));
	memset(&result, 0xa5, sizeof(result));
	memcpy(output_before, output, sizeof(output));
	memcpy(result_before, &result, sizeof(result));
	assert(payload_mm_authvar_read_transaction(&nested, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(!memcmp(output, output_before, sizeof(output)));
	assert(!memcmp(&result, result_before, sizeof(result)));
	callback_read_attempted = true;
	mutation->kind = PAYLOAD_MM_AUTHVAR_MUTATION_WRITE;
	mutation->attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	mutation->data_size = sizeof(read_value);
	memcpy(data, read_value, sizeof(read_value));
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

int main(int argc, char **argv)
{
	const uint32_t attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	struct payload_mm_authvar_read_result result;
	uint8_t output[16];

	assert(argc == 2);
	make_clean();
	seed_record(attributes);
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	{
		static const uint8_t zero_timestamp[16];
		static const uint8_t one = 1U;
		const uint32_t record_size = (PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
			sizeof(read_name) + sizeof(read_value) + 3U) & ~3U;
		const uint32_t nv_bs_time = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;

		view_used_offset = coordinator_append_record(
			PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + record_size,
			coordinator_vendor_guid, coordinator_vendor_name,
			sizeof(coordinator_vendor_name), nv_bs_time, zero_timestamp,
			&one, sizeof(one),
			PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO);
	}
#endif
	install_without_provider();
	memset(output, 0xa5, sizeof(output));
	memset(&result, 0xa5, sizeof(result));

	if (!strcmp(argv[1], "view-get-all")) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		static const uint8_t disabled;
		static const uint8_t enabled = 1U;
		static const uint8_t cert_db_empty[] = { 4, 0, 0, 0 };
		const uint32_t bs_rt = PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;

		expect_view_get(coordinator_global_guid, setup_mode_name,
			sizeof(setup_mode_name), &enabled, sizeof(enabled), bs_rt);
		expect_view_get(coordinator_global_guid, signature_support_name,
			sizeof(signature_support_name), signature_support,
			sizeof(signature_support), bs_rt);
		expect_view_get(coordinator_global_guid, secure_boot_name,
			sizeof(secure_boot_name), &disabled, sizeof(disabled), bs_rt);
		expect_view_get(cert_db_guid, cert_db_volatile_name,
			sizeof(cert_db_volatile_name), cert_db_empty,
			sizeof(cert_db_empty),
			bs_rt | PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED);
		expect_view_get(coordinator_global_guid, vendor_keys_name,
			sizeof(vendor_keys_name), &enabled, sizeof(enabled), bs_rt);
		assert(executor.sealed_volatile_modes_valid &&
			executor.sealed_volatile_modes ==
				(PAYLOAD_MM_AUTHVAR_MODE_SETUP |
				 PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
#else
		assert(false);
#endif
	} else if (!strcmp(argv[1], "view-next-all")) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		static const uint8_t *const names[] = {
			setup_mode_name, signature_support_name, secure_boot_name,
			cert_db_volatile_name, vendor_keys_name, read_name,
			coordinator_vendor_name,
		};
		static const size_t sizes[] = {
			sizeof(setup_mode_name), sizeof(signature_support_name),
			sizeof(secure_boot_name), sizeof(cert_db_volatile_name),
			sizeof(vendor_keys_name), sizeof(read_name),
			sizeof(coordinator_vendor_name),
		};
		static const uint8_t *const guids[] = {
			coordinator_global_guid, coordinator_global_guid,
			coordinator_global_guid, cert_db_guid,
			coordinator_global_guid, caller_guid, coordinator_vendor_guid,
		};
		uint8_t cursor[64] = { 0 };
		uint8_t cursor_guid[16] = { 0 };
		size_t cursor_size = 0U;

		for (size_t i = 0U; i < ARRAY_SIZE(names); i++) {
			uint8_t next_name[64];
			struct payload_mm_authvar_read_request request = {
				.operation = PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
				.name = cursor_size ? cursor : NULL,
				.name_size = cursor_size,
				.result_name = next_name,
				.name_capacity = sizeof(next_name),
			};

			memcpy(request.vendor_guid, cursor_guid,
				sizeof(request.vendor_guid));
			memset(next_name, 0xa5, sizeof(next_name));
			memset(&result, 0xa5, sizeof(result));
			assert(payload_mm_authvar_read_transaction(&request, &result) ==
				PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
			assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
			assert(result.required_name_size == sizes[i] &&
				!memcmp(result.vendor_guid, guids[i], 16U) &&
				!memcmp(next_name, names[i], sizes[i]));
			memcpy(cursor, next_name, sizes[i]);
			memcpy(cursor_guid, result.vendor_guid, sizeof(cursor_guid));
			cursor_size = sizes[i];
		}
		{
			uint8_t next_name[64];
			struct payload_mm_authvar_read_request request = {
				.operation = PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
				.name = cursor,
				.name_size = cursor_size,
				.result_name = next_name,
				.name_capacity = sizeof(next_name),
			};

			memcpy(request.vendor_guid, cursor_guid,
				sizeof(request.vendor_guid));
			memset(next_name, 0xa5, sizeof(next_name));
			memset(&result, 0xa5, sizeof(result));
			assert(payload_mm_authvar_read_transaction(&request, &result) ==
				PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
			assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
			assert(zero_buffer(next_name, sizeof(next_name)));
			assert_empty_metadata(&result);
		}
#else
		assert(false);
#endif
	} else if (!strcmp(argv[1], "view-small")) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		struct payload_mm_authvar_read_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
			.name = signature_support_name,
			.name_size = sizeof(signature_support_name),
			.result_data = output,
			.data_capacity = sizeof(output),
		};

		memcpy(request.vendor_guid, coordinator_global_guid,
			sizeof(request.vendor_guid));
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
		assert(result.required_data_size == sizeof(signature_support) &&
			result.attributes == (PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
				PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS));
		assert(zero_buffer(output, sizeof(output)));
		request = (struct payload_mm_authvar_read_request) {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
			.result_name = output,
			.name_capacity = sizeof(output),
		};
		memset(output, 0xa5, sizeof(output));
		memset(&result, 0xa5, sizeof(result));
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
		assert(result.required_name_size == sizeof(setup_mode_name) &&
			zero_buffer(output, sizeof(output)) &&
			zero_buffer(result.vendor_guid, sizeof(result.vendor_guid)));
#else
		assert(false);
#endif
	} else if (!strcmp(argv[1], "view-reconcile-statuses")) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		static const uint8_t missing_name[] = { 'Z', 0, 0, 0 };
		const uint8_t modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
			PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;

		for (unsigned int test = 0U; test < 6U; test++) {
			struct payload_mm_authvar_read_request request = { 0 };
			uint64_t expected_status;

			arm_view_reconcile(modes);
			memset(output, 0xa5, sizeof(output));
			memset(&result, 0xa5, sizeof(result));
			switch (test) {
			case 0U:
				request = get_request(output, sizeof(output));
				request.name = missing_name;
				expected_status = PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
				break;
			case 1U:
				request = (struct payload_mm_authvar_read_request) {
					.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
					.name = signature_support_name,
					.name_size = sizeof(signature_support_name),
					.result_data = output,
					.data_capacity = sizeof(output),
				};
				memcpy(request.vendor_guid, coordinator_global_guid, 16U);
				expected_status =
					PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL;
				break;
			case 2U:
				request = (struct payload_mm_authvar_read_request) {
					.operation = PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
					.name = missing_name,
					.name_size = sizeof(missing_name),
					.result_name = output,
					.name_capacity = sizeof(output),
				};
				memcpy(request.vendor_guid, caller_guid, 16U);
				expected_status =
					PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
				break;
			case 3U:
				request.operation = PAYLOAD_MM_AUTHVAR_SERVICE_QUERY;
				request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE;
				expected_status =
					PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
				break;
			case 4U:
				request.operation = PAYLOAD_MM_AUTHVAR_SERVICE_QUERY;
				request.attributes =
					PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
				expected_status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
				break;
			case 5U:
				media[FV_HEADER_SIZE + view_used_offset] = 0xaaU;
				request.operation = PAYLOAD_MM_AUTHVAR_SERVICE_QUERY;
				request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
					PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
				expected_status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
				break;
			default:
				abort();
			}
			assert(payload_mm_authvar_read_transaction(&request, &result) ==
				expected_status);
			assert_complete(&result, expected_status);
			assert(!executor.modes_need_reconcile &&
				!executor.sealed_modes_need_reconcile && !poisoned);
		}
#else
		assert(false);
#endif
	} else if (!strcmp(argv[1], "view-reconcile-end-failure")) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		static const uint8_t missing_name[] = { 'Z', 0, 0, 0 };
		const uint8_t modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
			PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
		struct payload_mm_authvar_read_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
			.name = setup_mode_name,
			.name_size = sizeof(setup_mode_name),
			.result_data = output,
			.data_capacity = sizeof(output),
		};

		memcpy(request.vendor_guid, coordinator_global_guid, 16U);
		arm_view_reconcile(modes);
		fail_end = true;
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(filled_with(output, 0xa5, sizeof(output)) &&
			executor.modes_need_reconcile &&
			executor.sealed_modes_need_reconcile &&
			executor.sealed_volatile_modes == modes);
		fail_end = false;
		request.name = missing_name;
		request.name_size = sizeof(missing_name);
		memset(&result, 0xa5, sizeof(result));
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
		assert(!executor.modes_need_reconcile &&
			!executor.sealed_modes_need_reconcile);
#else
		assert(false);
#endif
	} else if (!strcmp(argv[1], "view-boot-drift")) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		static const uint8_t enabled = 1U;
		const uint32_t vendor_offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE +
			((PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + sizeof(read_name) +
			  sizeof(read_value) + 3U) & ~3U);
		struct payload_mm_authvar_read_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
			.name = setup_mode_name,
			.name_size = sizeof(setup_mode_name),
			.result_data = output,
			.data_capacity = sizeof(output),
		};

		expect_view_get(coordinator_global_guid, setup_mode_name,
			sizeof(setup_mode_name), &enabled, sizeof(enabled),
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS);
		media[FV_HEADER_SIZE + vendor_offset +
			((PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
			  sizeof(coordinator_vendor_name) + 3U) & ~3U)] = 0U;
		memcpy(request.vendor_guid, coordinator_global_guid, 16U);
		memset(output, 0xa5, sizeof(output));
		memset(&result, 0xa5, sizeof(result));
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && filled_with(output, 0xa5, sizeof(output)));
#else
		assert(false);
#endif
	} else if (!strcmp(argv[1], "view-runtime-unsealed")) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		struct payload_mm_authvar_read_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
			.name = setup_mode_name,
			.name_size = sizeof(setup_mode_name),
			.result_data = output,
			.data_capacity = sizeof(output),
		};

		executor.at_runtime = true;
		executor.sealed_at_runtime = true;
		memcpy(request.vendor_guid, coordinator_global_guid, 16U);
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && filled_with(output, 0xa5, sizeof(output)));
#else
		assert(false);
#endif
	} else if (!strcmp(argv[1], "view-runtime-reconcile")) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		static const uint8_t zero_timestamp[16];
		static const uint8_t one = 1U;
		static const uint8_t pk_data = 0x5aU;
		static const uint8_t enabled = 1U;
		const uint32_t nv_bs = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
		uint32_t pk_offset = view_used_offset;
		uint32_t enable_offset;

		view_used_offset = coordinator_append_record(view_used_offset,
			coordinator_global_guid, coordinator_pk_name,
			sizeof(coordinator_pk_name), nv_bs |
				PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
				PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
			zero_timestamp, &pk_data, sizeof(pk_data),
			PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO);
		enable_offset = view_used_offset;
		view_used_offset = coordinator_append_record(view_used_offset,
			coordinator_enable_guid, coordinator_enable_name,
			sizeof(coordinator_enable_name), nv_bs, zero_timestamp,
			&one, sizeof(one),
			PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED);
		expect_view_get(coordinator_global_guid, secure_boot_name,
			sizeof(secure_boot_name), &enabled, sizeof(enabled),
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS);
		executor.at_runtime = true;
		executor.sealed_at_runtime = true;
		media[FV_HEADER_SIZE + pk_offset + 2U] =
			PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED;
		media[FV_HEADER_SIZE + enable_offset +
			((PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
			  sizeof(coordinator_enable_name) + 3U) & ~3U)] = 0U;
		{
			struct payload_mm_authvar_store_entry entries[64];
			struct payload_mm_authvar_store_index index = {
				.entries = entries,
				.entry_capacity = ARRAY_SIZE(entries),
			};
			const struct payload_mm_authvar_store_limits limits = {
				.maximum_store_size = STORE_SIZE,
				.maximum_name_size = 128U,
				.maximum_data_size = 2048U,
				.maximum_records = ARRAY_SIZE(entries),
			};
			u8 modes;

			assert(payload_mm_authvar_store_scan(&index,
				media + FV_HEADER_SIZE, STORE_SIZE, &limits) == CB_SUCCESS);
			assert(payload_mm_authvar_coordinator_reconcile_modes(&index, true,
				PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
				PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS, &modes));
			assert(modes == (PAYLOAD_MM_AUTHVAR_MODE_SETUP |
				PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
				PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
		}
		arm_view_reconcile(PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
			PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS);
		expect_view_get(coordinator_global_guid, setup_mode_name,
			sizeof(setup_mode_name), &enabled, sizeof(enabled),
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS);
		assert(executor.sealed_volatile_modes ==
			(PAYLOAD_MM_AUTHVAR_MODE_SETUP |
			 PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
			 PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS) &&
			!executor.sealed_modes_need_reconcile);
		expect_view_get(coordinator_global_guid, secure_boot_name,
			sizeof(secure_boot_name), &enabled, sizeof(enabled),
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS);
#else
		assert(false);
#endif
	} else if (!strcmp(argv[1], "view-first-end-failure") ||
		   !strcmp(argv[1], "view-next-end-failure")) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		bool next = !strcmp(argv[1], "view-next-end-failure");
		uint8_t next_output[32];
		uint8_t *caller_output = next ? next_output : output;
		size_t caller_capacity = next ? sizeof(next_output) : sizeof(output);
		struct payload_mm_authvar_read_request request = {
			.operation = next ? PAYLOAD_MM_AUTHVAR_SERVICE_NEXT :
				PAYLOAD_MM_AUTHVAR_SERVICE_GET,
			.name = next ? NULL : setup_mode_name,
			.name_size = next ? 0U : sizeof(setup_mode_name),
			.result_name = next ? caller_output : NULL,
			.name_capacity = next ? caller_capacity : 0U,
			.result_data = next ? NULL : caller_output,
			.data_capacity = next ? 0U : caller_capacity,
		};

		if (!next)
			memcpy(request.vendor_guid, coordinator_global_guid, 16U);
		memset(caller_output, 0xa5, caller_capacity);
		fail_end = true;
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(filled_with(caller_output, 0xa5, caller_capacity) &&
			executor.sealed_volatile_modes_valid &&
			executor.sealed_volatile_modes ==
				(PAYLOAD_MM_AUTHVAR_MODE_SETUP |
				 PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS) &&
			!executor.sealed_modes_need_reconcile);
#else
		assert(false);
#endif
	} else if (!strncmp(argv[1], "view-collision-", 15U)) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		static const uint8_t zero_timestamp[16];
		static const uint8_t one = 1U;
		struct payload_mm_authvar_read_request request = { 0 };

		(void)coordinator_append_record(view_used_offset,
			coordinator_global_guid, setup_mode_name,
			sizeof(setup_mode_name),
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
				PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
			zero_timestamp, &one, sizeof(one),
			PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED);
		if (!strcmp(argv[1], "view-collision-get")) {
			request = (struct payload_mm_authvar_read_request) {
				.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
				.name = setup_mode_name,
				.name_size = sizeof(setup_mode_name),
				.result_data = output,
				.data_capacity = sizeof(output),
			};
			memcpy(request.vendor_guid, coordinator_global_guid, 16U);
		} else if (!strcmp(argv[1], "view-collision-next")) {
			request = (struct payload_mm_authvar_read_request) {
				.operation = PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
				.result_name = output,
				.name_capacity = sizeof(output),
			};
		} else {
			assert(!strcmp(argv[1], "view-collision-query"));
			request.operation = PAYLOAD_MM_AUTHVAR_SERVICE_QUERY;
			request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
				PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
		}
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && filled_with(output, 0xa5, sizeof(output)));
		assert_empty_metadata(&result);
#else
		assert(false);
#endif
	} else if (!strcmp(argv[1], "view-get-malformed") ||
		   !strcmp(argv[1], "view-get-device")) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		static const uint8_t malformed_name[] = {
			'B', 0, 0, 0, 'X', 0, 0, 0,
		};
		bool device = !strcmp(argv[1], "view-get-device");
		struct payload_mm_authvar_read_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
			.name = device ? setup_mode_name : malformed_name,
			.name_size = device ? sizeof(setup_mode_name) :
				sizeof(malformed_name),
			.result_data = output,
			.data_capacity = sizeof(output),
		};
		uint64_t expected_status = device ?
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR :
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;

		memcpy(request.vendor_guid, coordinator_global_guid, 16U);
		arm_view_reconcile(PAYLOAD_MM_AUTHVAR_MODE_SETUP |
			PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS);
		force_view_get_device_error = device;
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			expected_status);
		assert_complete(&result, expected_status);
		assert(!poisoned && zero_buffer(output, sizeof(output)) &&
			!executor.sealed_modes_need_reconcile);
#else
		assert(false);
#endif
	} else if (!strcmp(argv[1], "view-query-out-of-range")) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		struct payload_mm_authvar_read_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_QUERY,
			.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
				PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS,
		};

		arm_view_reconcile(PAYLOAD_MM_AUTHVAR_MODE_SETUP |
			PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS);
		force_view_query_out_of_range = true;
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && executor.modes_need_reconcile &&
			executor.sealed_modes_need_reconcile);
		assert_empty_metadata(&result);
#else
		assert(false);
#endif
	} else if (!strcmp(argv[1], "get") || !strcmp(argv[1], "end-failure")) {
		struct payload_mm_authvar_read_request request =
			get_request(output, sizeof(output));

		fail_end = !strcmp(argv[1], "end-failure");
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			(fail_end ? PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR :
			 PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS));
		assert_complete(&result, fail_end ? PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR :
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		if (fail_end) {
			for (size_t i = 0; i < sizeof(output); i++)
				assert(output[i] == 0xa5);
			assert(!result.required_data_size && !result.attributes);
			assert(!cache_bound);
		} else {
			assert(!memcmp(output, read_value, sizeof(read_value)));
			assert(result.required_data_size == sizeof(read_value));
			assert(result.attributes == attributes);
			for (size_t i = sizeof(read_value); i < sizeof(output); i++)
				assert(!output[i]);
		}
	} else if (!strcmp(argv[1], "get-small")) {
		struct payload_mm_authvar_read_request request = get_request(output, 2U);

		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
		assert(result.required_data_size == sizeof(read_value));
		assert(result.attributes == attributes && !output[0] && !output[1]);
	} else if (!strcmp(argv[1], "next")) {
		static const uint8_t zero_guid[16];
		struct payload_mm_authvar_read_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
			.result_name = output,
			.name_capacity = sizeof(output),
		};

		memcpy(request.vendor_guid, zero_guid, sizeof(zero_guid));
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(result.required_name_size == sizeof(read_name));
		assert(!memcmp(result.vendor_guid, caller_guid, sizeof(caller_guid)));
		assert(!memcmp(output, read_name, sizeof(read_name)));
	} else if (!strcmp(argv[1], "next-small")) {
		struct payload_mm_authvar_read_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
			.result_name = output,
			.name_capacity = 2U,
		};

		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
		assert(result.required_name_size == sizeof(read_name));
		assert(!output[0] && !output[1]);
		assert(zero_buffer(result.vendor_guid, sizeof(result.vendor_guid)));
	} else if (!strcmp(argv[1], "query")) {
		struct payload_mm_authvar_read_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_QUERY,
			.attributes = attributes | PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE,
		};

		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(result.maximum_storage == STORE_SIZE -
			PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE);
		assert(result.remaining_storage < result.maximum_storage);
		assert(result.maximum_variable > 0);
	} else if (!strcmp(argv[1], "query-unknown")) {
		struct payload_mm_authvar_read_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_QUERY,
			.attributes = 1U << 31,
		};

		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
		assert_empty_metadata(&result);
	} else if (!strcmp(argv[1], "not-found")) {
		uint8_t missing[] = { 'Z', 0, 0, 0 };
		struct payload_mm_authvar_read_request request =
			get_request(output, sizeof(output));

		request.name = missing;
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
		assert_empty_metadata(&result);
		assert(zero_buffer(output, sizeof(output)));
	} else if (!strcmp(argv[1], "invalid")) {
		static const uint8_t missing[] = { 'Z', 0, 0, 0 };
		struct payload_mm_authvar_read_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
			.name = missing,
			.name_size = sizeof(missing),
			.result_name = output,
			.name_capacity = sizeof(output),
		};

		memcpy(request.vendor_guid, caller_guid, sizeof(caller_guid));
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert_empty_metadata(&result);
		assert(zero_buffer(output, sizeof(output)));
	} else if (!strcmp(argv[1], "runtime")) {
		uint8_t *record = media + FV_HEADER_SIZE +
			PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
		struct payload_mm_authvar_read_request request =
			get_request(output, sizeof(output));

		put32(record + 4U, PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS);
		executor.at_runtime = true;
		executor.sealed_at_runtime = true;
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
		assert(zero_buffer(output, sizeof(output)));
	} else if (!strcmp(argv[1], "dirty-tail")) {
		const size_t record_size = PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
			sizeof(read_name) + sizeof(read_value);
		struct payload_mm_authvar_read_request request =
			get_request(output, sizeof(output));

		media[FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE +
			record_size] = 0xaa;
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(!memcmp(output, read_value, sizeof(read_value)));
		request = (struct payload_mm_authvar_read_request) {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_QUERY,
			.attributes = attributes,
		};
		memset(output, 0xa5, sizeof(output));
		memset(&result, 0xa5, sizeof(result));
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert_empty_metadata(&result);
	} else if (!strcmp(argv[1], "next-dirty-tail")) {
		const size_t record_size = PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
			sizeof(read_name) + sizeof(read_value);
		struct payload_mm_authvar_read_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
			.result_name = output,
			.name_capacity = sizeof(output),
		};

		media[FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE +
			record_size] = 0xaa;
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(!memcmp(output, read_name, sizeof(read_name)));
	} else if (!strcmp(argv[1], "begin-failure") ||
		   !strcmp(argv[1], "read-failure")) {
		struct payload_mm_authvar_read_request request =
			get_request(output, sizeof(output));
		bool begin_failure = !strcmp(argv[1], "begin-failure");

		fail_operation = begin_failure ? 1U : 2U;
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
		assert_empty_metadata(&result);
		assert(filled_with(output, begin_failure ? 0xa5 : 0,
			sizeof(output)));
		assert(end_count == (begin_failure ? 0U : 1U));
		assert(!cache_bound);
	} else if (!strcmp(argv[1], "publication-order")) {
		struct payload_mm_authvar_read_request request =
			get_request(output, sizeof(output));

		observed_result = &result;
		observed_output = output;
		observed_output_size = sizeof(output);
		observe_end = true;
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	} else if (!strcmp(argv[1], "publication-gate")) {
		struct payload_mm_authvar_read_request request =
			get_request(output, sizeof(output));
		pthread_t watcher;

		observe_gate_release = true;
		assert(!pthread_create(&watcher, NULL, watch_gate_release, &result));
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(!pthread_join(watcher, NULL));
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	} else if (!strcmp(argv[1], "reentry")) {
		struct payload_mm_authvar_read_request request =
			get_request(output, sizeof(output));

		nested_read_on_begin = true;
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(nested_read_status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(begin_count == 1U && end_count == 1U);
	} else if (!strcmp(argv[1], "provider-reentry")) {
		struct payload_mm_authvar_read_request request =
			get_request(output, sizeof(output));
		uint8_t before[sizeof(result)];

		memcpy(before, &result, sizeof(before));
		__atomic_store_n(&executor.provider_active, 1, __ATOMIC_RELEASE);
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(!memcmp(before, &result, sizeof(result)));
		assert(filled_with(output, 0xa5, sizeof(output)));
		assert(!begin_count && !end_count);
	} else if (!strcmp(argv[1], "provider-callback")) {
		struct payload_mm_authvar_policy_provider provider = {
			.revision = PAYLOAD_MM_AUTHVAR_POLICY_REVISION,
			.size = sizeof(provider),
			.authorize = authorize_with_read,
		};
		struct payload_mm_authvar_policy_request request = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
			.attributes = attributes,
			.name = read_name,
			.name_size = sizeof(read_name),
			.data = read_value,
			.data_size = sizeof(read_value),
		};
		struct payload_mm_authvar_policy_result policy_result;

		memcpy(request.vendor_guid, caller_guid, sizeof(caller_guid));
		assert(payload_mm_authvar_policy_install(&provider) == CB_SUCCESS);
		assert(payload_mm_authvar_policy_transaction(&request, &policy_result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(callback_read_attempted && poisoned && end_count == 1U);
		assert(policy_result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	} else if (!strcmp(argv[1], "lifecycle")) {
		struct payload_mm_authvar_policy_request phase = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT,
		};
		struct payload_mm_authvar_policy_result phase_result;
		struct payload_mm_authvar_read_request request =
			get_request(output, sizeof(output));

		assert(test_policy_install() == CB_SUCCESS);
		assert(payload_mm_authvar_policy_transaction(&phase, &phase_result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(executor.ready_to_boot && !executor.at_runtime);
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(executor.ready_to_boot && !executor.at_runtime);
		phase.operation = PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME;
		assert(payload_mm_authvar_policy_transaction(&phase, &phase_result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(executor.ready_to_boot && executor.at_runtime);
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(executor.ready_to_boot && executor.at_runtime);
	} else if (!strcmp(argv[1], "recovery-abort") ||
		   !strcmp(argv[1], "recovery-replay") ||
		   !strcmp(argv[1], "recovery-complete") ||
		   !strcmp(argv[1], "recovery-restore")) {
		struct payload_mm_authvar_read_request request =
			get_request(output, sizeof(output));

		if (!strcmp(argv[1], "recovery-abort")) {
			make_write(PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
			memcpy(spare(), media, BLOCK_SIZE);
		} else if (!strcmp(argv[1], "recovery-replay")) {
			make_write(PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
			memcpy(spare(), media, BLOCK_SIZE);
		} else if (!strcmp(argv[1], "recovery-complete")) {
			make_write(PAYLOAD_MM_AUTHVAR_FTW_RECORD_DESTINATION_COMPLETE,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
		} else {
			make_workspace(spare(), PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID);
			memset(working(), 0xff, BLOCK_SIZE);
		}
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(erase_count && !memcmp(output, read_value, sizeof(read_value)));
	} else if (!strcmp(argv[1], "freshness")) {
		struct payload_mm_authvar_read_request request =
			get_request(output, sizeof(output));
		uint8_t *value = media + FV_HEADER_SIZE +
			PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE +
			PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + sizeof(read_name);

		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		value[0] = 0;
		memset(output, 0xa5, sizeof(output));
		memset(&result, 0xa5, sizeof(result));
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(!output[0] && output[1] == read_value[1]);
		assert(begin_count == 2U && end_count == 2U && cache_bound);
	} else if (!strcmp(argv[1], "end-not-found") ||
		   !strcmp(argv[1], "end-bts")) {
		uint8_t missing[] = { 'Z', 0, 0, 0 };
		struct payload_mm_authvar_read_request request = get_request(output,
			!strcmp(argv[1], "end-bts") ? 2U : sizeof(output));

		if (!strcmp(argv[1], "end-not-found"))
			request.name = missing;
		fail_end = true;
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert_empty_metadata(&result);
		assert(filled_with(output, 0xa5, sizeof(output)));
		assert(!cache_bound);
	} else if (!strcmp(argv[1], "alias")) {
		struct payload_mm_authvar_read_request request =
			get_request(&result, sizeof(read_value));
		uint8_t before[sizeof(result)];

		memcpy(before, &result, sizeof(before));
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(!memcmp(before, &result, sizeof(result)));
		assert(!begin_count && !end_count);
	} else if (!strcmp(argv[1], "alias-matrix")) {
		alias_matrix();
	} else if (!strcmp(argv[1], "unsupported")) {
		struct payload_mm_authvar_read_request request = get_request(output,
			sizeof(output));

		request.operation = UINT32_MAX;
		assert(payload_mm_authvar_read_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
		assert_complete(&result, PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
		for (size_t i = 0; i < sizeof(output); i++)
			assert(output[i] == 0xa5);
	} else {
		assert(false);
	}
	if (strcmp(argv[1], "provider-callback") && strcmp(argv[1], "lifecycle"))
		assert(!executor.sealed.provider.authorize);
	return 0;
}
