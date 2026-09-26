/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence.h>
extern long write(int fd, const void *buffer, unsigned long count);

int main(void)
{
	const uint64_t values[] = {
		sizeof(struct lb_authvar_presence_endpoint),
		offsetof(struct lb_authvar_presence_endpoint, generation),
		offsetof(struct lb_authvar_presence_endpoint, communication_base),
		offsetof(struct lb_authvar_presence_endpoint, transport),
		offsetof(struct lb_authvar_presence_endpoint, action_scope),
		offsetof(struct lb_authvar_presence_endpoint, reserved),
		sizeof(struct payload_mm_authvar_presence_message),
		_Alignof(struct payload_mm_authvar_presence_message),
		offsetof(struct payload_mm_authvar_presence_message, generation),
		offsetof(struct payload_mm_authvar_presence_message, request_id),
		offsetof(struct payload_mm_authvar_presence_message, capability),
		offsetof(struct payload_mm_authvar_presence_message, status),
		offsetof(struct payload_mm_authvar_presence_message, reserved),
		offsetof(struct payload_mm_authvar_presence_message, completion),
		LB_TAG_AUTHVAR_PRESENCE_ENDPOINT,
		LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION,
		LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS,
		LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8,
		LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
		LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE,
		PAYLOAD_MM_AUTHVAR_PRESENCE_REVISION,
		PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
		PAYLOAD_MM_AUTHVAR_PRESENCE_PENDING,
		PAYLOAD_MM_AUTHVAR_PRESENCE_COMPLETE,
	};

	return write(1, values, sizeof(values)) == sizeof(values) ? 0 : 1;
}
