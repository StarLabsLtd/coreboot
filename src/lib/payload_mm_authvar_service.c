/* SPDX-License-Identifier: GPL-2.0-only */

/* Shared with Star Labs CDK2; adapted from its native service validator. */

#include <boot/payload_mm_authvar_service.h>
#include <boot/authvar_image_policy.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool message_layout_valid(
	const struct lb_authvar_service_endpoint *endpoint, size_t *data_offset)
{
	uint64_t name_end;
	uint64_t aligned_data;
	uint64_t data_end;

	name_end = (uint64_t)PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE +
		(uint64_t)endpoint->maximum_name_size;
	if (name_end > endpoint->message_size || name_end > UINT64_MAX - 7U)
		return false;
	aligned_data = (name_end + 7U) & ~(uint64_t)7U;
	if (aligned_data > endpoint->message_size)
		return false;
	data_end = aligned_data + (uint64_t)endpoint->maximum_data_size;
	if (data_end > UINT32_MAX || data_end > endpoint->message_size)
		return false;
	*data_offset = (size_t)aligned_data;
	return true;
}

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size-- != 0U)
		value |= *bytes++;
	return value == 0U;
}

static bool name_valid(const struct lb_authvar_service_endpoint *endpoint,
	const void *message, size_t size)
{
	const uint8_t *name = (const uint8_t *)message +
		PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE;

	if (size < 2U * sizeof(uint16_t) || (size & 1U) != 0U ||
	    size > endpoint->maximum_name_size || name[size - 1U] != 0U ||
	    name[size - 2U] != 0U)
		return false;
	for (size_t offset = 0; offset + sizeof(uint16_t) < size;
	     offset += sizeof(uint16_t)) {
		if (name[offset] == 0U && name[offset + 1U] == 0U)
			return false;
	}
	return true;
}

enum cb_err payload_mm_authvar_service_endpoint_validate(
	const struct lb_authvar_service_endpoint *endpoint)
{
	size_t required;
	uint64_t communication_base;
	uint64_t generation;

	if (endpoint == NULL)
		return CB_ERR_ARG;
	communication_base = endpoint->communication_base;
	generation = endpoint->generation;

	if (endpoint->tag != LB_TAG_AUTHVAR_SERVICE_ENDPOINT ||
	    endpoint->size != sizeof(*endpoint) ||
	    endpoint->revision != LB_AUTHVAR_SERVICE_ENDPOINT_REVISION ||
	    endpoint->header_size != sizeof(*endpoint) ||
	    (endpoint->flags & ~(LB_AUTHVAR_ENDPOINT_IMAGE_POLICY_GENERAL |
		LB_AUTHVAR_ENDPOINT_STATE_PREDICATE_PINNED |
		LB_AUTHVAR_ENDPOINT_CONFIRMED_SETUP)) !=
		LB_AUTHVAR_ENDPOINT_REQUIRED_FLAGS ||
	    ((endpoint->flags & LB_AUTHVAR_ENDPOINT_STATE_PREDICATE_PINNED) &&
	     !(endpoint->flags & LB_AUTHVAR_ENDPOINT_IMAGE_POLICY_GENERAL)) ||
	    generation == 0U || communication_base == 0U ||
	    communication_base > UINTPTR_MAX ||
	    (communication_base & (sizeof(uint64_t) - 1U)) != 0U ||
	    endpoint->communication_size < PAYLOAD_MM_AUTHVAR_SERVICE_MIN_MESSAGE_SIZE ||
	    endpoint->communication_size > PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE ||
	    endpoint->message_size != endpoint->communication_size ||
	    endpoint->transport != LB_AUTHVAR_ENDPOINT_TRANSPORT_APM_IO8 ||
	    endpoint->trigger_width != sizeof(uint8_t) ||
	    endpoint->trigger_address == 0U ||
	    endpoint->trigger_address > UINT16_MAX ||
	    endpoint->trigger_value == 0U || endpoint->trigger_value > UINT8_MAX ||
	    endpoint->maximum_name_size < 2U * sizeof(uint16_t) ||
	    (endpoint->maximum_name_size & 1U) != 0U ||
	    endpoint->maximum_data_size == 0U || endpoint->reserved != 0U)
		return CB_ERR_ARG;
	if (!message_layout_valid(endpoint, &required) ||
	    endpoint->message_size - 1U > UINTPTR_MAX - (uintptr_t)communication_base)
		return CB_ERR_ARG;
	return CB_SUCCESS;
}

static bool pending_result_valid(
	const struct payload_mm_authvar_service_frame *frame)
{
	return frame->status == PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING &&
		frame->maximum_storage == 0U && frame->remaining_storage == 0U &&
		frame->maximum_variable == 0U && frame->result_name_size == 0U &&
		frame->result_data_size == 0U && frame->result_attributes == 0U &&
		bytes_zero(frame->result_vendor_guid,
			sizeof(frame->result_vendor_guid)) &&
		frame->reserved[0] == 0U && frame->reserved[1] == 0U &&
		frame->completion == PAYLOAD_MM_AUTHVAR_SERVICE_PENDING;
}

static bool confirmed_key_request_valid(
	const struct lb_authvar_service_endpoint *endpoint,
	const struct payload_mm_authvar_confirmed_key_frame *request, size_t size)
{
	const struct payload_mm_authvar_confirmed_frame *confirmed = &request->confirmed;
	const struct payload_mm_authvar_service_frame *frame = &confirmed->service;
	uint32_t attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS | PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;

	if (size < sizeof(*request) || frame->data_size < 40U ||
	    frame->data_size > endpoint->maximum_data_size ||
	    frame->data_size > size - sizeof(*request) ||
	    request->key_id < PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_PK ||
	    request->key_id > PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_DBT ||
	    request->mutation < PAYLOAD_MM_AUTHVAR_CONFIRMED_REPLACE ||
	    request->mutation > PAYLOAD_MM_AUTHVAR_CONFIRMED_DELETE ||
	    (request->mutation == PAYLOAD_MM_AUTHVAR_CONFIRMED_APPEND &&
	     request->key_id == PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_PK) ||
	    (request->mutation == PAYLOAD_MM_AUTHVAR_CONFIRMED_DELETE && frame->data_size != 40U))
		return false;
	if (request->mutation == PAYLOAD_MM_AUTHVAR_CONFIRMED_APPEND)
		attributes |= PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	return (endpoint->flags & LB_AUTHVAR_ENDPOINT_CONFIRMED_SETUP) &&
		frame->header_size == sizeof(*request) &&
		frame->operation == PAYLOAD_MM_AUTHVAR_SERVICE_CONFIRMED_SETUP &&
		frame->flags == PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_MUTATION &&
		frame->generation == endpoint->generation && frame->request_id &&
		frame->request_id != UINT64_MAX && frame->attributes == attributes &&
		!frame->name_size && !frame->name_capacity && !frame->data_capacity &&
		!frame->reserved0 && bytes_zero(frame->vendor_guid, sizeof(frame->vendor_guid)) &&
		pending_result_valid(frame) && !confirmed->value && !confirmed->result_flags &&
		!bytes_zero(confirmed->capability, sizeof(confirmed->capability)) &&
		bytes_zero((const uint8_t *)request + sizeof(*request) + frame->data_size,
			size - sizeof(*request) - frame->data_size);
}

static bool confirmed_request_valid(
	const struct lb_authvar_service_endpoint *endpoint,
	const struct payload_mm_authvar_confirmed_frame *request, size_t size)
{
	const struct payload_mm_authvar_service_frame *frame = &request->service;

	return (endpoint->flags & LB_AUTHVAR_ENDPOINT_CONFIRMED_SETUP) &&
		size >= sizeof(*request) && frame->header_size == sizeof(*request) &&
		frame->operation == PAYLOAD_MM_AUTHVAR_SERVICE_CONFIRMED_SETUP &&
		frame->flags >= PAYLOAD_MM_AUTHVAR_CONFIRMED_ENABLE &&
		frame->flags <= PAYLOAD_MM_AUTHVAR_CONFIRMED_DELETE_PK &&
		frame->generation == endpoint->generation && frame->request_id &&
		frame->request_id != UINT64_MAX &&
		!frame->attributes && !frame->name_size && !frame->data_size &&
		!frame->name_capacity && !frame->data_capacity && !frame->reserved0 &&
		bytes_zero(frame->vendor_guid, sizeof(frame->vendor_guid)) &&
		pending_result_valid(frame) && request->value <= 1U &&
		(frame->flags != PAYLOAD_MM_AUTHVAR_CONFIRMED_DELETE_PK || !request->value) &&
		!request->result_flags &&
		!bytes_zero(request->capability, sizeof(request->capability)) &&
		bytes_zero((const uint8_t *)request + sizeof(*request), size - sizeof(*request));
}

enum cb_err payload_mm_authvar_service_request_validate(
	const struct lb_authvar_service_endpoint *endpoint, const void *message,
	size_t message_size)
{
	const struct payload_mm_authvar_service_frame *frame = message;
	bool guid_zero;
	size_t data_offset;

	if (payload_mm_authvar_service_endpoint_validate(endpoint) != CB_SUCCESS ||
	    !message || message_size != endpoint->message_size)
		return CB_ERR_ARG;
	if (frame->revision == PAYLOAD_MM_AUTHVAR_CONFIRMED_REVISION)
		return confirmed_request_valid(endpoint, message, message_size) ?
			CB_SUCCESS : CB_ERR_ARG;
	if (frame->revision == PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_REVISION)
		return confirmed_key_request_valid(endpoint, message, message_size) ?
			CB_SUCCESS : CB_ERR_ARG;

	if (!message_layout_valid(endpoint, &data_offset) ||
	    !bytes_zero((const uint8_t *)message + data_offset + endpoint->maximum_data_size,
		message_size - data_offset - endpoint->maximum_data_size) ||
	    frame->revision != PAYLOAD_MM_AUTHVAR_SERVICE_REVISION ||
	    frame->header_size != sizeof(*frame) || frame->request_id == 0U ||
	    frame->generation != endpoint->generation ||
	    frame->flags != 0U ||
	    frame->reserved0 != 0U || !pending_result_valid(frame) ||
	    (frame->attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED) != 0U ||
	    frame->name_size > endpoint->maximum_name_size ||
	    frame->data_size > endpoint->maximum_data_size ||
	    frame->name_capacity > endpoint->maximum_name_size ||
	    frame->data_capacity > endpoint->maximum_data_size)
		return CB_ERR_ARG;
	guid_zero = bytes_zero(frame->vendor_guid, sizeof(frame->vendor_guid));
	switch (frame->operation) {
	case PAYLOAD_MM_AUTHVAR_SERVICE_IMAGE_POLICY_SNAPSHOT:
		if (!guid_zero || frame->attributes || frame->name_size ||
		    frame->data_size || frame->name_capacity ||
		    !bytes_zero((const uint8_t *)message + sizeof(*frame),
			message_size - sizeof(*frame)))
			return CB_ERR_ARG;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_KEY_CLASSIFY:
	case PAYLOAD_MM_AUTHVAR_SERVICE_GET:
		if (frame->operation == PAYLOAD_MM_AUTHVAR_SERVICE_KEY_CLASSIFY &&
		    !bytes_zero((const uint8_t *)message + sizeof(*frame) + frame->name_size,
			message_size - sizeof(*frame) - frame->name_size))
			return CB_ERR_ARG;
		if (!name_valid(endpoint, message, frame->name_size) ||
		    frame->attributes != 0U || frame->data_size != 0U ||
		    frame->name_capacity != 0U)
			return CB_ERR_ARG;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_NEXT:
		if (frame->attributes != 0U || frame->data_size != 0U ||
		    frame->data_capacity != 0U ||
		    frame->name_capacity < sizeof(uint16_t) ||
		    frame->name_size > frame->name_capacity ||
		    (frame->name_size != 0U ?
		     !name_valid(endpoint, message, frame->name_size) :
		     !guid_zero))
			return CB_ERR_ARG;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_SET:
		if (!name_valid(endpoint, message, frame->name_size) ||
		    frame->name_capacity != 0U || frame->data_capacity != 0U ||
		    (frame->attributes == 0U && frame->data_size != 0U))
			return CB_ERR_ARG;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_QUERY:
		if (!guid_zero || frame->attributes == 0U ||
		    (frame->attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE) != 0U ||
		    frame->name_size != 0U || frame->data_size != 0U ||
		    frame->name_capacity != 0U || frame->data_capacity != 0U)
			return CB_ERR_ARG;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT:
	case PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME:
	case PAYLOAD_MM_AUTHVAR_SERVICE_LOCK_POLICY:
		if (!guid_zero || frame->attributes != 0U || frame->name_size != 0U ||
		    frame->data_size != 0U || frame->name_capacity != 0U ||
		    frame->data_capacity != 0U)
			return CB_ERR_ARG;
		if (frame->operation == PAYLOAD_MM_AUTHVAR_SERVICE_LOCK_POLICY &&
		    !bytes_zero((const uint8_t *)message + sizeof(*frame),
			message_size - sizeof(*frame)))
			return CB_ERR_ARG;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_REGISTER_POLICY:
		if (!guid_zero || frame->attributes != 0U || frame->name_size != 0U ||
		    frame->name_capacity != 0U || frame->data_capacity != 0U ||
		    frame->data_size < PAYLOAD_MM_AUTHVAR_POLICY_MIN_SIZE ||
		    frame->data_size > PAYLOAD_MM_AUTHVAR_POLICY_MAX_SIZE ||
		    !bytes_zero((const uint8_t *)message + sizeof(*frame),
			data_offset - sizeof(*frame)) ||
		    !bytes_zero((const uint8_t *)message + data_offset + frame->data_size,
			message_size - data_offset - frame->data_size))
			return CB_ERR_ARG;
		break;
	default:
		return CB_ERR_ARG;
	}
	return CB_SUCCESS;
}

static bool request_identity_equal(
	const struct payload_mm_authvar_service_frame *request,
	const struct payload_mm_authvar_service_frame *response)
{
	return request->revision == response->revision &&
		request->header_size == response->header_size &&
		request->operation == response->operation &&
		request->flags == response->flags &&
		request->generation == response->generation &&
		request->request_id == response->request_id &&
		memcmp(request->vendor_guid, response->vendor_guid,
			sizeof(request->vendor_guid)) == 0 &&
		request->attributes == response->attributes &&
		request->name_size == response->name_size &&
		request->data_size == response->data_size &&
		request->name_capacity == response->name_capacity &&
		request->data_capacity == response->data_capacity &&
		response->reserved0 == 0U;
}

static bool stored_attributes_valid(uint32_t attributes)
{
	if (attributes == 0U ||
	    (attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED) != 0U ||
	    (attributes & (PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE |
			   PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE)) != 0U ||
	    (attributes & PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS) == 0U)
		return false;
	if ((attributes & PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR) != 0U &&
	    (attributes & (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			   PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			   PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)) !=
		(PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		 PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		 PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS))
		return false;
	return true;
}

static bool result_empty(const struct payload_mm_authvar_service_frame *frame)
{
	return frame->maximum_storage == 0U && frame->remaining_storage == 0U &&
		frame->maximum_variable == 0U && frame->result_name_size == 0U &&
		frame->result_data_size == 0U && frame->result_attributes == 0U &&
		bytes_zero(frame->result_vendor_guid,
			sizeof(frame->result_vendor_guid));
}

static bool name_slot_tail_zero(
	const struct lb_authvar_service_endpoint *endpoint, const void *response,
	size_t used)
{
	const uint8_t *bytes = response;
	size_t data_offset;
	size_t offset;

	if (used > endpoint->maximum_name_size ||
	    !message_layout_valid(endpoint, &data_offset))
		return false;
	offset = PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE + used;
	return bytes_zero(bytes + offset, data_offset - offset);
}

static bool data_slot_tail_zero(
	const struct lb_authvar_service_endpoint *endpoint, const void *response,
	size_t used)
{
	const uint8_t *bytes = response;
	size_t data_offset;

	if (used > endpoint->maximum_data_size ||
	    !message_layout_valid(endpoint, &data_offset))
		return false;
	return bytes_zero(bytes + data_offset + used,
		endpoint->message_size - data_offset - used);
}

static bool get_response_valid(
	const struct lb_authvar_service_endpoint *endpoint,
	const struct payload_mm_authvar_service_frame *request,
	const struct payload_mm_authvar_service_frame *response)
{
	if (response->result_name_size != 0U || response->maximum_storage != 0U ||
	    response->remaining_storage != 0U || response->maximum_variable != 0U ||
	    !bytes_zero(response->result_vendor_guid,
		    sizeof(response->result_vendor_guid)) ||
	    !name_slot_tail_zero(endpoint, response, 0U))
		return false;
	switch (response->status) {
	case PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS:
		return response->result_data_size != 0U &&
			response->result_data_size <= request->data_capacity &&
			stored_attributes_valid(response->result_attributes) &&
			data_slot_tail_zero(endpoint, response,
				response->result_data_size);
	case PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL:
		return response->result_data_size > request->data_capacity &&
			stored_attributes_valid(response->result_attributes) &&
			data_slot_tail_zero(endpoint, response, 0U);
	case PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND:
	case PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR:
	case PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED:
	case PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED:
		return response->result_data_size == 0U &&
			response->result_attributes == 0U &&
			data_slot_tail_zero(endpoint, response, 0U);
	default:
		return false;
	}
}

static bool next_response_valid(
	const struct lb_authvar_service_endpoint *endpoint,
	const struct payload_mm_authvar_service_frame *request,
	const struct payload_mm_authvar_service_frame *response)
{
	if (response->result_data_size != 0U ||
	    response->result_attributes != 0U || response->maximum_storage != 0U ||
	    response->remaining_storage != 0U || response->maximum_variable != 0U ||
	    !data_slot_tail_zero(endpoint, response, 0U))
		return false;
	switch (response->status) {
	case PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS:
		return response->result_name_size != 0U &&
			response->result_name_size <= request->name_capacity &&
			name_valid(endpoint, response, response->result_name_size) &&
			name_slot_tail_zero(endpoint, response,
				response->result_name_size);
	case PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL:
		return response->result_name_size >= 2U * sizeof(uint16_t) &&
			(response->result_name_size & 1U) == 0U &&
			response->result_name_size > request->name_capacity &&
			bytes_zero(response->result_vendor_guid,
				sizeof(response->result_vendor_guid)) &&
			name_slot_tail_zero(endpoint, response, 0U);
	case PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND:
	case PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER:
	case PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR:
	case PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED:
	case PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED:
		return response->result_name_size == 0U &&
			bytes_zero(response->result_vendor_guid,
				sizeof(response->result_vendor_guid)) &&
			name_slot_tail_zero(endpoint, response, 0U);
	default:
		return false;
	}
}

static bool set_response_valid(
	const struct lb_authvar_service_endpoint *endpoint,
	const struct payload_mm_authvar_service_frame *response)
{
	if (!result_empty(response) ||
	    !name_slot_tail_zero(endpoint, response, 0U) ||
	    !data_slot_tail_zero(endpoint, response, 0U))
		return false;
	switch (response->status) {
	case PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS:
	case PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER:
	case PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED:
	case PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR:
	case PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED:
	case PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES:
	case PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND:
	case PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED:
	case PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION:
		return true;
	default:
		return false;
	}
}

static bool policy_response_valid(
	const struct lb_authvar_service_endpoint *endpoint,
	const struct payload_mm_authvar_service_frame *response)
{
	if (!result_empty(response) ||
	    !name_slot_tail_zero(endpoint, response, 0U) ||
	    !data_slot_tail_zero(endpoint, response, 0U))
		return false;
	switch (response->status) {
	case PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS:
	case PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED:
	case PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR:
	case PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED:
		return true;
	case PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER:
	case PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES:
	case PAYLOAD_MM_AUTHVAR_STATUS_ALREADY_STARTED:
		return response->operation == PAYLOAD_MM_AUTHVAR_SERVICE_REGISTER_POLICY;
	default:
		return false;
	}
}

static bool query_response_valid(
	const struct lb_authvar_service_endpoint *endpoint,
	const struct payload_mm_authvar_service_frame *response)
{
	if (response->result_name_size != 0U ||
	    response->result_data_size != 0U || response->result_attributes != 0U ||
	    !bytes_zero(response->result_vendor_guid,
		    sizeof(response->result_vendor_guid)) ||
	    !name_slot_tail_zero(endpoint, response, 0U) ||
	    !data_slot_tail_zero(endpoint, response, 0U))
		return false;
	if (response->status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return response->maximum_storage != 0U &&
			response->remaining_storage <= response->maximum_storage &&
			response->maximum_variable <= response->remaining_storage;
	if (response->status != PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER &&
	    response->status != PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED &&
	    response->status != PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
	    response->status != PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED)
		return false;
	return response->maximum_storage == 0U &&
		response->remaining_storage == 0U && response->maximum_variable == 0U;
}

static bool lifecycle_response_valid(
	const struct lb_authvar_service_endpoint *endpoint,
	const struct payload_mm_authvar_service_frame *response)
{
	return (response->status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
		response->status == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED ||
		response->status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR ||
		response->status == PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED) &&
		result_empty(response) &&
		name_slot_tail_zero(endpoint, response, 0U) &&
		data_slot_tail_zero(endpoint, response, 0U);
}

static bool classification_response_valid(
	const struct lb_authvar_service_endpoint *endpoint,
	const struct payload_mm_authvar_service_frame *request,
	const struct payload_mm_authvar_service_frame *response)
{
	const uint8_t *body;
	uint32_t fields[4];
	size_t offset;

	if (response->maximum_storage || response->remaining_storage ||
	    response->maximum_variable || response->result_name_size ||
	    response->result_attributes ||
	    !bytes_zero(response->result_vendor_guid, sizeof(response->result_vendor_guid)) ||
	    !name_slot_tail_zero(endpoint, response, 0) ||
	    !message_layout_valid(endpoint, &offset))
		return false;
	switch (response->status) {
	case PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS:
		if (response->result_data_size != PAYLOAD_MM_AUTHVAR_KEY_CLASSIFICATION_SIZE ||
		    request->data_capacity < PAYLOAD_MM_AUTHVAR_KEY_CLASSIFICATION_SIZE ||
		    !data_slot_tail_zero(endpoint, response, PAYLOAD_MM_AUTHVAR_KEY_CLASSIFICATION_SIZE))
			return false;
		body = (const uint8_t *)response + offset;
		for (size_t i = 0; i < 4; i++)
			fields[i] = (uint32_t)body[4 * i] | (uint32_t)body[4 * i + 1] << 8 |
				(uint32_t)body[4 * i + 2] << 16 | (uint32_t)body[4 * i + 3] << 24;
		if (fields[0] != PAYLOAD_MM_AUTHVAR_KEY_CLASSIFICATION_REVISION ||
		    fields[3] & ~PAYLOAD_MM_AUTHVAR_KEY_VISIBLE ||
		    (!(fields[3] & PAYLOAD_MM_AUTHVAR_KEY_VISIBLE) &&
		     (fields[2] & PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)))
			return false;
		if ((endpoint->flags & LB_AUTHVAR_ENDPOINT_STATE_PREDICATE_PINNED) &&
		    (fields[1] == PAYLOAD_MM_AUTHVAR_KEY_VOLATILE_PINNED ||
		     fields[1] == PAYLOAD_MM_AUTHVAR_KEY_PERSISTENT) && !fields[2])
			return !fields[3];
		if (fields[1] == PAYLOAD_MM_AUTHVAR_KEY_VOLATILE_PINNED) {
			const uint32_t authenticated = PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
				PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;

			if (fields[2] == authenticated ||
			    fields[2] == (authenticated | PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS))
				return true;
			return (endpoint->flags & LB_AUTHVAR_ENDPOINT_STATE_PREDICATE_PINNED) &&
				(fields[2] == 2U || fields[2] == 6U);
		}
		if (!stored_attributes_valid(fields[2]))
			return false;
		if (fields[1] == PAYLOAD_MM_AUTHVAR_KEY_SYNTHETIC)
			return fields[3] == PAYLOAD_MM_AUTHVAR_KEY_VISIBLE &&
				(fields[2] == 6U || fields[2] == 38U);
		return fields[1] == PAYLOAD_MM_AUTHVAR_KEY_PERSISTENT;
	case PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL:
		return response->result_data_size == PAYLOAD_MM_AUTHVAR_KEY_CLASSIFICATION_SIZE &&
			request->data_capacity < PAYLOAD_MM_AUTHVAR_KEY_CLASSIFICATION_SIZE &&
			data_slot_tail_zero(endpoint, response, 0);
	case PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND:
	case PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER:
	case PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR:
	case PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED:
	case PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED:
		return !response->result_data_size && data_slot_tail_zero(endpoint, response, 0);
	default:
		return false;
	}
}

static bool snapshot_response_valid(
	const struct lb_authvar_service_endpoint *endpoint,
	const struct payload_mm_authvar_service_frame *request,
	const struct payload_mm_authvar_service_frame *response)
{
	struct authvar_image_policy_snapshot_view parsed;
	size_t data_offset;

	if (response->maximum_storage || response->remaining_storage ||
	    response->maximum_variable || response->result_name_size ||
	    response->result_attributes ||
	    !bytes_zero(response->result_vendor_guid, sizeof(response->result_vendor_guid)) ||
	    !name_slot_tail_zero(endpoint, response, 0) ||
	    !message_layout_valid(endpoint, &data_offset))
		return false;
	switch (response->status) {
	case PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS:
		return response->result_data_size <= request->data_capacity &&
			authvar_image_policy_snapshot_decode_shape(
				(const uint8_t *)response + data_offset, response->result_data_size,
				&parsed) &&
			data_slot_tail_zero(endpoint, response, response->result_data_size);
	case PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL:
		return response->result_data_size >= AUTHVAR_IMAGE_POLICY_SNAPSHOT_HEADER_SIZE &&
			response->result_data_size <= AUTHVAR_IMAGE_POLICY_SNAPSHOT_MAX_SIZE &&
			response->result_data_size > request->data_capacity &&
			data_slot_tail_zero(endpoint, response, 0);
	case PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED:
	case PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR:
	case PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER:
		return !response->result_data_size && data_slot_tail_zero(endpoint, response, 0);
	default:
		return false;
	}
}

static bool confirmed_response_valid(
	const struct payload_mm_authvar_confirmed_frame *request,
	const struct payload_mm_authvar_confirmed_frame *response, size_t size,
	uint32_t completion)
{
	const struct payload_mm_authvar_service_frame *frame = &response->service;

	if (!request_identity_equal(&request->service, frame) ||
	    frame->completion != completion || !result_empty(frame) ||
	    frame->reserved[0] || frame->reserved[1] ||
	    !bytes_zero(response->capability, sizeof(response->capability)) ||
	    request->value != response->value ||
	    response->result_flags & ~PAYLOAD_MM_AUTHVAR_CONFIRMED_RESET_REQUIRED ||
	    (response->result_flags && frame->flags != PAYLOAD_MM_AUTHVAR_CONFIRMED_DELETE_PK) ||
	    !bytes_zero((const uint8_t *)response + sizeof(*response), size - sizeof(*response)))
		return false;
	switch (frame->status) {
	case PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS:
	case PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER:
	case PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED:
	case PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR:
	case PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED:
	case PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES:
	case PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND:
	case PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED:
	case PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION:
		return true;
	default:
		return false;
	}
}

static bool confirmed_key_response_valid(
	const struct payload_mm_authvar_confirmed_key_frame *request,
	const struct payload_mm_authvar_confirmed_key_frame *response, size_t size,
	uint32_t completion)
{
	return confirmed_response_valid(&request->confirmed, &response->confirmed,
		sizeof(response->confirmed), completion) &&
		!response->confirmed.result_flags &&
		request->key_id == response->key_id && request->mutation == response->mutation &&
		bytes_zero((const uint8_t *)response + sizeof(*response), size - sizeof(*response));
}

enum cb_err payload_mm_authvar_service_response_validate(
	const struct lb_authvar_service_endpoint *endpoint, const void *request,
	const void *response, size_t message_size)
{
	const struct payload_mm_authvar_service_frame *before = request;
	const struct payload_mm_authvar_service_frame *after = response;

	if (payload_mm_authvar_service_request_validate(endpoint, request, message_size) != CB_SUCCESS ||
	    !response || message_size != endpoint->message_size)
		return CB_ERR_ARG;
	if (before->revision == PAYLOAD_MM_AUTHVAR_CONFIRMED_REVISION)
		return confirmed_response_valid(request, response, message_size, PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE) ?
			CB_SUCCESS : CB_ERR_ARG;
	if (before->revision == PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_REVISION)
		return confirmed_key_response_valid(request, response, message_size, PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE) ?
			CB_SUCCESS : CB_ERR_ARG;

	if (!request_identity_equal(before, after) ||
	    after->status == PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING ||
	    after->completion != PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE ||
	    after->reserved[0] != 0U || after->reserved[1] != 0U ||
	    after->result_name_size > endpoint->maximum_name_size ||
	    (after->result_data_size > endpoint->maximum_data_size &&
	     !((after->operation == PAYLOAD_MM_AUTHVAR_SERVICE_GET ||
		after->operation == PAYLOAD_MM_AUTHVAR_SERVICE_IMAGE_POLICY_SNAPSHOT ||
		after->operation == PAYLOAD_MM_AUTHVAR_SERVICE_KEY_CLASSIFY) &&
	       after->status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL)) ||
	    (after->result_attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED) != 0U)
		return CB_ERR_ARG;
	switch (after->operation) {
	case PAYLOAD_MM_AUTHVAR_SERVICE_KEY_CLASSIFY:
		return classification_response_valid(endpoint, before, after) ?
			CB_SUCCESS : CB_ERR_ARG;
	case PAYLOAD_MM_AUTHVAR_SERVICE_IMAGE_POLICY_SNAPSHOT:
		return snapshot_response_valid(endpoint, before, after) ?
			CB_SUCCESS : CB_ERR_ARG;
	case PAYLOAD_MM_AUTHVAR_SERVICE_GET:
		return get_response_valid(endpoint, before, after) ?
			CB_SUCCESS : CB_ERR_ARG;
	case PAYLOAD_MM_AUTHVAR_SERVICE_NEXT:
		return next_response_valid(endpoint, before, after) ?
			CB_SUCCESS : CB_ERR_ARG;
	case PAYLOAD_MM_AUTHVAR_SERVICE_SET:
		return set_response_valid(endpoint, after) ?
			CB_SUCCESS : CB_ERR_ARG;
	case PAYLOAD_MM_AUTHVAR_SERVICE_REGISTER_POLICY:
	case PAYLOAD_MM_AUTHVAR_SERVICE_LOCK_POLICY:
		return policy_response_valid(endpoint, after) ?
			CB_SUCCESS : CB_ERR_ARG;
	case PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT:
	case PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME:
		return lifecycle_response_valid(endpoint, after) ?
			CB_SUCCESS : CB_ERR_ARG;
	case PAYLOAD_MM_AUTHVAR_SERVICE_QUERY:
		return query_response_valid(endpoint, after) ?
			CB_SUCCESS : CB_ERR_ARG;
	default:
		return CB_ERR_ARG;
	}
}
