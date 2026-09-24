/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_seal.h>
#include <limits.h>
#include <string.h>

#if ENV_SMM && !ENV_TEST
#error "MOR completion seal sender must not be built in SMM"
#endif

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return !combined;
}

static bool channel_valid(
	const struct payload_mm_authvar_mor_seal_channel *channel)
{
	return channel->transport_base <= UINTPTR_MAX &&
		channel->transport_size ==
			sizeof(struct payload_mm_authvar_mor_seal_request) &&
		object_valid((const void *)(uintptr_t)channel->transport_base,
			channel->transport_size,
			_Alignof(struct payload_mm_authvar_mor_seal_request)) &&
		channel->caller && channel->caller_context &&
		!bytes_zero(channel->capability, sizeof(channel->capability));
}

static enum cb_err send_request(
	struct payload_mm_authvar_mor_seal_channel *channel,
	const struct payload_mm_authvar_mor_grant *grant, uint32_t command,
	payload_mm_authvar_mor_seal_trigger trigger, void *context)
{
	struct payload_mm_authvar_mor_seal_channel channel_copy;
	struct payload_mm_authvar_mor_seal_request candidate = { 0 };
	struct payload_mm_authvar_mor_seal_request *transport;
	struct payload_mm_authvar_mor_grant grant_copy;
	enum cb_err status = CB_ERR;
	bool grant_valid = object_valid(grant, sizeof(*grant), _Alignof(*grant));

	if (!object_valid(channel, sizeof(*channel), _Alignof(*channel)))
		return CB_ERR_ARG;
	memcpy(&channel_copy, channel, sizeof(channel_copy));
	if (!channel_valid(&channel_copy))
		goto scrub_channel;
	transport = (void *)(uintptr_t)channel_copy.transport_base;
	if (ranges_overlap(channel, sizeof(*channel), transport,
		channel_copy.transport_size))
		goto scrub_channel;
	if (!trigger ||
	    (grant_valid &&
	     (ranges_overlap(grant, sizeof(*grant), channel, sizeof(*channel)) ||
	      ranges_overlap(grant, sizeof(*grant), transport,
		channel_copy.transport_size))))
		goto scrub_transport;
	if (grant_valid)
		memcpy(&grant_copy, grant, sizeof(grant_copy));
	else
		memset(&grant_copy, 0, sizeof(grant_copy));
	candidate.revision = PAYLOAD_MM_AUTHVAR_MOR_SEAL_REVISION;
	candidate.size = sizeof(candidate);
	candidate.command = command;
	memcpy(candidate.capability, channel_copy.capability,
		sizeof(candidate.capability));
	if (command == PAYLOAD_MM_AUTHVAR_MOR_SEAL_INSTALL)
		candidate.grant = grant_copy;
	memset(transport, 0, sizeof(*transport));
	memcpy(transport, &candidate, sizeof(candidate));
	if (memcmp(channel, &channel_copy, sizeof(channel_copy)) ||
	    (grant_valid && memcmp(grant, &grant_copy, sizeof(grant_copy))) ||
	    memcmp(transport, &candidate, sizeof(candidate)))
		goto scrub_transport;
	status = trigger(context);
	if (memcmp(channel, &channel_copy, sizeof(channel_copy)) ||
	    (grant_valid && memcmp(grant, &grant_copy, sizeof(grant_copy))) ||
	    !bytes_zero(transport, sizeof(*transport)))
		status = CB_ERR;

scrub_transport:
	memset(transport, 0, sizeof(*transport));
scrub_channel:
	memset(&candidate, 0, sizeof(candidate));
	memset(&grant_copy, 0, sizeof(grant_copy));
	memset(&channel_copy, 0, sizeof(channel_copy));
	memset(channel, 0, sizeof(*channel));
	return status;
}

enum cb_err payload_mm_authvar_mor_seal_send_install(
	struct payload_mm_authvar_mor_seal_channel *channel,
	const struct payload_mm_authvar_mor_grant *grant,
	payload_mm_authvar_mor_seal_trigger trigger, void *context)
{
	return send_request(channel, grant, PAYLOAD_MM_AUTHVAR_MOR_SEAL_INSTALL,
		trigger, context);
}

enum cb_err payload_mm_authvar_mor_seal_send_close(
	struct payload_mm_authvar_mor_seal_channel *channel,
	payload_mm_authvar_mor_seal_trigger trigger, void *context)
{
	return send_request(channel, NULL, PAYLOAD_MM_AUTHVAR_MOR_SEAL_CLOSE,
		trigger, context);
}
