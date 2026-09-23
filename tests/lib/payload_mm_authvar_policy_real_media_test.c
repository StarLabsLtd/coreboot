/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EXECUTOR_SOURCE_INCLUDE
#define EXECUTOR_SOURCE_INCLUDE "../../src/lib/payload_mm_authvar_executor.c"
#endif
#define main executor_fixture_main
#include "payload_mm_authvar_executor_test.c"
#undef main

static const char *raw_case;
static unsigned int authorize_count;
static uint8_t name[] = { 'R', 0, 0, 0 };
static uint8_t data[] = { 0x41 };

static uint64_t raw_authorize(
	const struct payload_mm_authvar_policy_request *request,
	const struct payload_mm_authvar_policy_view *view,
	struct payload_mm_authvar_policy_mutation *mutation,
	void *output, size_t capacity)
{
	uint64_t generation = 0;
	uint64_t token = 0;
	uint8_t byte = 0xfe;
	struct payload_mm_authvar_media_provider_scope forged = { 0 };

	authorize_count++;
	assert(view->index == &session()->index);
	assert(begin_count == 1 && end_count == 0);
	assert(test_policy_authorize(request, view, mutation, output, capacity) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	if (!strcmp(raw_case, "begin"))
		assert(payload_mm_authvar_media_begin(&generation, &token) ==
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	else if (!strcmp(raw_case, "program"))
		assert(payload_mm_authvar_media_program(session()->generation,
			session()->token, BLOCK_SIZE + 200U, &byte, 1) ==
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	else if (!strcmp(raw_case, "erase"))
		assert(payload_mm_authvar_media_erase(session()->generation,
			session()->token, 2U * BLOCK_SIZE, BLOCK_SIZE) ==
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	else if (!strcmp(raw_case, "end"))
		assert(payload_mm_authvar_media_end(session()->generation,
			session()->token) == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	else if (!strcmp(raw_case, "leave-zero") ||
		 !strcmp(raw_case, "leave-wrong") ||
		 !strcmp(raw_case, "leave-token")) {
		if (!strcmp(raw_case, "leave-wrong")) {
			forged.cookie = UINT64_MAX;
			forged.check = UINT64_MAX;
		} else if (!strcmp(raw_case, "leave-token")) {
			forged.cookie = session()->token;
			forged.check = session()->token;
		}
		assert(!payload_mm_authvar_media_provider_leave(&forged));
		assert(payload_mm_authvar_media_program(session()->generation,
			session()->token, BLOCK_SIZE + 200U, &byte, 1) ==
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	} else if (!strcmp(raw_case, "enter-nested")) {
		assert(!payload_mm_authvar_media_provider_enter(&forged));
		assert(payload_mm_authvar_media_program(session()->generation,
			session()->token, BLOCK_SIZE + 200U, &byte, 1) ==
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	} else if (!strcmp(raw_case, "transaction-nested")) {
		struct payload_mm_authvar_policy_request nested = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
			.attributes = request->attributes,
			.name = name,
			.name_size = sizeof(name),
			.data = data,
			.data_size = sizeof(data),
		};
		struct payload_mm_authvar_policy_result nested_result;

		memcpy(nested.vendor_guid, request->vendor_guid,
			sizeof(nested.vendor_guid));
		memset(&nested_result, 0xa5, sizeof(nested_result));
		assert(payload_mm_authvar_policy_transaction(&nested, &nested_result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		for (size_t i = 0; i < sizeof(nested_result); i++)
			assert(((const uint8_t *)&nested_result)[i] == 0xa5);
	} else
		assert(false);
	assert(begin_count == 1 && end_count == 0 && !program_count && !erase_count);
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

int main(int argc, char **argv)
{
	const struct payload_mm_authvar_executor_limits limits = {
		.maximum_store_size = REGION_SIZE,
		.maximum_name_size = 128,
		.maximum_data_size = 2048,
		.maximum_record_size = 8192,
		.maximum_records = 64,
	};
	const struct payload_mm_authvar_policy_provider provider = {
		.revision = PAYLOAD_MM_AUTHVAR_POLICY_REVISION,
		.size = sizeof(provider),
		.authorize = raw_authorize,
	};
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		.name = name,
		.name_size = sizeof(name),
		.data = data,
		.data_size = sizeof(data),
	};
	struct payload_mm_authvar_policy_result result;
	uint8_t before[REGION_SIZE];

	assert(argc == 2);
	raw_case = argv[1];
	real_media_prepare();
	make_clean();
	real_stack_install();
	assert(payload_mm_authvar_executor_install(arena, sizeof(arena), &limits) ==
		CB_SUCCESS);
	assert(payload_mm_authvar_policy_install(&provider) == CB_SUCCESS);
	memcpy(request.vendor_guid, caller_guid, sizeof(request.vendor_guid));
	memcpy(before, media, sizeof(before));
	memset(&result, 0xa5, sizeof(result));
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		!result.reserved);
	assert(authorize_count == 1 && begin_count == 1 && end_count == 1);
	assert(!program_count && !erase_count &&
		!memcmp(before, media, sizeof(before)));
	assert(payload_mm_authvar_media_provider_violated() ==
		(strcmp(raw_case, "transaction-nested") != 0));
	assert(!payload_mm_authvar_media_available());
	return 0;
}
