/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_service.h>
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
	if (data_end > UINT32_MAX || data_end != endpoint->message_size)
		return false;
	*data_offset = (size_t)aligned_data;
	return true;
}

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}

static bool name_valid(const struct lb_authvar_service_endpoint *endpoint,
	const void *message, uint32_t size)
{
	const uint8_t *name = (const uint8_t *)message +
		PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE;

	if (size < 2U * sizeof(uint16_t) || (size & 1U) ||
	    size > endpoint->maximum_name_size || name[size - 1] ||
	    name[size - 2])
		return false;
	for (uint32_t offset = 0; offset + sizeof(uint16_t) < size;
	     offset += sizeof(uint16_t)) {
		if (!name[offset] && !name[offset + 1U])
			return false;
	}
	return true;
}

enum cb_err payload_mm_authvar_service_endpoint_validate(
	const struct lb_authvar_service_endpoint *endpoint)
{
	size_t required;

	if (!endpoint || endpoint->tag != LB_TAG_AUTHVAR_SERVICE_ENDPOINT ||
	    endpoint->size != sizeof(*endpoint) ||
	    endpoint->revision != LB_AUTHVAR_SERVICE_ENDPOINT_REVISION ||
	    endpoint->header_size != sizeof(*endpoint) ||
	    endpoint->flags != LB_AUTHVAR_ENDPOINT_REQUIRED_FLAGS ||
	    !endpoint->generation || !endpoint->communication_base ||
	    endpoint->communication_base > UINTPTR_MAX ||
	    endpoint->communication_base & (sizeof(uint64_t) - 1U) ||
	    endpoint->communication_size <
		PAYLOAD_MM_AUTHVAR_SERVICE_MIN_MESSAGE_SIZE ||
	    endpoint->communication_size >
		PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE ||
	    endpoint->message_size != endpoint->communication_size ||
	    endpoint->transport != LB_AUTHVAR_ENDPOINT_TRANSPORT_APM_IO8 ||
	    endpoint->trigger_width != sizeof(uint8_t) ||
	    !endpoint->trigger_address || endpoint->trigger_address > UINT16_MAX ||
	    !endpoint->trigger_value || endpoint->trigger_value > UINT8_MAX ||
	    endpoint->maximum_name_size < 2U * sizeof(uint16_t) ||
	    endpoint->maximum_name_size & 1U ||
	    !endpoint->maximum_data_size || endpoint->reserved)
		return CB_ERR;
	if (!message_layout_valid(endpoint, &required) ||
	    endpoint->message_size - 1U > UINTPTR_MAX -
		(uintptr_t)endpoint->communication_base)
		return CB_ERR;
	return CB_SUCCESS;
}

static bool pending_result_valid(
	const struct payload_mm_authvar_service_frame *frame)
{
	return frame->status == PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING &&
		!frame->maximum_storage && !frame->remaining_storage &&
		!frame->maximum_variable && !frame->result_name_size &&
		!frame->result_data_size && !frame->result_attributes &&
		bytes_zero(frame->result_vendor_guid,
			sizeof(frame->result_vendor_guid)) &&
		!frame->reserved[0] && !frame->reserved[1] &&
		frame->completion == PAYLOAD_MM_AUTHVAR_SERVICE_PENDING;
}

enum cb_err payload_mm_authvar_service_request_validate(
	const struct lb_authvar_service_endpoint *endpoint, const void *message,
	size_t message_size)
{
	const struct payload_mm_authvar_service_frame *frame = message;
	bool guid_zero;

	if (payload_mm_authvar_service_endpoint_validate(endpoint) != CB_SUCCESS ||
	    !message || message_size != endpoint->message_size ||
	    frame->revision != PAYLOAD_MM_AUTHVAR_SERVICE_REVISION ||
	    frame->header_size != sizeof(*frame) || !frame->request_id ||
	    frame->generation != endpoint->generation || frame->flags ||
	    frame->reserved0 || !pending_result_valid(frame) ||
	    frame->attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED ||
	    frame->name_size > endpoint->maximum_name_size ||
	    frame->data_size > endpoint->maximum_data_size ||
	    frame->name_capacity > endpoint->maximum_name_size ||
	    frame->data_capacity > endpoint->maximum_data_size)
		return CB_ERR;
	guid_zero = bytes_zero(frame->vendor_guid, sizeof(frame->vendor_guid));
	switch (frame->operation) {
	case PAYLOAD_MM_AUTHVAR_SERVICE_GET:
		if (guid_zero || !name_valid(endpoint, message, frame->name_size) ||
		    frame->attributes || frame->data_size || frame->name_capacity)
			return CB_ERR;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_NEXT:
		if (frame->attributes || frame->data_size || frame->data_capacity ||
		    frame->name_capacity < sizeof(uint16_t) ||
		    frame->name_size > frame->name_capacity ||
		    (frame->name_size ?
		     (guid_zero || !name_valid(endpoint, message, frame->name_size)) :
		     !guid_zero))
			return CB_ERR;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_SET:
		if (guid_zero || !name_valid(endpoint, message, frame->name_size) ||
		    frame->name_capacity || frame->data_capacity ||
		    (!frame->attributes && frame->data_size))
			return CB_ERR;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_QUERY:
		if (!guid_zero || !frame->attributes ||
		    frame->attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE ||
		    frame->name_size || frame->data_size || frame->name_capacity ||
		    frame->data_capacity)
			return CB_ERR;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT:
	case PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME:
		if (!guid_zero || frame->attributes || frame->name_size ||
		    frame->data_size || frame->name_capacity || frame->data_capacity)
			return CB_ERR;
		break;
	default:
		return CB_ERR;
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
		!memcmp(request->vendor_guid, response->vendor_guid,
			sizeof(request->vendor_guid)) &&
		request->attributes == response->attributes &&
		request->name_size == response->name_size &&
		request->data_size == response->data_size &&
		request->name_capacity == response->name_capacity &&
		request->data_capacity == response->data_capacity &&
		response->reserved0 == 0;
}

enum cb_err payload_mm_authvar_service_response_validate(
	const struct lb_authvar_service_endpoint *endpoint, const void *request,
	const void *response, size_t message_size)
{
	const struct payload_mm_authvar_service_frame *before = request;
	const struct payload_mm_authvar_service_frame *after = response;

	if (payload_mm_authvar_service_request_validate(endpoint, request,
		message_size) != CB_SUCCESS || !response ||
	    message_size != endpoint->message_size ||
	    !request_identity_equal(before, after) ||
	    after->status == PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING ||
	    after->completion != PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE ||
	    after->reserved[0] || after->reserved[1] ||
	    after->result_name_size > endpoint->maximum_name_size ||
	    after->result_data_size > endpoint->maximum_data_size ||
	    after->result_attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED)
		return CB_ERR;
	switch (after->operation) {
	case PAYLOAD_MM_AUTHVAR_SERVICE_GET:
		if (after->result_name_size || after->maximum_storage ||
		    after->remaining_storage || after->maximum_variable ||
		    !bytes_zero(after->result_vendor_guid,
			sizeof(after->result_vendor_guid)))
			return CB_ERR;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_NEXT:
		if (after->result_data_size || after->result_attributes ||
		    after->maximum_storage || after->remaining_storage ||
		    after->maximum_variable ||
		    (after->result_name_size ?
		     (bytes_zero(after->result_vendor_guid,
			sizeof(after->result_vendor_guid)) ||
		      !name_valid(endpoint, response, after->result_name_size)) :
		     !bytes_zero(after->result_vendor_guid,
			sizeof(after->result_vendor_guid))))
			return CB_ERR;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_SET:
	case PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT:
	case PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME:
		if (after->result_name_size || after->result_data_size ||
		    after->result_attributes || after->maximum_storage ||
		    after->remaining_storage || after->maximum_variable ||
		    !bytes_zero(after->result_vendor_guid,
			sizeof(after->result_vendor_guid)))
			return CB_ERR;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_QUERY:
		if (after->result_name_size || after->result_data_size ||
		    after->result_attributes ||
		    !bytes_zero(after->result_vendor_guid,
			sizeof(after->result_vendor_guid)))
			return CB_ERR;
		break;
	default:
		return CB_ERR;
	}
	return CB_SUCCESS;
}
