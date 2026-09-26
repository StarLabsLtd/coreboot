/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence.h>
#include <string.h>

static bool capability_nonzero(const uint8_t *capability)
{
	uint8_t value = 0;

	for (size_t index = 0; index < LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE; index++)
		value |= capability[index];
	return value != 0;
}

enum cb_err payload_mm_authvar_presence_endpoint_validate(
	const struct lb_authvar_presence_endpoint *endpoint)
{
	if (!endpoint || endpoint->tag != LB_TAG_AUTHVAR_PRESENCE_ENDPOINT ||
	    endpoint->size != sizeof(*endpoint) ||
	    endpoint->revision != LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION ||
	    endpoint->header_size != sizeof(*endpoint) ||
	    endpoint->flags != LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS ||
	    !endpoint->generation || !endpoint->communication_base ||
	    endpoint->communication_base > UINTPTR_MAX ||
	    endpoint->communication_base & (sizeof(uint64_t) - 1U) ||
	    endpoint->communication_size != PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE ||
	    endpoint->message_size != PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE ||
	    endpoint->communication_size - 1U > UINTPTR_MAX -
		(uintptr_t)endpoint->communication_base ||
	    endpoint->transport != LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8 ||
	    endpoint->trigger_width != sizeof(uint8_t) ||
	    !endpoint->trigger_address || endpoint->trigger_address > UINT16_MAX ||
	    !endpoint->trigger_value || endpoint->trigger_value > UINT8_MAX ||
	    endpoint->action_scope != LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE ||
	    endpoint->capability_size != LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE ||
	    endpoint->reserved)
		return CB_ERR;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_request_validate(
	const struct lb_authvar_presence_endpoint *endpoint, const void *message,
	size_t message_size)
{
	const struct payload_mm_authvar_presence_message *request = message;

	if (payload_mm_authvar_presence_endpoint_validate(endpoint) != CB_SUCCESS ||
	    !request || (uintptr_t)request &
		(_Alignof(struct payload_mm_authvar_presence_message) - 1U) ||
	    message_size != endpoint->message_size ||
	    request->revision != PAYLOAD_MM_AUTHVAR_PRESENCE_REVISION ||
	    request->size != sizeof(*request) || request->flags ||
	    request->generation != endpoint->generation || !request->request_id ||
	    request->action != endpoint->action_scope ||
	    !capability_nonzero(request->capability) ||
	    request->status != PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_PENDING ||
	    request->reserved ||
	    request->completion != PAYLOAD_MM_AUTHVAR_PRESENCE_PENDING)
		return CB_ERR;
	return CB_SUCCESS;
}

static bool completion_status_valid(uint64_t status)
{
	switch (status) {
	case PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SUCCESS:
	case PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_UNSUPPORTED:
	case PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_DEVICE_ERROR:
	case PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_WRITE_PROTECTED:
	case PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ACCESS_DENIED:
	case PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SECURITY_VIOLATION:
		return true;
	default:
		return false;
	}
}

enum cb_err payload_mm_authvar_presence_response_validate(
	const struct lb_authvar_presence_endpoint *endpoint, const void *request,
	const void *response, size_t message_size)
{
	const struct payload_mm_authvar_presence_message *request_frame = request;
	const struct payload_mm_authvar_presence_message *response_frame = response;

	if (payload_mm_authvar_presence_request_validate(endpoint, request,
		message_size) != CB_SUCCESS || !response ||
	    (uintptr_t)response &
		(_Alignof(struct payload_mm_authvar_presence_message) - 1U) ||
	    response_frame->revision != request_frame->revision ||
	    response_frame->size != request_frame->size ||
	    response_frame->action != request_frame->action ||
	    response_frame->flags != request_frame->flags ||
	    response_frame->generation != request_frame->generation ||
	    response_frame->request_id != request_frame->request_id ||
	    memcmp(response_frame->capability, request_frame->capability,
		sizeof(request_frame->capability)) || response_frame->reserved ||
	    response_frame->completion != PAYLOAD_MM_AUTHVAR_PRESENCE_COMPLETE ||
	    !completion_status_valid(response_frame->status))
		return CB_ERR;
	return CB_SUCCESS;
}
