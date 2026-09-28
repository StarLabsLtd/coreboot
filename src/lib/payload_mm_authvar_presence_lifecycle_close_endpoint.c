/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
#include <string.h>

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;
	return object && size && IS_ALIGNED(base, alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool objects_overlap(const void *left, const void *right, size_t size)
{
	const uintptr_t left_base = (uintptr_t)left;
	const uintptr_t right_base = (uintptr_t)right;
	return left_base <= right_base + size - 1U &&
		right_base <= left_base + size - 1U;
}

static bool source_valid(uint32_t source)
{
	return source && !(source & (source - 1U)) &&
		(source & LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_SOURCE_MASK);
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_endpoint_validate(
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint)
{
	if (!object_valid(endpoint, sizeof(*endpoint), _Alignof(uint32_t)) ||
	    endpoint->tag != LB_TAG_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT ||
	    endpoint->size != sizeof(*endpoint) ||
	    endpoint->revision != LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT_REVISION ||
	    endpoint->header_size != sizeof(*endpoint) ||
	    endpoint->flags != LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_REQUIRED_FLAGS ||
	    !endpoint->generation || !endpoint->communication_base ||
	    endpoint->communication_base >= (1ULL << 32) ||
	    endpoint->communication_base &
		(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_ALIGNMENT - 1U) ||
	    endpoint->communication_size !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE ||
	    endpoint->message_size !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE ||
	    endpoint->transport !=
		LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT_APM_IO8 ||
	    endpoint->trigger_width != sizeof(uint8_t) ||
	    !endpoint->trigger_address || endpoint->trigger_address > UINT16_MAX ||
	    !endpoint->trigger_value || endpoint->trigger_value > UINT8_MAX ||
	    endpoint->source_mask !=
		LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_SOURCE_MASK ||
	    endpoint->reserved[0] || endpoint->reserved[1])
		return CB_ERR;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_request_validate(
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint,
	const void *message, size_t message_size)
{
	const struct payload_mm_authvar_presence_lifecycle_close_message *request = message;

	if (payload_mm_authvar_presence_lifecycle_close_endpoint_validate(endpoint) !=
		CB_SUCCESS || !object_valid(request, sizeof(*request), _Alignof(*request)) ||
	    message_size != endpoint->message_size ||
	    request->revision !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_REVISION ||
	    request->size != sizeof(*request) || !source_valid(request->source) ||
	    !(request->source & endpoint->source_mask) || request->flags ||
	    request->generation != endpoint->generation || !request->request_id ||
	    request->status !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_PENDING ||
	    request->reserved[0] || request->reserved[1] || request->reserved[2] ||
	    request->reserved[3] || request->reserved[4] ||
	    request->completion != PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING)
		return CB_ERR;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_response_validate(
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint,
	const void *request, const void *response, size_t message_size)
{
	const struct payload_mm_authvar_presence_lifecycle_close_message *before = request;
	const struct payload_mm_authvar_presence_lifecycle_close_message *after = response;
	struct payload_mm_authvar_presence_lifecycle_close_message expected;
	struct payload_mm_authvar_presence_lifecycle_close_message snapshot;
	uint32_t completion;

	if (!object_valid(request, sizeof(*before), _Alignof(*before)) ||
	    !object_valid(response, sizeof(*after), _Alignof(*after)) ||
	    objects_overlap(request, response, sizeof(*after)) ||
	    payload_mm_authvar_presence_lifecycle_close_request_validate(endpoint,
		request, message_size) != CB_SUCCESS)
		return CB_ERR;
	completion = __atomic_load_n(&after->completion, __ATOMIC_ACQUIRE);
	if (completion != PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPLETE)
		return CB_ERR;
	snapshot = *after;
	if (__atomic_load_n(&after->completion, __ATOMIC_ACQUIRE) != completion ||
	    (snapshot.status != PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SUCCESS &&
	     snapshot.status != PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_INVALID_PARAMETER &&
	     snapshot.status != PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_UNSUPPORTED &&
	     snapshot.status != PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_DEVICE_ERROR &&
	     snapshot.status != PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_ACCESS_DENIED &&
	     snapshot.status != PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SECURITY_VIOLATION))
		return CB_ERR;
	expected = *before;
	expected.status = snapshot.status;
	expected.completion = completion;
	return memcmp(&expected, &snapshot, sizeof(expected)) ? CB_ERR : CB_SUCCESS;
}
