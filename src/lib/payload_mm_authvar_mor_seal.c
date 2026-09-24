/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_seal.h>
#include <limits.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "MOR completion seal receiver must only be built in SMM"
#endif

static struct {
	struct payload_mm_authvar_mor_seal_channel channel;
	struct payload_mm_authvar_mor_seal_request candidate;
	payload_mm_authvar_mor_seal_range_check protected_storage;
	payload_mm_authvar_mor_seal_range_check fixed_transport;
	bool channel_attempted;
	bool channel_installed;
	bool request_attempted;
	bool poisoned;
} seal_authority;

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

	if (!object_valid(first, first_size, 1) ||
	    !object_valid(second, second_size, 1))
		return true;
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

static bool channel_snapshot_valid(
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

static bool request_header_valid(
	const struct payload_mm_authvar_mor_seal_request *request,
	const struct payload_mm_authvar_mor_seal_channel *channel)
{
	return request->revision == PAYLOAD_MM_AUTHVAR_MOR_SEAL_REVISION &&
		request->size == sizeof(*request) && !request->reserved &&
		!memcmp(request->capability, channel->capability,
			sizeof(request->capability));
}

static void scrub_transport(void *transport, size_t size)
{
	if (object_valid(transport, size,
		_Alignof(struct payload_mm_authvar_mor_seal_request)) &&
	    size == sizeof(struct payload_mm_authvar_mor_seal_request))
		memset(transport, 0, size);
}

static void forget_channel(bool poisoned)
{
	memset(&seal_authority.channel, 0, sizeof(seal_authority.channel));
	memset(&seal_authority.candidate, 0, sizeof(seal_authority.candidate));
	seal_authority.protected_storage = NULL;
	seal_authority.fixed_transport = NULL;
	seal_authority.channel_installed = false;
	seal_authority.request_attempted = true;
	seal_authority.poisoned = poisoned;
}

static enum cb_err terminal_close(void *transport, size_t size)
{
	scrub_transport(transport, size);
	(void)payload_mm_authvar_mor_grant_close();
	forget_channel(true);
	return CB_ERR;
}

static bool grant_storage_is_protected(void *unused, const void *base,
	size_t size)
{
	struct payload_mm_authvar_mor_seal_channel channel_copy =
		seal_authority.channel;
	struct {
		uint32_t revision;
		uint32_t size;
		uint32_t command;
		uint32_t reserved;
		uint8_t capability[PAYLOAD_MM_AUTHVAR_MOR_SEAL_CAPABILITY_SIZE];
	} header_copy = {
		.revision = seal_authority.candidate.revision,
		.size = seal_authority.candidate.size,
		.command = seal_authority.candidate.command,
		.reserved = seal_authority.candidate.reserved,
	};
	payload_mm_authvar_mor_seal_range_check protected_storage_copy =
		seal_authority.protected_storage;
	payload_mm_authvar_mor_seal_range_check fixed_transport_copy =
		seal_authority.fixed_transport;
	void *transport = (void *)(uintptr_t)channel_copy.transport_base;
	bool protected;

	(void)unused;
	memcpy(header_copy.capability, seal_authority.candidate.capability,
		sizeof(header_copy.capability));
	protected = protected_storage_copy(base, size);
	return protected &&
		!memcmp(&channel_copy, &seal_authority.channel,
			sizeof(channel_copy)) &&
		protected_storage_copy == seal_authority.protected_storage &&
		fixed_transport_copy == seal_authority.fixed_transport &&
		seal_authority.channel_attempted &&
		seal_authority.channel_installed &&
		seal_authority.request_attempted && !seal_authority.poisoned &&
		seal_authority.candidate.revision == header_copy.revision &&
		seal_authority.candidate.size == header_copy.size &&
		seal_authority.candidate.command == header_copy.command &&
		seal_authority.candidate.reserved == header_copy.reserved &&
		!memcmp(seal_authority.candidate.capability,
			header_copy.capability, sizeof(header_copy.capability)) &&
		bytes_zero(transport, channel_copy.transport_size);
}

enum cb_err payload_mm_authvar_mor_seal_channel_install(
	const struct payload_mm_authvar_mor_seal_channel *channel,
	payload_mm_authvar_mor_seal_range_check protected_storage,
	payload_mm_authvar_mor_seal_range_check fixed_transport)
{
	struct payload_mm_authvar_mor_seal_channel snapshot;
	bool protected;
	bool shared;

	if (seal_authority.channel_attempted)
		return CB_ERR;
	seal_authority.channel_attempted = true;
	if (!object_valid(channel, sizeof(*channel), _Alignof(*channel)) ||
	    !protected_storage || !fixed_transport) {
		return terminal_close(NULL, 0);
	}
	memcpy(&snapshot, channel, sizeof(snapshot));
	if (!channel_snapshot_valid(&snapshot) ||
	    ranges_overlap(channel, sizeof(*channel), &seal_authority,
		sizeof(seal_authority)) ||
	    ranges_overlap((const void *)(uintptr_t)snapshot.transport_base,
		snapshot.transport_size, channel, sizeof(*channel)) ||
	    ranges_overlap((const void *)(uintptr_t)snapshot.transport_base,
		snapshot.transport_size, &seal_authority,
		sizeof(seal_authority)))
		return terminal_close(NULL, 0);
	seal_authority.channel = snapshot;
	seal_authority.protected_storage = protected_storage;
	seal_authority.fixed_transport = fixed_transport;
	protected = protected_storage(&seal_authority, sizeof(seal_authority)) &&
		protected_storage(channel, sizeof(*channel)) &&
		protected_storage((const void *)(uintptr_t)protected_storage, 1) &&
		protected_storage((const void *)(uintptr_t)fixed_transport, 1);
	shared = fixed_transport((const void *)(uintptr_t)snapshot.transport_base,
		snapshot.transport_size);
	if (!protected || !shared ||
	    memcmp(channel, &snapshot, sizeof(snapshot)) ||
	    memcmp(&seal_authority.channel, &snapshot, sizeof(snapshot)) ||
	    seal_authority.protected_storage != protected_storage ||
	    seal_authority.fixed_transport != fixed_transport ||
	    seal_authority.channel_installed || seal_authority.request_attempted ||
	    seal_authority.poisoned ||
	    !bytes_zero(&seal_authority.candidate,
		sizeof(seal_authority.candidate)))
		return terminal_close((void *)(uintptr_t)snapshot.transport_base,
			snapshot.transport_size);
	seal_authority.channel_installed = true;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_seal_receive(
	const struct payload_mm_authvar_mor_seal_request *observed_transport,
	size_t observed_size, uint64_t observed_caller,
	uint64_t observed_caller_context)
{
	struct payload_mm_authvar_mor_seal_channel channel_copy;
	struct payload_mm_authvar_mor_seal_request *transport;
	payload_mm_authvar_mor_seal_range_check fixed_transport_copy;
	payload_mm_authvar_mor_seal_range_check protected_storage_copy;
	enum cb_err status;

	if (!seal_authority.channel_installed ||
	    seal_authority.request_attempted || seal_authority.poisoned) {
		return CB_ERR;
	}
	seal_authority.request_attempted = true;
	channel_copy = seal_authority.channel;
	fixed_transport_copy = seal_authority.fixed_transport;
	protected_storage_copy = seal_authority.protected_storage;
	transport = (void *)(uintptr_t)seal_authority.channel.transport_base;
	if (observed_transport != transport ||
	    observed_size != seal_authority.channel.transport_size ||
	    observed_caller != seal_authority.channel.caller ||
	    observed_caller_context != seal_authority.channel.caller_context ||
	    !fixed_transport_copy(transport, sizeof(*transport)) ||
	    memcmp(&channel_copy, &seal_authority.channel, sizeof(channel_copy)) ||
	    fixed_transport_copy != seal_authority.fixed_transport ||
	    protected_storage_copy != seal_authority.protected_storage)
		return terminal_close(transport, channel_copy.transport_size);
	memcpy(&seal_authority.candidate, transport,
		sizeof(seal_authority.candidate));
	memset(transport, 0, sizeof(*transport));
	if (!request_header_valid(&seal_authority.candidate, &channel_copy) ||
	    !bytes_zero(transport, sizeof(*transport)))
		return terminal_close(transport, channel_copy.transport_size);
	if (seal_authority.candidate.command ==
	    PAYLOAD_MM_AUTHVAR_MOR_SEAL_CLOSE) {
		if (!bytes_zero(&seal_authority.candidate.grant,
			sizeof(seal_authority.candidate.grant)))
			return terminal_close(transport, channel_copy.transport_size);
		status = payload_mm_authvar_mor_grant_close();
	} else if (seal_authority.candidate.command ==
		   PAYLOAD_MM_AUTHVAR_MOR_SEAL_INSTALL) {
		status = payload_mm_authvar_mor_grant_install(
			&seal_authority.candidate.grant,
			grant_storage_is_protected, NULL);
	} else {
		return terminal_close(transport, channel_copy.transport_size);
	}
	if (!request_header_valid(&seal_authority.candidate, &channel_copy) ||
	    observed_transport != transport ||
	    observed_size != channel_copy.transport_size ||
	    observed_caller != channel_copy.caller ||
	    observed_caller_context != channel_copy.caller_context ||
	    memcmp(&channel_copy, &seal_authority.channel, sizeof(channel_copy)) ||
	    fixed_transport_copy != seal_authority.fixed_transport ||
	    protected_storage_copy != seal_authority.protected_storage)
		status = CB_ERR;
	if (status != CB_SUCCESS) {
		return terminal_close(transport, channel_copy.transport_size);
	}
	scrub_transport(transport, channel_copy.transport_size);
	forget_channel(false);
	return CB_SUCCESS;
}
