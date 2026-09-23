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
	install_without_provider();
	memset(output, 0xa5, sizeof(output));
	memset(&result, 0xa5, sizeof(result));

	if (!strcmp(argv[1], "get") || !strcmp(argv[1], "end-failure")) {
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
