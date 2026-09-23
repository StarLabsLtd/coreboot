/* SPDX-License-Identifier: GPL-2.0-only */

#include <pthread.h>

#ifndef EXECUTOR_SOURCE_INCLUDE
#define EXECUTOR_SOURCE_INCLUDE "../../src/lib/payload_mm_authvar_executor.c"
#endif
#define main executor_fixture_main
#include "payload_mm_authvar_executor_test.c"
#undef main

static const char *policy_case;
static unsigned int authorize_count;
static unsigned int expected_entries;
static bool expected_runtime;
static bool expected_ready;
static bool expect_recovered;
static bool observe_gate_release;
static uint32_t gate_watcher_ready;
uint32_t gate_release_observed;
static uint8_t request_name[] = { 'P', 0, 0, 0 };
static uint8_t request_data[] = { 0x41, 0x42, 0x43, 0x44 };

static void *watch_gate_release(void *argument)
{
	struct payload_mm_authvar_policy_result *result = argument;
	uint32_t completion;

	while (__atomic_load_n(&executor.busy, __ATOMIC_ACQUIRE) != 1U)
		;
	__atomic_store_n(&gate_watcher_ready, 1U, __ATOMIC_RELEASE);
	while (__atomic_load_n(&executor.busy, __ATOMIC_ACQUIRE) != 0U)
		;
	completion = __atomic_load_n(&result->completion, __ATOMIC_ACQUIRE);
	__atomic_store_n(&gate_release_observed, 1U, __ATOMIC_RELEASE);
	assert(completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
	return NULL;
}

static struct payload_mm_authvar_policy_request make_request(void)
{
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		.name = request_name,
		.name_size = sizeof(request_name),
		.data = request_data,
		.data_size = sizeof(request_data),
	};

	memcpy(request.vendor_guid, caller_guid, 16);
	return request;
}

static uint64_t authorize(
	const struct payload_mm_authvar_policy_request *request,
	const struct payload_mm_authvar_policy_view *view,
	struct payload_mm_authvar_policy_mutation *mutation, void *data, size_t capacity)
{
	struct payload_mm_authvar_policy_request nested = make_request();
	struct payload_mm_authvar_policy_result nested_result;

	authorize_count++;
	if (observe_gate_release)
		while (!__atomic_load_n(&gate_watcher_ready, __ATOMIC_ACQUIRE))
			;
	assert(begin_count == end_count + 1U && read_count >= REGION_SIZE / BLOCK_SIZE);
	assert(owner_equal(session()));
	assert(view->index == &session()->index);
	assert(view->index->entry_count == expected_entries);
	assert(view->ready_to_boot == expected_ready && view->at_runtime == expected_runtime);
	if (expect_recovered) {
		assert(session()->recovery_count > 0);
		assert(session()->ftw.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		assert(erase_count > 0);
	}
	assert(request != &nested && request->name != request_name &&
		request->data != request_data);
	assert(!memcmp(request->name, request_name, sizeof(request_name)));
	assert(!memcmp(request->data, request_data, sizeof(request_data)));
	assert(test_policy_authorize(request, view, mutation, data, capacity) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	((uint8_t *)data)[0] = 0x99;
	if (!strcmp(policy_case, "reject"))
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	if (!strcmp(policy_case, "status-invalid-parameter"))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (!strcmp(policy_case, "status-unsupported"))
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	if (!strcmp(policy_case, "status-device-error"))
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (!strcmp(policy_case, "status-write-protected"))
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	if (!strcmp(policy_case, "status-out-of-resources"))
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	if (!strcmp(policy_case, "status-not-found"))
		return PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
	if (!strcmp(policy_case, "request-control"))
		((struct payload_mm_authvar_policy_request *)(uintptr_t)request)->attributes ^= 1U;
	else if (!strcmp(policy_case, "request-name"))
		((uint8_t *)(uintptr_t)request->name)[0] ^= 1U;
	else if (!strcmp(policy_case, "request-data"))
		((uint8_t *)(uintptr_t)request->data)[0] ^= 1U;
	else if (!strcmp(policy_case, "snapshot"))
		((uint8_t *)(uintptr_t)view->index->store)[0] ^= 1U;
	else if (!strcmp(policy_case, "index"))
		((struct payload_mm_authvar_store_index *)(uintptr_t)view->index)->entry_count++;
	else if (!strcmp(policy_case, "index-pointer"))
		((struct payload_mm_authvar_store_index *)(uintptr_t)view->index)->store = NULL;
	else if (!strcmp(policy_case, "entries"))
		view->index->entries[0].data_offset ^= 1U;
	else if (!strcmp(policy_case, "session"))
		session()->token++;
	else if (!strcmp(policy_case, "plan"))
		session()->write.step_count++;
	else if (!strcmp(policy_case, "provider-seal"))
		executor.policy.provider.authorize = NULL;
	else if (!strcmp(policy_case, "phase-seal"))
		executor.at_runtime = !executor.at_runtime;
	else if (!strcmp(policy_case, "invalid-kind"))
		mutation->kind = 0;
	else if (!strcmp(policy_case, "oversize"))
		mutation->data_size = (uint32_t)capacity + 1U;
	else if (!strcmp(policy_case, "invalid-delete")) {
		mutation->kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE;
		mutation->attributes = 0;
		mutation->data_size = 0;
		mutation->timestamp[0] = 1;
	} else if (!strcmp(policy_case, "delete-attributes")) {
		mutation->kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE;
		mutation->attributes = 1;
		mutation->data_size = 0;
	} else if (!strcmp(policy_case, "delete-data")) {
		mutation->kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE;
		mutation->attributes = 0;
		mutation->data_size = 1;
	} else if (!strcmp(policy_case, "zero-write")) {
		mutation->kind = PAYLOAD_MM_AUTHVAR_MUTATION_WRITE;
		mutation->attributes = 0;
		mutation->data_size = 0;
	} else if (!strcmp(policy_case, "invalid-status")) {
		return UINT64_MAX;
	} else if (!strcmp(policy_case, "recover-reentry")) {
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	} else if (!strcmp(policy_case, "transaction-reentry")) {
		memset(&nested_result, 0xa5, sizeof(nested_result));
		assert(payload_mm_authvar_policy_transaction(&nested, &nested_result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		for (size_t i = 0; i < sizeof(nested_result); i++)
			assert(((const uint8_t *)&nested_result)[i] == 0xa5);
	} else if (!strcmp(policy_case, "install-reentry")) {
		assert(test_policy_install() == CB_ERR);
	}
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static void install_executor(void)
{
	const struct payload_mm_authvar_executor_limits limits = {
		.maximum_store_size = REGION_SIZE,
		.maximum_name_size = 128,
		.maximum_data_size = 2048,
		.maximum_record_size = 8192,
		.maximum_records = 64,
	};

	assert(payload_mm_authvar_executor_install(arena, sizeof(arena), &limits) == CB_SUCCESS);
}

static bool all_zero_bytes(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;

	for (size_t i = 0; i < size; i++)
		if (bytes[i])
			return false;
	return true;
}

static uint64_t transact(const struct payload_mm_authvar_policy_request *request)
{
	struct payload_mm_authvar_policy_result result;
	uint64_t status;

	memset(&result, 0xa5, sizeof(result));
	status = payload_mm_authvar_policy_transaction(request, &result);
	assert(result.status == status &&
		result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE && !result.reserved);
	assert(all_zero_bytes(arena, executor.sealed.required_size));
	return status;
}

static void alias_tests(struct payload_mm_authvar_policy_request *request)
{
	struct payload_mm_authvar_policy_result result;
	struct payload_mm_authvar_policy_request saved = *request;
	uint8_t before[sizeof(result)];
	uint8_t executor_before[sizeof(executor)];

	memset(&result, 0xa5, sizeof(result));
	memcpy(before, &result, sizeof(result));
	memcpy(executor_before, &executor, sizeof(executor));
	if (!strcmp(policy_case, "alias-request-result")) {
		assert(payload_mm_authvar_policy_transaction(request,
			(struct payload_mm_authvar_policy_result *)request) ==
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(!memcmp(request, &saved, sizeof(saved)));
		return;
	}
	if (!strcmp(policy_case, "alias-name-data"))
		request->data = request->name;
	else if (!strcmp(policy_case, "alias-name-data-partial"))
		request->data = (const uint8_t *)request->name + 2U;
	else if (!strcmp(policy_case, "alias-data-result"))
		request->data = &result;
	else if (!strcmp(policy_case, "alias-name-result"))
		request->name = &result;
	else if (!strcmp(policy_case, "alias-result-arena")) {
		assert(payload_mm_authvar_policy_transaction(request,
			(struct payload_mm_authvar_policy_result *)arena) ==
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(all_zero_bytes(arena, executor.sealed.required_size));
		return;
	} else if (!strcmp(policy_case, "alias-result-executor")) {
		assert(payload_mm_authvar_policy_transaction(request,
			(struct payload_mm_authvar_policy_result *)&executor) ==
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(!memcmp(&executor, executor_before, sizeof(executor)));
		return;
	}
	else if (!strcmp(policy_case, "alias-arena"))
		request->data = arena;
	else if (!strcmp(policy_case, "alias-executor"))
		request->data = &executor;
	else if (!strcmp(policy_case, "alias-request-name"))
		request->name = request;
	else if (!strcmp(policy_case, "alias-request-data"))
		request->data = request;
	else if (!strcmp(policy_case, "overflow"))
		request->data = (void *)(uintptr_t)(UINTPTR_MAX - 1U);
	else
		assert(false);
	assert(payload_mm_authvar_policy_transaction(request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(!memcmp(before, &result, sizeof(result)));
}

int main(int argc, char **argv)
{
	struct payload_mm_authvar_policy_request request = make_request();
	struct payload_mm_authvar_policy_result gate_result;
	struct payload_mm_authvar_policy_provider provider = {
		.revision = PAYLOAD_MM_AUTHVAR_POLICY_REVISION,
		.size = sizeof(provider),
		.authorize = authorize,
	};
	uint8_t old_media[REGION_SIZE];
	uint64_t status;
	pthread_t watcher;

	assert(argc == 2);
	policy_case = argv[1];
	make_clean();
	if (!strcmp(policy_case, "install-before-executor")) {
		assert(payload_mm_authvar_policy_install(&provider) == CB_ERR);
		install_executor();
		assert(payload_mm_authvar_policy_install(&provider) == CB_ERR);
		return 0;
	}
	install_executor();
	if (!strcmp(policy_case, "no-provider")) {
		assert(transact(&request) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(!begin_count && !program_count && !erase_count);
		return 0;
	}
	if (!strcmp(policy_case, "bad-install")) {
		provider.authorize = NULL;
		assert(payload_mm_authvar_policy_install(&provider) == CB_ERR);
		assert(test_policy_install() == CB_ERR);
		return 0;
	}
	assert(payload_mm_authvar_policy_install(&provider) == CB_SUCCESS);
	assert(payload_mm_authvar_policy_install(&provider) == CB_ERR);
	memset(&provider, 0, sizeof(provider));
	if (!strcmp(policy_case, "preseal-complete") ||
	    !strcmp(policy_case, "preseal-alias") ||
	    !strcmp(policy_case, "preseal-private")) {
		struct payload_mm_authvar_policy_result result;
		struct payload_mm_authvar_policy_request saved = request;

		executor.policy.limits.maximum_data_size ^= 1U;
		memset(&result, 0xa5, sizeof(result));
		if (!strcmp(policy_case, "preseal-alias")) {
			assert(payload_mm_authvar_policy_transaction(&request,
				(struct payload_mm_authvar_policy_result *)&request) ==
				PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
			assert(!memcmp(&request, &saved, sizeof(request)));
		} else if (!strcmp(policy_case, "preseal-private")) {
			assert(payload_mm_authvar_policy_transaction(&request,
				(struct payload_mm_authvar_policy_result *)arena) ==
				PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
			assert(all_zero_bytes(arena, executor.sealed.required_size));
		} else {
			assert(payload_mm_authvar_policy_transaction(&request, &result) ==
				PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
			assert(result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
				result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
				!result.reserved);
		}
		assert(!begin_count && !authorize_count && !program_count && !erase_count);
		return 0;
	}
	if (!strncmp(policy_case, "alias-", 6U) || !strcmp(policy_case, "overflow")) {
		alias_tests(&request);
		assert(!begin_count && !authorize_count && !program_count && !erase_count);
		return 0;
	}
	if (!strcmp(policy_case, "unsupported")) {
		request.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET;
		assert(transact(&request) == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
		assert(!begin_count && !authorize_count);
		return 0;
	}
	if (!strcmp(policy_case, "bad-name")) {
		request_name[2] = 1;
		assert(transact(&request) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(!begin_count && !authorize_count);
		return 0;
	}
	if (!strcmp(policy_case, "ready-only") ||
	    !strcmp(policy_case, "runtime-first") ||
	    !strcmp(policy_case, "runtime-then-ready")) {
		struct payload_mm_authvar_policy_request phase = {
			.operation = !strcmp(policy_case, "ready-only") ?
				PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT :
				PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME,
		};

		assert(transact(&phase) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(executor.ready_to_boot &&
			executor.at_runtime == (strcmp(policy_case, "ready-only") != 0));
		if (!strcmp(policy_case, "runtime-then-ready")) {
			phase.operation = PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT;
			assert(transact(&phase) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
			assert(executor.ready_to_boot && executor.at_runtime);
		}
		assert(!authorize_count && begin_count == end_count);
		return 0;
	}
	if (!strcmp(policy_case, "lifecycle")) {
		struct payload_mm_authvar_policy_request phase = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT,
		};

		assert(transact(&phase) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		expected_ready = true;
		assert(executor.ready_to_boot && !executor.at_runtime && !authorize_count);
		phase.operation = PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME;
		assert(transact(&phase) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		expected_runtime = true;
		assert(executor.at_runtime && executor.ready_to_boot && !authorize_count);
		/* New variables at runtime are not supported by the existing writer. */
		status = transact(&request);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED);
		assert(authorize_count == 1U && begin_count == end_count);
		return 0;
	}
	if (!strcmp(policy_case, "session-binding")) {
		assert(transact(&request) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		expected_entries = 1;
		assert(transact(&request) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(authorize_count == 2 && begin_count == 2 && end_count == 2);
		return 0;
	}
	if (!strcmp(policy_case, "store-pressure")) {
		uint8_t pressure_data[512] = { 0 };

		memcpy(pressure_data, request_data, sizeof(request_data));
		request.data = pressure_data;
		request.data_size = sizeof(pressure_data);
		for (unsigned int attempt = 0; attempt < 20 && !erase_count; attempt++) {
			pressure_data[4] = (uint8_t)(attempt + 1U);
			assert(transact(&request) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
			expected_entries = 1;
		}
		assert(erase_count > 0 && program_count > 0);
		assert(authorize_count >= 2 && begin_count == end_count);
		return 0;
	}
	if (!strcmp(policy_case, "after-recovery")) {
		make_write(PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
		memcpy(spare(), media, BLOCK_SIZE);
		expect_recovered = true;
		assert(transact(&request) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(authorize_count == 1 && begin_count == 1 && end_count == 1);
		assert(erase_count > 0);
		return 0;
	}
	memcpy(old_media, media, sizeof(old_media));
	if (!strcmp(policy_case, "publication-gate")) {
		memset(&gate_result, 0xa5, sizeof(gate_result));
		observe_gate_release = true;
		assert(!pthread_create(&watcher, NULL, watch_gate_release, &gate_result));
		status = payload_mm_authvar_policy_transaction(&request, &gate_result);
		assert(!pthread_join(watcher, NULL));
		assert(gate_result.status == status &&
			gate_result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
			!gate_result.reserved);
		assert(all_zero_bytes(arena, executor.sealed.required_size));
	} else {
		if (!strcmp(policy_case, "end-failure"))
			fail_end = true;
		status = transact(&request);
	}
	assert(authorize_count == 1 && begin_count == 1 && end_count == 1);
	if (!strcmp(policy_case, "allow") || !strcmp(policy_case, "publication-gate")) {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(media[FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE +
			PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + sizeof(request_name)] == 0x99);
		assert(program_count && !poisoned);
	} else if (!strcmp(policy_case, "reject") ||
		   !strncmp(policy_case, "status-", 7U)) {
		uint64_t expected = PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;

		if (!strcmp(policy_case, "status-invalid-parameter"))
			expected = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		else if (!strcmp(policy_case, "status-unsupported"))
			expected = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
		else if (!strcmp(policy_case, "status-device-error"))
			expected = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		else if (!strcmp(policy_case, "status-write-protected"))
			expected = PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
		else if (!strcmp(policy_case, "status-out-of-resources"))
			expected = PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
		else if (!strcmp(policy_case, "status-not-found"))
			expected = PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
		assert(status == expected);
		assert(!memcmp(old_media, media, sizeof(media)));
		assert(!program_count && !erase_count && !poisoned);
	} else if (!strcmp(policy_case, "end-failure")) {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && !cache_bound);
	} else {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned);
		assert(!memcmp(old_media, media, sizeof(media)));
		assert(!program_count && !erase_count && !cache_bound);
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(begin_count == 1 && end_count == 1);
	}
	return 0;
}
