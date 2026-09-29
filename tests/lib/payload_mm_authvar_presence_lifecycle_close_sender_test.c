/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_transport.h>
#include <cpu/x86/smm_command.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static uint8_t backing[PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE]
	__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_ALIGNMENT);
static struct lb_authvar_presence_lifecycle_close_endpoint endpoint;
static unsigned int triggers;
static bool fail_response;
static bool mutate_endpoint;

void __noreturn
platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop(void)
{
	abort();
}

uint64_t payload_mm_authvar_presence_lifecycle_close_trigger_test(uint64_t value)
{
	struct payload_mm_authvar_presence_lifecycle_close_message *message =
		(void *)backing;

	assert(value ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SENTINEL);
	assert(message->completion ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING);
	triggers++;
	if (mutate_endpoint)
		endpoint.generation++;
	if (fail_response)
		return 0;
	message->status =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SUCCESS;
	__atomic_store_n(&message->completion,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPLETE,
		__ATOMIC_RELEASE);
	return PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SUCCESS;
}

static void reset(void)
{
	payload_mm_authvar_presence_lifecycle_close_sender_reset_test();
	memset(backing, 0xa5, sizeof(backing));
	endpoint = (struct lb_authvar_presence_lifecycle_close_endpoint) {
		.tag = LB_TAG_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT,
		.size = sizeof(endpoint),
		.revision = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT_REVISION,
		.header_size = sizeof(endpoint),
		.flags = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_REQUIRED_FLAGS,
		.generation = 7,
		.communication_base = (uintptr_t)backing,
		.communication_size =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
		.message_size =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
		.transport =
			LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT_APM_IO8,
		.trigger_width = 1,
		.trigger_address = 0xb2,
		.trigger_value = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		.source_mask = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_SOURCE_MASK,
	};
	triggers = 0;
	fail_response = false;
	mutate_endpoint = false;
}

static void assert_public_frame_scrubbed(void)
{
	uint8_t zero[PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE] = { 0 };

	assert(!memcmp(backing, zero, sizeof(zero)));
	for (size_t offset = sizeof(zero); offset < sizeof(backing); offset++)
		assert(backing[offset] == 0xa5);
}

int main(void)
{
	static const uint32_t sources[] = {
		LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PRE_EXTERNAL_IMAGE,
		LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_REPROOF,
		LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PAYLOAD_FAILURE_OR_RETURN,
	};

	reset();
	for (size_t index = 0; index < ARRAY_SIZE(sources); index++) {
		assert(payload_mm_authvar_presence_lifecycle_close_send(&endpoint,
			sources[index]) == CB_SUCCESS);
		assert_public_frame_scrubbed();
		memset(backing, 0xa5, sizeof(backing));
	}
	assert(triggers == ARRAY_SIZE(sources));
	assert(payload_mm_authvar_presence_lifecycle_close_send(&endpoint,
		sources[0]) == CB_ERR);
	assert(triggers == ARRAY_SIZE(sources));

	reset();
	fail_response = true;
	assert(payload_mm_authvar_presence_lifecycle_close_send(&endpoint,
		sources[0]) == CB_ERR);
	assert(triggers == 1);
	assert(payload_mm_authvar_presence_lifecycle_close_send(&endpoint,
		sources[1]) == CB_ERR);
	assert(triggers == 1);

	reset();
	mutate_endpoint = true;
	assert(payload_mm_authvar_presence_lifecycle_close_send(&endpoint,
		sources[0]) == CB_ERR);
	assert(triggers == 1);
	assert(payload_mm_authvar_presence_lifecycle_close_send(&endpoint,
		sources[1]) == CB_ERR);
	assert(triggers == 1);

	reset();
	endpoint.communication_size++;
	assert(payload_mm_authvar_presence_lifecycle_close_send(&endpoint,
		sources[0]) == CB_ERR);
	assert(!triggers);
	return 0;
}
