/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker.h>
#include <stdint.h>

#include "capsule_broker_internal.h"

static bool range_addressable(uint64_t base, uint64_t size)
{
	return size && base <= UINTPTR_MAX && size - 1 <= UINTPTR_MAX - base;
}

static bool ranges_overlap(uint64_t left_base, uint64_t left_size,
	uint64_t right_base, uint64_t right_size)
{
	if (!left_size || !right_size || left_base > UINT64_MAX - left_size ||
	    right_base > UINT64_MAX - right_size)
		return true;
	return left_base < right_base + right_size &&
		right_base < left_base + left_size;
}

bool capsule_broker_endpoint_shape_valid(
	const struct lb_capsule_broker_endpoint *endpoint, uint64_t image_size)
{
	uint64_t communication_base;
	uint64_t staging_base;
	uint64_t staging_size;

	if (!endpoint || !image_size ||
	    endpoint->tag != LB_TAG_CAPSULE_BROKER_ENDPOINT ||
	    endpoint->size != sizeof(*endpoint) ||
	    endpoint->revision != LB_CAPSULE_BROKER_ENDPOINT_REVISION ||
	    endpoint->header_size != sizeof(*endpoint) ||
	    endpoint->flags != LB_CAPSULE_ENDPOINT_REQUIRED_FLAGS ||
	    endpoint->communication_size != CAPSULE_BROKER_TRANSPORT_SIZE ||
	    endpoint->message_size != CAPSULE_BROKER_TRANSPORT_SIZE ||
	    endpoint->transport != LB_CAPSULE_ENDPOINT_TRANSPORT_APM_IO8 ||
	    endpoint->trigger_width != sizeof(uint8_t) ||
	    endpoint->trigger_address != CAPSULE_BROKER_APM_PORT ||
	    endpoint->trigger_value != CAPSULE_BROKER_APM_COMMAND ||
	    endpoint->reserved[0] || endpoint->reserved[1] ||
	    endpoint->reserved[2])
		return false;
	communication_base = endpoint->communication_base;
	staging_base = endpoint->staging_base;
	staging_size = endpoint->staging_size;
	return endpoint->generation && communication_base &&
		communication_base % sizeof(uint64_t) == 0 && staging_base &&
		staging_base % sizeof(uint64_t) == 0 && staging_size >= image_size &&
		range_addressable(communication_base,
			endpoint->communication_size) &&
		range_addressable(staging_base, staging_size) &&
		!ranges_overlap(communication_base, endpoint->communication_size,
			staging_base, staging_size);
}

enum cb_err capsule_broker_endpoint_validate(
	const struct lb_capsule_broker_endpoint *endpoint,
	const struct lb_capsule_handoff *handoff)
{
	if (!handoff || handoff->tag != LB_TAG_CAPSULE_HANDOFF ||
	    handoff->revision != LB_CAPSULE_HANDOFF_REVISION ||
	    handoff->header_size != sizeof(*handoff))
		return CB_ERR;
	return capsule_broker_endpoint_shape_valid(endpoint,
		handoff->image_size) ? CB_SUCCESS : CB_ERR;
}
