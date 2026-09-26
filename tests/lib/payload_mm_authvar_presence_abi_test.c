/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

_Static_assert(PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ERROR_BIT == (1ULL << 63),
	"presence status error bit");
_Static_assert(LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS == 0x3f,
	"presence endpoint required flags");
_Static_assert(PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SUCCESS == 0ULL &&
	PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_UNSUPPORTED == ((1ULL << 63) | 3ULL) &&
	PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_DEVICE_ERROR == ((1ULL << 63) | 7ULL) &&
	PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_WRITE_PROTECTED == ((1ULL << 63) | 8ULL) &&
	PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ACCESS_DENIED == ((1ULL << 63) | 15ULL) &&
	PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SECURITY_VIOLATION ==
		((1ULL << 63) | 26ULL),
	"presence status values");

static const struct lb_authvar_presence_endpoint endpoint = {
	.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT,
	.size = sizeof(endpoint),
	.revision = LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION,
	.header_size = sizeof(endpoint),
	.flags = LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS,
	.generation = 7,
	.communication_base = 0x100000,
	.communication_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
	.message_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
	.transport = LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8,
	.trigger_width = 1,
	.trigger_address = 0xb2,
	.trigger_value = 0xe8,
	.action_scope = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
	.capability_size = LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE,
};

static struct payload_mm_authvar_presence_message request(void)
{
	struct payload_mm_authvar_presence_message message = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_REVISION,
		.size = sizeof(message),
		.action = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
		.generation = endpoint.generation,
		.request_id = 9,
		.status = PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_PENDING,
		.completion = PAYLOAD_MM_AUTHVAR_PRESENCE_PENDING,
	};

	for (size_t index = 0; index < sizeof(message.capability); index++)
		message.capability[index] = (uint8_t)(index + 1U);
	return message;
}

static struct payload_mm_authvar_presence_message response(
	const struct payload_mm_authvar_presence_message *request_message,
	uint64_t status)
{
	struct payload_mm_authvar_presence_message message = *request_message;

	message.status = status;
	message.completion = PAYLOAD_MM_AUTHVAR_PRESENCE_COMPLETE;
	return message;
}

static void endpoint_validation(void)
{
	struct lb_authvar_presence_endpoint changed;
	static const uint32_t required_flags[] = {
		LB_AUTHVAR_PRESENCE_COREBOOT_SMM_OWNER,
		LB_AUTHVAR_PRESENCE_FIXED_COMMUNICATION,
		LB_AUTHVAR_PRESENCE_DMA_PROTECTED,
		LB_AUTHVAR_PRESENCE_CPU_RENDEZVOUS,
		LB_AUTHVAR_PRESENCE_ONE_SHOT_CAPABILITY,
		LB_AUTHVAR_PRESENCE_LIFECYCLE_SEALED,
	};

	assert(payload_mm_authvar_presence_endpoint_validate(&endpoint) == CB_SUCCESS);
#define REJECT(member, value) do { \
	changed = endpoint; \
	changed.member = (value); \
	assert(payload_mm_authvar_presence_endpoint_validate(&changed) == CB_ERR); \
} while (0)
	REJECT(tag, LB_TAG_AUTHVAR_SERVICE_ENDPOINT);
	REJECT(size, sizeof(endpoint) - 1U);
	REJECT(revision, LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION + 1U);
	REJECT(header_size, sizeof(endpoint) - 1U);
	REJECT(flags, endpoint.flags ^ LB_AUTHVAR_PRESENCE_DMA_PROTECTED);
	REJECT(flags, endpoint.flags | (1U << 31));
	REJECT(generation, 0);
	REJECT(communication_base, 0);
	REJECT(communication_base, endpoint.communication_base + 1U);
	REJECT(communication_size, endpoint.communication_size - 1U);
	REJECT(communication_size, endpoint.communication_size + 1U);
	REJECT(message_size, endpoint.message_size - 1U);
	REJECT(message_size, endpoint.message_size + 1U);
	REJECT(transport, LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8 + 1U);
	REJECT(trigger_width, 0);
	REJECT(trigger_width, sizeof(uint16_t));
	REJECT(trigger_address, 0);
	REJECT(trigger_address, UINT16_MAX + 1U);
	REJECT(trigger_value, 0);
	REJECT(trigger_value, UINT8_MAX + 1U);
	REJECT(action_scope, LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE + 1U);
	REJECT(capability_size, LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE - 1U);
	REJECT(capability_size, LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE + 1U);
	REJECT(reserved, 1);
	REJECT(communication_base,
		(uint64_t)(UINTPTR_MAX & ~(uintptr_t)(sizeof(uint64_t) - 1U)));
#if UINTPTR_MAX < UINT64_MAX
	REJECT(communication_base, (uint64_t)UINTPTR_MAX + 1ULL);
#endif
#undef REJECT
	for (size_t index = 0; index < ARRAY_SIZE(required_flags); index++) {
		changed = endpoint;
		changed.flags ^= required_flags[index];
		assert(payload_mm_authvar_presence_endpoint_validate(&changed) == CB_ERR);
	}
}

static void request_validation(void)
{
	struct payload_mm_authvar_presence_message changed;
	struct payload_mm_authvar_presence_message valid = request();
	struct lb_authvar_presence_endpoint bad_endpoint = endpoint;
	uint8_t unaligned[sizeof(valid) + 1U] __aligned(8);

	assert(payload_mm_authvar_presence_request_validate(&endpoint, &valid,
		sizeof(valid)) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_request_validate(NULL, &valid,
		sizeof(valid)) == CB_ERR);
	assert(payload_mm_authvar_presence_request_validate(&endpoint, NULL,
		sizeof(valid)) == CB_ERR);
	memcpy(unaligned + 1U, &valid, sizeof(valid));
	assert(payload_mm_authvar_presence_request_validate(&endpoint, unaligned + 1U,
		sizeof(valid)) == CB_ERR);
	assert(payload_mm_authvar_presence_request_validate(&endpoint, &valid,
		sizeof(valid) - 1U) == CB_ERR);
	bad_endpoint.reserved = 1;
	assert(payload_mm_authvar_presence_request_validate(&bad_endpoint, &valid,
		sizeof(valid)) == CB_ERR);
#define REJECT(member, value) do { \
	changed = valid; \
	changed.member = (value); \
	assert(payload_mm_authvar_presence_request_validate(&endpoint, &changed, \
		sizeof(changed)) == CB_ERR); \
} while (0)
	REJECT(revision, valid.revision + 1U);
	REJECT(size, valid.size - 1U);
	REJECT(action, valid.action + 1U);
	REJECT(flags, 1);
	REJECT(generation, valid.generation + 1U);
	REJECT(request_id, 0);
	REJECT(status, PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SUCCESS);
	REJECT(reserved, 1);
	REJECT(completion, PAYLOAD_MM_AUTHVAR_PRESENCE_COMPLETE);
#undef REJECT
	changed = valid;
	memset(changed.capability, 0, sizeof(changed.capability));
	assert(payload_mm_authvar_presence_request_validate(&endpoint, &changed,
		sizeof(changed)) == CB_ERR);
	for (size_t index = 0; index < sizeof(changed.capability); index++) {
		changed = valid;
		memset(changed.capability, 0, sizeof(changed.capability));
		changed.capability[index] = 1;
		assert(payload_mm_authvar_presence_request_validate(&endpoint, &changed,
			sizeof(changed)) == CB_SUCCESS);
	}
}

static void response_validation(void)
{
	static const uint64_t accepted_statuses[] = {
		PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_UNSUPPORTED,
		PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_DEVICE_ERROR,
		PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_WRITE_PROTECTED,
		PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ACCESS_DENIED,
		PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SECURITY_VIOLATION,
	};
	struct payload_mm_authvar_presence_message valid_request = request();
	struct payload_mm_authvar_presence_message valid_response;
	struct payload_mm_authvar_presence_message changed;
	uint8_t unaligned[sizeof(valid_response) + 1U] __aligned(8);

	for (size_t index = 0; index < ARRAY_SIZE(accepted_statuses); index++) {
		valid_response = response(&valid_request, accepted_statuses[index]);
		assert(payload_mm_authvar_presence_response_validate(&endpoint,
			&valid_request, &valid_response, sizeof(valid_response)) == CB_SUCCESS);
	}
	valid_response = response(&valid_request,
		PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SUCCESS);
	assert(payload_mm_authvar_presence_response_validate(&endpoint,
		&valid_request, NULL, sizeof(valid_response)) == CB_ERR);
	memcpy(unaligned + 1U, &valid_response, sizeof(valid_response));
	assert(payload_mm_authvar_presence_response_validate(&endpoint,
		&valid_request, unaligned + 1U, sizeof(valid_response)) == CB_ERR);
	assert(payload_mm_authvar_presence_response_validate(&endpoint,
		&valid_request, &valid_response, sizeof(valid_response) - 1U) == CB_ERR);
#define REJECT(member, expression) do { \
	changed = valid_response; \
	expression; \
	assert(payload_mm_authvar_presence_response_validate(&endpoint, \
		&valid_request, &changed, sizeof(changed)) == CB_ERR); \
} while (0)
	REJECT(revision, changed.revision++);
	REJECT(size, changed.size--);
	REJECT(action, changed.action++);
	REJECT(flags, changed.flags = 1);
	REJECT(generation, changed.generation++);
	REJECT(request_id, changed.request_id++);
	REJECT(status, changed.status = PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_PENDING);
	REJECT(status, changed.status = PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ERROR_BIT);
	REJECT(reserved, changed.reserved = 1);
	REJECT(completion, changed.completion = PAYLOAD_MM_AUTHVAR_PRESENCE_PENDING);
#undef REJECT
	for (size_t index = 0; index < sizeof(changed.capability); index++) {
		changed = valid_response;
		changed.capability[index] ^= 0xff;
		assert(payload_mm_authvar_presence_response_validate(&endpoint,
			&valid_request, &changed, sizeof(changed)) == CB_ERR);
	}
}

int main(void)
{
	endpoint_validation();
	request_validation();
	response_validation();
	return 0;
}
