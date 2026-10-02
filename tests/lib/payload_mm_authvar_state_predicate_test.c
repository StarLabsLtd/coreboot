/* SPDX-License-Identifier: GPL-2.0-only */

/* The existing model supplies media/crypto callbacks, not a native SMM proof. */
#define main legacy_executor_main
#define payload_mm_authvar_media_end predicate_model_media_end
#include "payload_mm_authvar_executor_test.c"
#undef payload_mm_authvar_media_end
#undef main

enum payload_mm_authvar_media_result payload_mm_authvar_media_end(uint64_t generation,
	uint64_t token);
#ifndef STATE_PREDICATE_EXECUTOR_SOURCE
#define STATE_PREDICATE_EXECUTOR_SOURCE "../../src/lib/payload_mm_authvar_executor.c"
#endif
#include STATE_PREDICATE_EXECUTOR_SOURCE

static bool mutate_pending_data;
static bool mutate_pending_name;

enum payload_mm_authvar_media_result payload_mm_authvar_media_end(uint64_t generation,
	uint64_t token)
{
	enum payload_mm_authvar_media_result result = predicate_model_media_end(generation, token);

	if (mutate_pending_data) {
		((u8 *)session()->request.data)[0] ^= 1;
		mutate_pending_data = false;
	}
	if (mutate_pending_name) {
		((u8 *)session()->request.name)[0] ^= 1;
		mutate_pending_name = false;
	}
	return result;
}

static const uint8_t predicate_guid[16] = { 0x52, 0x77, 0x19, 0x43 };
static const uint8_t predicate_name[] = { 'P', 0, 'r', 0, 'e', 0, 'd', 0, 0, 0 };
static const uint8_t target_guid[16] = { 0x59, 0x86, 0x12, 0x47 };
static const uint8_t target_name[] = { 'T', 0, 'a', 0, 'r', 0, 'g', 0, 'e', 0, 't', 0, 0, 0 };

static uint64_t register_predicate(const uint8_t guid[16], const void *name, size_t name_size,
	uint8_t target_identity)
{
	uint8_t entry[512];
	size_t size = 62 + name_size;

	variable_policy_fixture(entry, target_guid, 3, 255);
	entry[8] = target_identity;
	entry[4] = (uint8_t)size;
	entry[5] = (uint8_t)(size >> 8);
	entry[6] = entry[4];
	entry[7] = entry[5];
	memcpy(entry + 44, guid, 16);
	entry[60] = 1;
	entry[61] = 0;
	memcpy(entry + 62, name, name_size);
	uint64_t status = payload_mm_authvar_variable_policy_register(entry, size);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		dprintf(2, "predicate registration status: %llx\n", (unsigned long long)status);
	return status;
}

static uint64_t write_value(const uint8_t guid[16], const void *name, size_t name_size,
	uint32_t attributes, const void *data, size_t data_size)
{
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = attributes, .name = name, .name_size = name_size,
		.data = data, .data_size = data_size,
	};
	struct payload_mm_authvar_policy_result result;

	memcpy(request.vendor_guid, guid, 16);
	return payload_mm_authvar_policy_transaction(&request, &result);
}

static uint64_t read_value(uint32_t operation, const uint8_t guid[16],
	const void *name, size_t name_size, void *data, size_t capacity,
	struct payload_mm_authvar_read_result *result)
{
	struct payload_mm_authvar_read_request request = {
		.operation = operation, .name = name, .name_size = name_size,
		.result_data = data, .data_capacity = capacity,
	};

	memcpy(request.vendor_guid, guid, 16);
	return payload_mm_authvar_read_transaction(&request, result);
}

static uint64_t query_pool(struct payload_mm_authvar_read_result *result)
{
	struct payload_mm_authvar_read_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_QUERY, .attributes = 6,
	};

	return payload_mm_authvar_read_transaction(&request, result);
}

static uint64_t next_value(const void *name, size_t size,
	struct payload_mm_authvar_read_result *result)
{
	uint8_t output[128];
	struct payload_mm_authvar_read_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
		.name = name, .name_size = size, .result_name = output,
		.name_capacity = sizeof(output),
	};

	memcpy(request.vendor_guid, predicate_guid, 16);
	return payload_mm_authvar_read_transaction(&request, result);
}

int main(int argc, char **argv)
{
	struct payload_mm_authvar_read_result result;
	uint8_t data[2048];
	uint32_t classification[4];
	unsigned int programs, erases;

	assert(argc == 2);
	make_clean();
	make_candidate_source();
	coordinator_install_executor();
	assert(payload_mm_authvar_executor_state_predicate_pinned());
	if (!strcmp(argv[1], "reserved")) {
		const struct {
			const uint8_t *guid;
			const uint8_t *name;
			size_t size;
		} reserved[] = {
			{ coordinator_custom_guid, coordinator_custom_name, sizeof(coordinator_custom_name) },
			{ coordinator_enable_guid, coordinator_enable_name, sizeof(coordinator_enable_name) },
			{ coordinator_global_guid, coordinator_pk_name, sizeof(coordinator_pk_name) },
			{ coordinator_global_guid, coordinator_kek_name, sizeof(coordinator_kek_name) },
			{ coordinator_vendor_guid, coordinator_vendor_name, sizeof(coordinator_vendor_name) },
		};

		for (size_t index = 0; index < ARRAY_SIZE(reserved); index++) {
			callback_trace_count = 0;
			assert(register_predicate(reserved[index].guid, reserved[index].name,
				reserved[index].size, (uint8_t)(index + 1)) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
			assert(read_value(PAYLOAD_MM_AUTHVAR_SERVICE_KEY_CLASSIFY, reserved[index].guid,
				reserved[index].name, reserved[index].size, classification,
				sizeof(classification), &result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
			assert(classification[1] != PAYLOAD_MM_AUTHVAR_KEY_VOLATILE_PINNED);
			assert(write_value(reserved[index].guid, reserved[index].name,
				reserved[index].size, 2, &(uint8_t) { 1 }, 1) != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		}
		return 0;
	}
	if (!strcmp(argv[1], "persistent-live") || !strcmp(argv[1], "persistent-deleted")) {
		assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 7,
			&(uint8_t) { 0x51 }, 1) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		if (!strcmp(argv[1], "persistent-deleted"))
			assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 0,
				&(uint8_t) { 1 }, 1) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(register_predicate(predicate_guid, predicate_name, sizeof(predicate_name),
			target_guid[0]) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(read_value(PAYLOAD_MM_AUTHVAR_SERVICE_KEY_CLASSIFY, predicate_guid,
			predicate_name, sizeof(predicate_name), classification, sizeof(classification),
			&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(classification[1] == PAYLOAD_MM_AUTHVAR_KEY_PERSISTENT);
		if (!strcmp(argv[1], "persistent-live"))
			assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 0,
				&(uint8_t) { 1 }, 1) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 6,
			&(uint8_t) { 1 }, 1) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 7,
			&(uint8_t) { 0x42 }, 1) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(read_value(PAYLOAD_MM_AUTHVAR_SERVICE_GET, predicate_guid, predicate_name,
			sizeof(predicate_name), data, sizeof(data), &result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(data[0] == 0x42 && result.attributes == 7);
		return 0;
	}
	assert(register_predicate(predicate_guid, predicate_name, sizeof(predicate_name),
		target_guid[0]) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	if (!strcmp(argv[1], "pool")) {
		uint8_t name[] = { 'P', 0, 'A', 0, 0, 0 };

		memset(data, 0x35, sizeof(data));
		for (unsigned int index = 0; index < 32; index++) {
			callback_trace_count = 0;
			name[2] = (uint8_t)('A' + index);
			assert(register_predicate(predicate_guid, name, sizeof(name),
				(uint8_t)(index + 1)) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
			assert(write_value(predicate_guid, name, sizeof(name), 6, data,
				sizeof(data)) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		}
		callback_trace_count = 0;
		assert(query_pool(&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(result.maximum_storage == 65536 && !result.remaining_storage &&
			!result.maximum_variable);
		assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 6,
			data, 1) == PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES);
		name[2] = 'A';
		assert(write_value(predicate_guid, name, sizeof(name), 0, data, 1) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(query_pool(&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(result.remaining_storage == sizeof(data) &&
			result.maximum_variable == sizeof(data));
		assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 6,
			data, sizeof(data)) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		name[2] = 'B';
		memset(data, 0, sizeof(data));
		assert(read_value(PAYLOAD_MM_AUTHVAR_SERVICE_GET, predicate_guid, name,
			sizeof(name), data, sizeof(data), &result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		for (size_t index = 0; index < sizeof(data); index++)
			assert(data[index] == 0x35);
		return 0;
	}
	if (!strcmp(argv[1], "next")) {
		struct payload_mm_authvar_policy_request phase = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME,
		};
		struct payload_mm_authvar_policy_result phase_result;

		assert(next_value(predicate_name, sizeof(predicate_name), &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(next_value(target_name, sizeof(target_name), &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 2,
			&(uint8_t) { 1 }, 1) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(next_value(predicate_name, sizeof(predicate_name), &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
		assert(payload_mm_authvar_policy_transaction(&phase, &phase_result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(next_value(predicate_name, sizeof(predicate_name), &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		return 0;
	}
	if (!strcmp(argv[1], "end-data") || !strcmp(argv[1], "end-name")) {
		static struct variable_policy_ledger before;

		memcpy(&before, &variable_policies, sizeof(before));
		mutate_pending_data = !strcmp(argv[1], "end-data");
		mutate_pending_name = !strcmp(argv[1], "end-name");
		assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 6,
			&(uint8_t) { 1 }, 1) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && !memcmp(&before, &variable_policies, sizeof(before)) &&
			!memcmp(&before, &sealed_variable_policies, sizeof(before)));
		return 0;
	}
	assert(read_value(PAYLOAD_MM_AUTHVAR_SERVICE_KEY_CLASSIFY, predicate_guid,
		predicate_name, sizeof(predicate_name), classification, sizeof(classification),
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(classification[1] == PAYLOAD_MM_AUTHVAR_KEY_VOLATILE_PINNED &&
		classification[2] == 0 && classification[3] == 0);
	programs = program_count;
	erases = erase_count;
	memset(data, 0x5a, sizeof(data));
	assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 6,
		data, sizeof(data)) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(program_count == programs && erase_count == erases);
	memset(data, 0, sizeof(data));
	assert(read_value(PAYLOAD_MM_AUTHVAR_SERVICE_GET, predicate_guid, predicate_name,
		sizeof(predicate_name), data, sizeof(data), &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(result.required_data_size == sizeof(data) && result.attributes == 6);
	for (size_t i = 0; i < sizeof(data); i++)
		assert(data[i] == 0x5a);
	assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 7,
		data, 1) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 2,
		data, 1) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 6 | 64,
		NULL, 0) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 6 | 64,
		data, 1) == PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES);
	assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 0,
		data, 3) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(read_value(PAYLOAD_MM_AUTHVAR_SERVICE_GET, predicate_guid, predicate_name,
		sizeof(predicate_name), data, sizeof(data), &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	assert(read_value(PAYLOAD_MM_AUTHVAR_SERVICE_KEY_CLASSIFY, predicate_guid,
		predicate_name, sizeof(predicate_name), classification, sizeof(classification),
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(classification[1] == PAYLOAD_MM_AUTHVAR_KEY_VOLATILE_PINNED &&
		classification[2] == 0 && classification[3] == 0);
	assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 7,
		data, 1) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 0,
		NULL, 0) == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 6,
		&(uint8_t) { 1 }, 1) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(write_value(target_guid, target_name, sizeof(target_name), 7,
		&(uint8_t) { 0x11 }, 1) == PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED);
	assert(write_value(predicate_guid, predicate_name, sizeof(predicate_name), 6,
		&(uint8_t) { 0 }, 1) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(write_value(target_guid, target_name, sizeof(target_name), 7,
		&(uint8_t) { 0x11 }, 1) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	return 0;
}
