/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/authvar_owner.h>
#include <boot/authvar_policy.h>
#include <boot/payload_mm_authvar_service.h>
#include <commonlib/endian.h>
#include <efi_variable_service.h>
#include <string.h>

#include "native_variables.h"
#include "native_capsule.h"
#include "wire.h"

static uint32_t wire_status(uint64_t status)
{
	return (uint32_t)status | ((status >> 32) & (1U << 31));
}

static bool terminated_name(const void *name, size_t size)
{
	const uint8_t *bytes = name;

	if (size < sizeof(uint16_t) || size % sizeof(uint16_t))
		return false;
	for (size_t offset = 0; offset < size; offset += sizeof(uint16_t))
		if (!read_le16(bytes + offset))
			return offset && offset == size - sizeof(uint16_t);
	return false;
}

static uint64_t policy_get(void *context, const uint16_t *name, const void *guid,
	uint32_t *attributes, size_t *size, void *data)
{
	struct authvar_owner *owner = context;
	const bool runtime = owner->runtime;

	/* Policies may refer to boot-service variables after ExitBootServices. */
	owner->runtime = false;
	const uint64_t status = authvar_owner_get(owner, name, guid, attributes, size, data);
	owner->runtime = runtime;
	return status;
}

static uint64_t set_variable(struct authvar_owner *owner,
	const struct authvar_policy *policy, const struct authvar_fixed_policy *fixed,
	const struct smm_variable_access32 *access, const uint16_t *name, const void *data)
{
	const size_t size = access->data_size;
	size_t payload_size = size;

	if (access->attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED) {
		struct payload_mm_authvar_auth2_view auth;

		if (payload_mm_authvar_auth2_parse(data, size, &auth) !=
		    PAYLOAD_MM_AUTHVAR_FORMAT_OK)
			return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		payload_size = auth.payload.size;
	}
	const uint64_t status = authvar_policy_check(policy, name, &access->guid,
		access->attributes, payload_size, policy_get, owner, NULL);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return status;
	/* Like Fixed MM, trust built-in setup until the first boot handoff.
	 * The SMM owner's retained state, not the request, closes this window.
	 */
	const bool setup_open = !owner->ready && !owner->runtime;
	return authvar_owner_set(owner, name, &access->guid, access->attributes,
		size, data, setup_open, fixed, NULL);
}

static uint64_t access_variable(struct authvar_owner *owner,
	const struct authvar_policy *policy, const struct authvar_fixed_policy *fixed,
	void *data, size_t size, bool write)
{
	struct smm_variable_access32 *access = data;
	uint32_t attributes = 0;
	size_t name_size, data_size;

	if (size < sizeof(*access))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	size -= sizeof(*access);
	name_size = access->name_size;
	data_size = access->data_size;
	void *name = (uint8_t *)access + sizeof(*access);
	if (name_size > size || data_size > size - name_size ||
	    !terminated_name(name, name_size))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;

	void *value = (uint8_t *)name + name_size;
	if (write)
		return set_variable(owner, policy, fixed, access, name, value);
	uint64_t status = authvar_owner_get(owner, name, &access->guid, &attributes,
		&data_size, value);
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
	    status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL) {
		access->attributes = attributes;
		access->data_size = data_size;
	}
	return status;
}

static uint64_t next_variable(struct authvar_owner *owner, void *data, size_t size)
{
	struct smm_variable_next32 *next = data;
	size_t name_size;

	if (size < sizeof(*next))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	name_size = next->name_size;
	if (name_size > size - sizeof(*next) || name_size < sizeof(uint16_t) ||
	    name_size % sizeof(uint16_t))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	void *name = (uint8_t *)next + sizeof(*next);
	const uint64_t status = authvar_owner_next(owner, &name_size, name, &next->guid);

	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
	    status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL)
		next->name_size = name_size;
	return status;
}

static uint64_t lock_variable(struct authvar_owner *owner, struct authvar_policy *policy,
	const void *data, size_t size)
{
	const struct smm_variable_next32 *request = data;
	struct {
		struct authvar_policy_entry entry;
		uint16_t name[(AUTHVAR_POLICY_BYTES - sizeof(struct authvar_policy_entry)) / 2];
	} lock = {
		.entry = {
			.version = AUTHVAR_POLICY_REVISION,
			.offset_to_name = sizeof(lock.entry),
			.max_size = UINT32_MAX,
			.lock_policy_type = 1,
		},
	};

	if (policy->locked || owner->ready || owner->runtime)
		return PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED;
	if (size < sizeof(*request) || request->name_size != size - sizeof(*request) ||
	    request->name_size > sizeof(lock.name) ||
	    !terminated_name((const uint8_t *)data + sizeof(*request), request->name_size))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;

	lock.entry.size = sizeof(lock.entry) + request->name_size;
	memcpy(lock.entry.name_space, &request->guid, sizeof(request->guid));
	memcpy(lock.name, (const uint8_t *)data + sizeof(*request), request->name_size);
	uint64_t status = authvar_policy_register(policy, &lock.entry);
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_ALREADY_STARTED) {
		status = authvar_policy_check(policy, lock.name, lock.entry.name_space,
			0, 0, policy_get, owner, NULL);
		return status == PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED ?
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS : PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED;
	}
	return status;
}

static uint64_t query_variables(struct authvar_owner *owner, void *data, size_t size)
{
	struct smm_variable_query32 *query = data;
	uint64_t maximum, remaining, variable;

	if (size != sizeof(*query))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	const uint64_t status = authvar_owner_query(owner, query->attributes,
		&maximum, &remaining, &variable);
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
		query->maximum_storage = maximum;
		query->remaining_storage = remaining;
		query->maximum_variable = variable;
	}
	return status;
}

bool ass_native_variable_dispatch(struct authvar_owner *owner,
	struct authvar_policy *policy, const struct authvar_fixed_policy *fixed,
	void *message, size_t size)
{
	struct smm_variable_header32 *variable = message;
	uint64_t status;

	if (!owner || !policy || !message || (uintptr_t)message % _Alignof(uint16_t) ||
	    size < sizeof(*variable))
		return false;
	size -= sizeof(*variable);
	switch (variable->function) {
	case SMM_VARIABLE_FUNCTION_GET_VARIABLE:
	case SMM_VARIABLE_FUNCTION_SET_VARIABLE:
		status = access_variable(owner, policy, fixed, variable->data, size,
			variable->function == SMM_VARIABLE_FUNCTION_SET_VARIABLE);
		break;
	case SMM_VARIABLE_FUNCTION_GET_NEXT_VARIABLE_NAME:
		status = next_variable(owner, variable->data, size);
		break;
	case SMM_VARIABLE_FUNCTION_QUERY_VARIABLE_INFO:
		status = query_variables(owner, variable->data, size);
		break;
	case SMM_VARIABLE_FUNCTION_LOCK_VARIABLE:
		status = lock_variable(owner, policy, variable->data, size);
		break;
	case SMM_VARIABLE_FUNCTION_READY_TO_BOOT:
		if (size) {
			status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		} else if (owner->runtime) {
			status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
		} else {
			policy->locked = true;
			status = authvar_owner_ready(owner, fixed);
		}
		break;
	case SMM_VARIABLE_FUNCTION_EXIT_BOOT_SERVICE:
		if (size) {
			status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		} else if (!owner->ready || owner->failed || owner->restart_required) {
			status = PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED;
		} else {
			authvar_owner_enter_runtime(owner);
			status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		}
		break;
	case SMM_VARIABLE_FUNCTION_GET_PAYLOAD_SIZE:
		if (size != sizeof(uint32_t)) {
			status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		} else {
			write_le32(variable->data, EFI_VARIABLE_SERVICE_MESSAGE_CAPACITY -
				sizeof(struct mm_communication_header32) - sizeof(*variable));
			status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		}
		break;
	default:
		status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	}
	/* The wire uses the IA32 EFI status error bit even for an x86-64 client. */
	variable->return_status = wire_status(status);
	return true;
}

static uint64_t dispatch_policy(struct authvar_policy *policy, void *data, size_t size)
{
	struct smm_variable_policy32 *message = data;
	uint64_t status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;

	if (size < sizeof(*message) || size > 16 * 1024 ||
	    message->signature != ASS_VARIABLE_POLICY_SIGNATURE ||
	    message->revision != ASS_VARIABLE_POLICY_REVISION)
		return status;
	size -= sizeof(*message);
	switch (message->command) {
	case ASS_POLICY_ENABLED:
		if (size == 1) {
			message->data[0] = 1;
			status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		}
		break;
	case ASS_POLICY_REGISTER: {
		const struct authvar_policy_entry *entry = (const void *)message->data;

		if (size >= sizeof(*entry) && entry->size == size)
			status = authvar_policy_register(policy, entry);
		break;
	}
	case ASS_POLICY_LOCK:
		if (!size) {
			status = policy->locked ? PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED :
				PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
			policy->locked = true;
		}
		break;
	case ASS_POLICY_DISABLE:
		if (!size)
			status = policy->locked ? PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED :
				PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
		break;
	default:
		status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	}
	message->result = wire_status(status);
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static uint32_t dispatch_fixed_policy(struct authvar_owner *owner, uint8_t *data, size_t size)
{
	enum { PREPARE, READY, READ_PK, READ_KEK, READ_DB, READ_DBX };
	static const EFI_GUID global = {
		0x8be4df61, 0x93ca, 0x11d2, { 0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c }
	};
	static const EFI_GUID image = {
		0xd719b2cb, 0x3d3a, 0x4596, { 0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f }
	};
	static const uint16_t keys[][4] = {
		{ 'P', 'K' }, { 'K', 'E', 'K' }, { 'd', 'b' }, { 'd', 'b', 'x' },
	};
	const size_t header_size = 12;
	const uint64_t not_ready = PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 6U;
	struct payload_mm_authvar_view view;
	uint64_t status;
	size_t length = 0;

	if (size < header_size)
		return wire_status(PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	const uint32_t command = read_le32(data);
	if (!owner->fixed_keys) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	} else if (owner->restart_required) {
		status = not_ready;
	} else if ((status = authvar_owner_view(owner, &view))) {
		/* A failed owner cannot attest to fixed policy. */
	} else if (command == PREPARE || command == READY) {
		if (size != header_size || owner->runtime)
			status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		else if (command == PREPARE)
			owner->fixed_policy_prepared = true;
		else if (!owner->fixed_policy_prepared)
			status = not_ready;
	} else if (command >= READ_PK && command <= READ_DBX) {
		if (!owner->fixed_policy_prepared) {
			status = not_ready;
		} else {
			length = size - header_size;
			status = authvar_owner_get(owner, keys[command - READ_PK],
				command <= READ_KEK ? &global : &image, NULL, &length,
				data + header_size);
			if (status && status != PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL)
				length = 0;
		}
	} else {
		status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	}
	write_le32(data + 4, wire_status(status));
	write_le32(data + 8, length);
	return 0;
}

static uint64_t dispatch_event(struct authvar_owner *owner, struct authvar_policy *policy,
	const struct authvar_fixed_policy *fixed, enum ass_mm_phase *phase,
	const struct mm_communication_header32 *wire)
{
	static const EFI_GUID events[] = {
		ASS_END_OF_DXE_GUID, ASS_READY_TO_LOCK_GUID,
		ASS_READY_TO_BOOT_GUID, ASS_EXIT_BOOT_SERVICES_GUID,
	};
	size_t event;

	for (event = 0; event < ARRAY_SIZE(events); event++)
		if (!memcmp(&wire->guid, &events[event], sizeof(wire->guid)))
			break;
	if (event == ARRAY_SIZE(events))
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	if (wire->message_length != 1 || wire->data[0])
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (owner->failed || owner->restart_required)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (event < *phase)
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	/* EDK2 can exit from a built-in image without signalling ReadyToBoot. */
	if (event == ASS_MM_WAIT_EXIT_BOOT_SERVICES && *phase == ASS_MM_WAIT_READY_TO_BOOT) {
		uint64_t status = authvar_owner_ready(owner, fixed);

		if (status)
			return status;
		*phase = ASS_MM_WAIT_EXIT_BOOT_SERVICES;
	}
	if (event != *phase)
		return PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED;
	switch (*phase) {
	case ASS_MM_WAIT_END_OF_DXE:
		policy->locked = true;
		break;
	case ASS_MM_WAIT_READY_TO_LOCK:
		break;
	case ASS_MM_WAIT_READY_TO_BOOT: {
		uint64_t status = authvar_owner_ready(owner, fixed);

		if (status)
			return status;
		break;
	}
	case ASS_MM_WAIT_EXIT_BOOT_SERVICES:
		authvar_owner_enter_runtime(owner);
		break;
	case ASS_MM_RUNTIME:
		return PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED;
	}
	(*phase)++;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint32_t ass_native_mm_dispatch(struct authvar_owner *owner,
	struct authvar_policy *policy, const struct authvar_fixed_policy *fixed,
	enum ass_mm_phase *phase, const struct ass_mm_capsule *capsule, void *message,
	size_t size)
{
	const EFI_GUID variable_guid = ASS_VARIABLE_GUID;
	const EFI_GUID ready_guid = ASS_SERVICE_READY_GUID;
	const EFI_GUID policy_guid = ASS_VARIABLE_POLICY_GUID;
	const EFI_GUID fixed_guid = {
		0xcc4b3ec3, 0x6cce, 0x4a6a, { 0xa6, 0x73, 0xc4, 0xb4, 0x4f, 0x74, 0x45, 0x61 }
	};
	const uint8_t capsule_guid[] = ASS_CAPSULE_GUID_BYTES;
	struct mm_communication_header32 *wire = message;

	if (!owner || !policy || !phase || !message || size < sizeof(*wire) ||
	    size > EFI_VARIABLE_SERVICE_MESSAGE_CAPACITY ||
	    wire->message_length > size - sizeof(*wire))
		return wire_status(PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	if (!memcmp(&wire->guid, &variable_guid, sizeof(variable_guid))) {
		if (!ass_native_variable_dispatch(owner, policy, fixed, wire->data,
		    wire->message_length))
			return wire_status(PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		return 0;
	}
	if (!memcmp(&wire->guid, &ready_guid, sizeof(ready_guid))) {
		struct payload_mm_authvar_view view;

		/* Byte 1 is the firmware-private manage query, not an SMI request. */
		if (wire->message_length != 1 || wire->data[0])
			return wire_status(PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		return wire_status(authvar_owner_view(owner, &view));
	}
	if (!memcmp(&wire->guid, &policy_guid, sizeof(policy_guid))) {
		if ((uintptr_t)wire->data % _Alignof(uint16_t))
			return wire_status(PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		return wire_status(dispatch_policy(policy, wire->data, wire->message_length));
	}
	if (!memcmp(&wire->guid, capsule_guid, sizeof(capsule_guid))) {
		if (!capsule)
			return wire_status(PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
		return ass_native_capsule_dispatch(capsule->transaction, capsule->source,
			owner, capsule->crypto, capsule->platform, wire->data,
			wire->message_length, capsule->image);
	}
	if (!memcmp(&wire->guid, &fixed_guid, sizeof(fixed_guid)))
		return dispatch_fixed_policy(owner, wire->data, wire->message_length);
	const enum ass_mm_phase before = *phase;
	const uint64_t status = dispatch_event(owner, policy, fixed, phase, wire);
	if (!status && capsule && before != ASS_MM_RUNTIME && *phase == ASS_MM_RUNTIME) {
		const struct ass_capsule_platform *platform = capsule->platform;

		if (platform && platform->boot_slot && platform->boot_slot < 3 && platform->commit)
			platform->commit(0);
		ass_native_capsule_close(capsule->transaction, capsule->source);
	}
	return wire_status(status);
}

uint32_t ass_native_mailbox_dispatch(struct authvar_owner *owner,
	struct authvar_policy *policy, const struct authvar_fixed_policy *fixed,
	enum ass_mm_phase *phase, const struct ass_mm_capsule *capsule, void *mailbox,
	size_t mailbox_size,
	void *scratch, size_t scratch_size)
{
	struct efi_variable_service_buffer *buffer = mailbox;
	const uintptr_t shared = (uintptr_t)mailbox, private = (uintptr_t)scratch;
	uint32_t result = EFI_VARIABLE_SERVICE_RET_FAILURE;

	if (!buffer || !scratch || shared % _Alignof(uint32_t) ||
	    private % _Alignof(uint16_t) || mailbox_size != EFI_VARIABLE_SERVICE_BUFFER_SIZE ||
	    (private >= shared ? private - shared < mailbox_size :
				 shared - private < scratch_size))
		return result;
	if (__atomic_load_n(&buffer->state, __ATOMIC_ACQUIRE) !=
	    EFI_VARIABLE_SERVICE_BUFFER_REQUEST)
		return result;

	const uint32_t transaction = buffer->transaction;
	const uint32_t size = buffer->message_size;
	if (buffer->signature != EFI_VARIABLE_SERVICE_BUFFER_SIGNATURE ||
	    buffer->revision != EFI_VARIABLE_SERVICE_REVISION ||
	    buffer->header_size != EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE ||
	    size < sizeof(struct mm_communication_header32) ||
	    size > EFI_VARIABLE_SERVICE_MESSAGE_CAPACITY || size > scratch_size)
		return result;

	void *message = (uint8_t *)mailbox + EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE;
	memcpy(scratch, message, size);
	if (buffer->transaction == transaction &&
	    __atomic_load_n(&buffer->state, __ATOMIC_ACQUIRE) ==
		EFI_VARIABLE_SERVICE_BUFFER_REQUEST) {
		const uint32_t status =
			ass_native_mm_dispatch(owner, policy, fixed, phase, capsule, scratch, size);

		if (buffer->transaction == transaction &&
		    __atomic_load_n(&buffer->state, __ATOMIC_ACQUIRE) ==
			EFI_VARIABLE_SERVICE_BUFFER_REQUEST) {
			memcpy(message, scratch, size);
			buffer->status = status;
			buffer->response_size = size;
			__atomic_store_n(&buffer->state, EFI_VARIABLE_SERVICE_BUFFER_RESPONSE,
				__ATOMIC_RELEASE);
			result = EFI_VARIABLE_SERVICE_RET_SUCCESS;
		}
	}
	for (volatile uint8_t *p = scratch; p < (uint8_t *)scratch + size; p++)
		*p = 0;
	return result;
}
