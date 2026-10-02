/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker.h>
#include <stdint.h>
#include <string.h>

#include "capsule_broker_internal.h"
#include "capsule_broker_info_internal.h"
#include "payload_mm_fmp_transaction_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Capsule broker transport must only be built in SMM"
#endif

struct transport_control {
	uint64_t last_transaction;
	uint64_t last_info_transaction;
	uint64_t last_close_transaction;
	bool busy;
};

static struct transport_control transport_authority;

static bool control_matches(const struct transport_control *expected)
{
	return !memcmp(&transport_authority, expected, sizeof(*expected));
}

#if ENV_TEST
void *capsule_broker_transport_test_authority(size_t *size)
{
	*size = sizeof(transport_authority);
	return &transport_authority;
}
#endif

static void transport_barrier(void)
{
	__asm__ __volatile__("" : : : "memory");
}

static bool request_valid(
	const struct capsule_broker_transport_request *request)
{
	if ((request->revision != CAPSULE_BROKER_TRANSPORT_REVISION_1 &&
	     request->revision != CAPSULE_BROKER_TRANSPORT_REVISION &&
	     request->revision != CAPSULE_BROKER_TRANSPORT_RAM_REVISION) ||
	    request->size != sizeof(*request) || request->flags ||
	    !request->generation || !request->transaction)
		return false;
	if (request->operation == CAPSULE_BROKER_TRANSPORT_EXECUTE)
		return request->intent_size ==
				sizeof(struct payload_mm_fmp_capsule_intent) &&
			request->result_size ==
				sizeof(struct capsule_broker_transport_result);
	if (request->operation == CAPSULE_BROKER_TRANSPORT_READ_INFO)
		return request->revision != CAPSULE_BROKER_TRANSPORT_REVISION_1 &&
			!request->intent_size && request->result_size ==
				sizeof(struct capsule_broker_transport_info);
	return request->revision == CAPSULE_BROKER_TRANSPORT_RAM_REVISION &&
		request->operation == CAPSULE_BROKER_TRANSPORT_CLOSE_RAM &&
		!request->intent_size && request->result_size ==
			sizeof(struct capsule_broker_transport_result);
}

static bool intent_matches_request(
	const struct capsule_broker_transport_request *request,
	const struct payload_mm_fmp_capsule_intent *intent)
{
	return intent->revision == PAYLOAD_MM_FMP_CAPSULE_INTENT_REVISION &&
		intent->size == sizeof(*intent) &&
		(intent->operation == PAYLOAD_MM_FMP_CAPSULE_CHECK ||
		 intent->operation == PAYLOAD_MM_FMP_CAPSULE_SET) &&
		!intent->flags && intent->capsule_size &&
		intent->broker_generation == request->generation &&
		intent->transaction == request->transaction &&
		intent->digest_algorithm == PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256 &&
		intent->digest_size == sizeof(intent->digest) && !intent->reserved &&
		capsule_broker_intent_matches(intent->broker_generation,
			intent->capsule_size);
}

static void result_pending(struct capsule_broker_transport_result *shared)
{
	*(volatile uint32_t *)&shared->result = CAPSULE_BROKER_RESULT_PENDING;
	transport_barrier();
	*(volatile uint32_t *)&shared->last_attempt_status =
		CAPSULE_BROKER_STATUS_PENDING;
	transport_barrier();
	memset(shared, 0, offsetof(struct capsule_broker_transport_result,
		last_attempt_status));
	transport_barrier();
}

static void result_commit(struct capsule_broker_transport_result *shared,
	const struct capsule_broker_transport_result *result)
{
	memcpy(shared, result,
		offsetof(struct capsule_broker_transport_result,
			last_attempt_status));
	transport_barrier();
	*(volatile uint32_t *)&shared->last_attempt_status =
		result->last_attempt_status;
	transport_barrier();
	*(volatile uint32_t *)&shared->result = result->result;
}

static void info_pending(struct capsule_broker_transport_info *shared)
{
	*(volatile uint32_t *)&shared->result = CAPSULE_BROKER_RESULT_PENDING;
	transport_barrier();
	memset(shared, 0,
		offsetof(struct capsule_broker_transport_info, result));
	transport_barrier();
}

static void info_commit(struct capsule_broker_transport_info *shared,
	const struct capsule_broker_transport_info *info)
{
	memcpy(shared, info,
		offsetof(struct capsule_broker_transport_info, result));
	transport_barrier();
	*(volatile uint32_t *)&shared->result = info->result;
}

static enum cb_err dispatch_info(uintptr_t transport_address,
	const struct capsule_broker_transport_request *request,
	const struct transport_control *expected)
{
	struct capsule_broker_info_snapshot snapshot;
	struct capsule_broker_transport_info info;
	struct capsule_broker_transport_info *shared;
	enum cb_err status;

	shared = (void *)(transport_address + CAPSULE_BROKER_TRANSPORT_INFO_OFFSET);
	info_pending(shared);
	memset(&snapshot, 0, sizeof(snapshot));
	status = capsule_broker_info_read(&snapshot);
	info_pending(shared);
	if (!control_matches(expected) || !capsule_broker_transport_ready(request->revision)) {
		transport_authority = *expected;
		status = CB_ERR;
	}
	memset(&info, 0, sizeof(info));
	info.revision = request->revision;
	info.size = sizeof(info);
	info.generation = request->generation;
	info.transaction = request->transaction;
	if (status == CB_SUCCESS) {
		info.image_type = snapshot.image_type;
		info.hardware_instance = snapshot.hardware_instance;
		info.current_version = snapshot.current_version;
		info.lowest_supported_version = snapshot.lowest_supported_version;
		info.image_size = snapshot.image_size;
		info.capabilities = snapshot.capabilities;
		info.state_flags = snapshot.state_flags;
		info.last_attempt_version = snapshot.last_attempt_version;
		info.last_attempt_status = snapshot.last_attempt_status;
	}
	info.result = status == CB_SUCCESS ? CAPSULE_BROKER_RESULT_SUCCESS :
		CAPSULE_BROKER_RESULT_EXECUTION;
	info_commit(shared, &info);
	memset(&snapshot, 0, sizeof(snapshot));
	memset(&info, 0, sizeof(info));
	return status;
}

static enum cb_err dispatch_close(uintptr_t transport_address,
	const struct capsule_broker_transport_request *request,
	const struct transport_control *expected)
{
	struct capsule_broker_transport_result result = { 0 };
	struct capsule_broker_transport_result *shared =
		(void *)(transport_address + CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET);
	enum cb_err status;

	result_pending(shared);
	status = capsule_broker_ram_window_close();
	result_pending(shared);
	if (!control_matches(expected) ||
	    !capsule_broker_transport_ready(request->revision)) {
		transport_authority = *expected;
		status = CB_ERR;
	}
	result.revision = request->revision;
	result.size = sizeof(result);
	result.generation = request->generation;
	result.transaction = request->transaction;
	result.result = status == CB_SUCCESS ? CAPSULE_BROKER_RESULT_SUCCESS :
		CAPSULE_BROKER_RESULT_EXECUTION;
	result_commit(shared, &result);
	memset(&result, 0, sizeof(result));
	return status;
}

enum cb_err capsule_broker_transport_dispatch(void)
{
	struct capsule_broker_transport_request request;
	struct payload_mm_fmp_capsule_intent intent;
	struct capsule_broker_transport_result result;
	struct capsule_broker_transport_result *shared_result;
	struct transport_control expected;
	void *transport_buffer;
	size_t transport_size;
	uint64_t transport_generation;
	uintptr_t transport_address;
	enum cb_err status;
	enum payload_mm_fmp_transaction_outcome outcome;

	if (transport_authority.busy ||
	    !capsule_broker_transport_buffer(&transport_buffer, &transport_size,
		&transport_generation) ||
	    transport_size != CAPSULE_BROKER_TRANSPORT_SIZE)
		return CB_ERR;
	transport_address = (uintptr_t)transport_buffer;
	transport_authority.busy = true;
	memcpy(&request, (const void *)(uintptr_t)(transport_address +
		CAPSULE_BROKER_TRANSPORT_REQUEST_OFFSET), sizeof(request));
	if (!request_valid(&request) || request.generation != transport_generation ||
	    !capsule_broker_transport_ready(request.revision))
		goto reject;
	if (request.operation == CAPSULE_BROKER_TRANSPORT_READ_INFO) {
		if (request.transaction <=
		    transport_authority.last_info_transaction)
			goto reject;
		transport_authority.last_info_transaction = request.transaction;
		expected = transport_authority;
		status = dispatch_info(transport_address, &request, &expected);
		memset(&request, 0, sizeof(request));
		transport_authority.busy = false;
		return status;
	}
	if (request.operation == CAPSULE_BROKER_TRANSPORT_CLOSE_RAM) {
		if (request.transaction <= transport_authority.last_close_transaction)
			goto reject;
		transport_authority.last_close_transaction = request.transaction;
		expected = transport_authority;
		status = dispatch_close(transport_address, &request, &expected);
		memset(&request, 0, sizeof(request));
		transport_authority.busy = false;
		return status;
	}
	if (!capsule_broker_execution_ready())
		goto reject;
	memcpy(&intent, (const void *)(uintptr_t)(transport_address +
		CAPSULE_BROKER_TRANSPORT_INTENT_OFFSET), sizeof(intent));
	if (!intent_matches_request(&request, &intent) ||
	    request.transaction <= transport_authority.last_transaction ||
	    !capsule_broker_execution_ready())
		goto reject;
	transport_authority.last_transaction = request.transaction;
	expected = transport_authority;

	shared_result = (void *)(uintptr_t)(transport_address +
		CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET);
	result_pending(shared_result);
	status = payload_mm_fmp_transaction_execute_intent(&intent, &outcome);
	result_pending(shared_result);
	if (!control_matches(&expected)) {
		payload_mm_fmp_transaction_close();
		transport_authority = expected;
		status = CB_ERR;
		outcome = PAYLOAD_MM_FMP_TRANSACTION_FAILED;
	}
	memset(&result, 0, sizeof(result));
	result.revision = request.revision;
	result.size = sizeof(result);
	result.generation = request.generation;
	result.transaction = request.transaction;
	result.attempted_version = intent.attempted_version;
	result.last_attempt_status = status == CB_SUCCESS ?
		CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS :
		CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL;
	result.result = status == CB_SUCCESS ? CAPSULE_BROKER_RESULT_SUCCESS :
		CAPSULE_BROKER_RESULT_EXECUTION;
	if (status != CB_SUCCESS &&
	    intent.operation == PAYLOAD_MM_FMP_CAPSULE_CHECK &&
	    outcome == PAYLOAD_MM_FMP_TRANSACTION_SIGNATURE_REFUSED)
		result.result = CAPSULE_BROKER_RESULT_SIGNATURE_REFUSED;
	result_commit(shared_result, &result);
	memset(&request, 0, sizeof(request));
	memset(&intent, 0, sizeof(intent));
	memset(&result, 0, sizeof(result));
	transport_authority.busy = false;
	return status;

reject:
	memset(&request, 0, sizeof(request));
	memset(&intent, 0, sizeof(intent));
	transport_authority.busy = false;
	return CB_ERR;
}
