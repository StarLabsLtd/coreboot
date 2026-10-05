/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_executor.h>
#include <boot/payload_mm_authvar_policy.h>
#include <boot/payload_mm_authvar_presence_authority.h>
#include <boot/payload_mm_authvar_service.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Authenticated-variable service execution is SMM-only"
#endif

static uint32_t service_busy;

enum cb_err payload_mm_authvar_service_transaction(
	const struct lb_authvar_service_endpoint *endpoint,
	const void *request, void *response, size_t size)
{
	struct lb_authvar_service_endpoint descriptor;
	struct payload_mm_authvar_service_frame header;
	struct payload_mm_authvar_service_frame *frame = response;
	struct payload_mm_authvar_read_request read = {0};
	struct payload_mm_authvar_read_result read_result;
	struct payload_mm_authvar_policy_request write = {0};
	struct payload_mm_authvar_policy_result write_result;
	size_t data_offset;
	uint64_t status;
	uint32_t expected = 0;
	enum cb_err result = CB_ERR;

	/* These are private snapshots owned by the installed route, never the mailbox. */
	if ((uintptr_t)request % _Alignof(header) ||
	    (uintptr_t)response % _Alignof(header) ||
	    payload_mm_authvar_executor_service_admit(endpoint, request, response, size) !=
		CB_SUCCESS ||
	    payload_mm_authvar_buffers_overlap(request, size, &service_busy,
		sizeof(service_busy)) ||
	    payload_mm_authvar_buffers_overlap(response, size, &service_busy,
		sizeof(service_busy)) ||
	    payload_mm_authvar_buffers_overlap(endpoint, sizeof(*endpoint), &service_busy,
		sizeof(service_busy)))
		return CB_ERR;
	descriptor = *endpoint;
	header = *(const struct payload_mm_authvar_service_frame *)request;
	if (payload_mm_authvar_service_request_validate(&descriptor, request, size) !=
		CB_SUCCESS ||
	    !__atomic_compare_exchange_n(&service_busy, &expected, 1U, false,
		__ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return CB_ERR;
	data_offset = (sizeof(header) + descriptor.maximum_name_size + 7U) & ~(size_t)7U;
	memset(response, 0, size);
	*frame = header;
	read.operation = write.operation = header.operation;
	read.attributes = write.attributes = header.attributes;
	memcpy(read.vendor_guid, header.vendor_guid, sizeof(read.vendor_guid));
	memcpy(write.vendor_guid, header.vendor_guid, sizeof(write.vendor_guid));
	read.name = write.name = header.name_size ?
		(const uint8_t *)request + sizeof(header) : NULL;
	read.name_size = write.name_size = header.name_size;
	/* NEXT output is distinct from its input key and both result descriptors. */
	read.result_name = header.name_capacity ? (uint8_t *)response + sizeof(header) : NULL;
	read.name_capacity = header.name_capacity;
	read.result_data = header.data_capacity ? (uint8_t *)response + data_offset : NULL;
	read.data_capacity = header.data_capacity;
	write.data = header.data_size ? (const uint8_t *)request + data_offset : NULL;
	write.data_size = header.data_size;
	memset(&read_result, 0, sizeof(read_result));
	read_result.status = PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING;
	read_result.completion = PAYLOAD_MM_AUTHVAR_SERVICE_PENDING;
	memset(&write_result, 0, sizeof(write_result));
	write_result.status = PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING;
	write_result.completion = PAYLOAD_MM_AUTHVAR_SERVICE_PENDING;

	switch (header.operation) {
	case PAYLOAD_MM_AUTHVAR_SERVICE_GET:
	case PAYLOAD_MM_AUTHVAR_SERVICE_NEXT:
	case PAYLOAD_MM_AUTHVAR_SERVICE_QUERY:
	case PAYLOAD_MM_AUTHVAR_SERVICE_IMAGE_POLICY_SNAPSHOT:
	case PAYLOAD_MM_AUTHVAR_SERVICE_KEY_CLASSIFY:
		status = payload_mm_authvar_read_transaction(&read, &read_result);
		if (read_result.completion != PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE ||
		    read_result.status != status || read_result.reserved)
			goto out;
		frame->result_name_size = read_result.required_name_size;
		frame->result_data_size = read_result.required_data_size;
		frame->result_attributes = read_result.attributes;
		memcpy(frame->result_vendor_guid, read_result.vendor_guid,
			sizeof(frame->result_vendor_guid));
		frame->maximum_storage = read_result.maximum_storage;
		frame->remaining_storage = read_result.remaining_storage;
		frame->maximum_variable = read_result.maximum_variable;
		if (header.operation == PAYLOAD_MM_AUTHVAR_SERVICE_QUERY &&
		    !(header.attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED) &&
		    frame->maximum_variable > descriptor.maximum_data_size)
			frame->maximum_variable = descriptor.maximum_data_size;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_SET:
		status = payload_mm_authvar_set_transaction(&write, &write_result);
		if (write_result.completion != PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE ||
		    write_result.status != status || write_result.reserved)
			goto out;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_CONFIRMED_SETUP:
	{
		struct payload_mm_authvar_confirmed_frame *confirmed = response;
		const struct payload_mm_authvar_confirmed_frame *snapshot = request;

		confirmed->value = snapshot->value;
		if (header.revision == PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_REVISION) {
			struct payload_mm_authvar_confirmed_key_frame *key_response = response;
			const struct payload_mm_authvar_confirmed_key_frame *key_request = request;

			key_response->key_id = key_request->key_id;
			key_response->mutation = key_request->mutation;
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY)
			status = payload_mm_authvar_presence_confirmed_key(key_request, size);
#else
			status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
#endif
			break;
		}
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY)
		status = payload_mm_authvar_presence_confirmed_action(snapshot,
			&confirmed->result_flags);
#else
		status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
#endif
		break;
	}
	case PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT:
	case PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME:
		write.name = NULL;
		write.name_size = 0;
		status = payload_mm_authvar_policy_transaction(&write, &write_result);
		if (write_result.completion != PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE ||
		    write_result.status != status || write_result.reserved)
			goto out;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_REGISTER_POLICY:
		status = payload_mm_authvar_variable_policy_register(write.data, write.data_size);
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_LOCK_POLICY:
		status = payload_mm_authvar_variable_policy_lock();
		break;
	default:
		goto out;
	}
	if (memcmp(endpoint, &descriptor, sizeof(descriptor)) ||
	    memcmp(request, &header, sizeof(header)) ||
	    payload_mm_authvar_executor_service_admit(endpoint, request, response, size) !=
		CB_SUCCESS)
		goto out;
	frame->status = status;
	if (payload_mm_authvar_service_response_validate_pending(&descriptor, request, response,
		size) != CB_SUCCESS) {
		goto out;
	}
	/* Only the outer fixed route may publish this private response to the mailbox. */
	__atomic_store_n(&frame->completion, PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE,
		__ATOMIC_RELEASE);
	result = CB_SUCCESS;
out:
	if (result != CB_SUCCESS) {
		memset(response, 0, size);
		frame->status = PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING;
		__atomic_store_n(&frame->completion, PAYLOAD_MM_AUTHVAR_SERVICE_PENDING,
			__ATOMIC_RELEASE);
	}
	memset(&read, 0, sizeof(read));
	memset(&read_result, 0, sizeof(read_result));
	memset(&write, 0, sizeof(write));
	memset(&write_result, 0, sizeof(write_result));
	memset(&header, 0, sizeof(header));
	memset(&descriptor, 0, sizeof(descriptor));
	__atomic_store_n(&service_busy, 0U, __ATOMIC_RELEASE);
	return result;
}
