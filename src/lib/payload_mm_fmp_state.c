/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar.h>
#include <commonlib/helpers.h>
#include <stdint.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"
#include "payload_mm_fmp_state_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM FMP state policy must only be built in SMM"
#endif

static struct {
	struct payload_mm_fmp_state_policy policy;
	uint64_t last_transaction;
	bool installed;
	bool install_attempted;
	bool closed;
} state_authority;

static bool guid_present(const guid_t *guid)
{
	uint8_t bits = 0;

	for (size_t i = 0; i < sizeof(guid->b); i++)
		bits |= guid->b[i];
	return bits != 0;
}

bool payload_mm_fmp_state_staging_buffer(const void *buffer, size_t size)
{
	return payload_mm_authvar_smram_buffer(buffer, size) &&
		!payload_mm_authvar_buffers_overlap(buffer, size, &state_authority,
			sizeof(state_authority));
}

static bool bytes_zero(const uint8_t *data, size_t size)
{
	uint8_t bits = 0;

	for (size_t i = 0; i < size; i++)
		bits |= data[i];
	return bits == 0;
}

static uint32_t read32(const uint8_t *data)
{
	return data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 |
		(uint32_t)data[3] << 24;
}

static void write32(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

static bool state_canonical(const uint8_t state[PAYLOAD_MM_FMP_STATE_WIRE_SIZE])
{
	for (size_t i = 0; i < 4; i++)
		if (state[i] > 1)
			return false;
	return true;
}

static bool state_transition_valid(const uint8_t *current,
	const uint8_t candidate[PAYLOAD_MM_FMP_STATE_WIRE_SIZE])
{
	if (!state_canonical(candidate))
		return false;
	if (current != NULL) {
		if (!state_canonical(current))
			return false;
		for (size_t i = 0; i < 4; i++)
			if (current[i] && !candidate[i])
				return false;
		if (current[1] && read32(candidate + 8) < read32(current + 8))
			return false;
	}
	return true;
}

static const char *key_name(uint32_t key)
{
	static const char *const names[] = {
		[PAYLOAD_MM_FMP_STATE_KEY_STATE] = "FmpState",
		[PAYLOAD_MM_FMP_STATE_KEY_VERSION] = "FmpVersion",
		[PAYLOAD_MM_FMP_STATE_KEY_LOWEST_VERSION] = "FmpLsv",
		[PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_STATUS] =
			"LastAttemptStatus",
		[PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION] =
			"LastAttemptVersion",
	};

	return key < ARRAY_SIZE(names) ? names[key] : NULL;
}

static bool build_name(struct payload_mm_fmp_state_command *command)
{
	static const char hex[] = "0123456789ABCDEF";
	const char *base = key_name(command->message.key);
	size_t length = 0;

	if (base == NULL)
		return false;
	while (base[length] != '\0') {
		if (length + 1 >= ARRAY_SIZE(command->variable_name))
			return false;
		command->variable_name[length] = (uint8_t)base[length];
		length++;
	}
	if (state_authority.policy.hardware_instance != 0) {
		if (length + 16 >= ARRAY_SIZE(command->variable_name))
			return false;
		for (size_t i = 0; i < 16; i++) {
			unsigned int shift = (unsigned int)(15U - i) * 4U;

			command->variable_name[length + i] =
				(uint16_t)hex[(state_authority.policy.hardware_instance >>
					shift) & 0xfU];
		}
		length += 16;
	}
	command->variable_name[length] = 0;
	command->variable_name_bytes =
		(uint32_t)((length + 1U) * sizeof(uint16_t));
	return true;
}

static bool build_identity_name(struct payload_mm_fmp_state_identity *identity)
{
	struct payload_mm_fmp_state_command command = {
		.message.key = PAYLOAD_MM_FMP_STATE_KEY_STATE,
	};

	if (!build_name(&command))
		return false;
	identity->variable_name_bytes = command.variable_name_bytes;
	memcpy(identity->variable_name, command.variable_name,
		sizeof(identity->variable_name));
	return true;
}

bool payload_mm_fmp_state_authority_ready(void)
{
	return state_authority.installed && !state_authority.closed;
}

enum cb_err payload_mm_fmp_state_identity_get(
	struct payload_mm_fmp_state_identity *identity)
{
	if (!payload_mm_fmp_state_authority_ready() || !identity)
		return CB_ERR;
	memset(identity, 0, sizeof(*identity));
	identity->namespace_guid = state_authority.policy.namespace_guid;
	identity->hardware_instance = state_authority.policy.hardware_instance;
	identity->trusted_lowest_version =
		state_authority.policy.trusted_lowest_version;
	return build_identity_name(identity) ? CB_SUCCESS : CB_ERR;
}

enum cb_err payload_mm_fmp_state_checkpoint_build(
	const uint8_t current[PAYLOAD_MM_FMP_STATE_WIRE_SIZE],
	uint32_t attempted_version, struct payload_mm_fmp_state_identity *identity,
	uint8_t candidate[PAYLOAD_MM_FMP_STATE_WIRE_SIZE])
{
	uint32_t lowest_version;

	if (!payload_mm_fmp_state_authority_ready() || !current || !identity ||
	    !candidate || !state_canonical(current))
		return CB_ERR;
	lowest_version = state_authority.policy.trusted_lowest_version;
	if (current[1] && read32(current + 8) > lowest_version)
		lowest_version = read32(current + 8);
	if (attempted_version < lowest_version)
		return CB_ERR;
	if (payload_mm_fmp_state_identity_get(identity) != CB_SUCCESS)
		return CB_ERR;
	memcpy(candidate, current, PAYLOAD_MM_FMP_STATE_WIRE_SIZE);
	candidate[2] = 1;
	candidate[3] = 1;
	write32(candidate + 12, 1U);
	write32(candidate + 16, attempted_version);
	return CB_SUCCESS;
}

enum cb_err payload_mm_fmp_state_policy_install(
	const struct payload_mm_fmp_state_policy *trusted_policy,
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	struct payload_mm_fmp_state_policy snapshot;

	if (state_authority.install_attempted)
		return CB_ERR;
	state_authority.install_attempted = true;
	if (!payload_mm_authvar_authority_ready() || !trusted_policy ||
	    !storage_is_protected ||
	    !storage_is_protected(context, &state_authority,
		    sizeof(state_authority)))
		return CB_ERR;
	memcpy(&snapshot, trusted_policy, sizeof(snapshot));
	if (snapshot.revision != PAYLOAD_MM_FMP_STATE_POLICY_REVISION ||
	    snapshot.size != sizeof(snapshot) || snapshot.reserved ||
	    !guid_present(&snapshot.namespace_guid))
		return CB_ERR;
	state_authority.policy = snapshot;
	state_authority.installed = true;
	return CB_SUCCESS;
}

static bool message_shape_valid(const struct payload_mm_fmp_state_message *message,
	const uint8_t *current)
{
	switch (message->operation) {
	case PAYLOAD_MM_FMP_STATE_READ:
		return key_name(message->key) != NULL && !message->attributes &&
			message->data_size ==
			(message->key == PAYLOAD_MM_FMP_STATE_KEY_STATE ?
			 PAYLOAD_MM_FMP_STATE_WIRE_SIZE : sizeof(uint32_t)) &&
			bytes_zero(message->data, sizeof(message->data));
	case PAYLOAD_MM_FMP_STATE_WRITE_STATE:
		return message->key == PAYLOAD_MM_FMP_STATE_KEY_STATE &&
			message->attributes ==
			PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES &&
			message->data_size == PAYLOAD_MM_FMP_STATE_WIRE_SIZE &&
			state_transition_valid(current, message->data);
	case PAYLOAD_MM_FMP_STATE_REMOVE_LEGACY:
		return message->key >= PAYLOAD_MM_FMP_STATE_KEY_VERSION &&
			message->key <=
			PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION &&
			current != NULL && !message->attributes && !message->data_size &&
			bytes_zero(message->data, sizeof(message->data));
	case PAYLOAD_MM_FMP_STATE_CLOSE_STATE:
		return message->key == PAYLOAD_MM_FMP_STATE_KEY_NONE &&
			!message->attributes && !message->data_size &&
			bytes_zero(message->data, sizeof(message->data));
	default:
		return false;
	}
}

enum cb_err payload_mm_fmp_state_command_prepare(const void *trusted_message,
	size_t trusted_message_size, const void *current_state,
	size_t current_state_size, struct payload_mm_fmp_state_command *command)
{
	struct payload_mm_fmp_state_message message;
	uint8_t current[PAYLOAD_MM_FMP_STATE_WIRE_SIZE];
	const uint8_t *current_pointer = NULL;

	if (!state_authority.installed || state_authority.closed || !command ||
	    !payload_mm_fmp_state_staging_buffer(command, sizeof(*command)))
		return CB_ERR;
	if (!trusted_message ||
	    trusted_message_size != sizeof(struct payload_mm_fmp_state_message) ||
	    (uintptr_t)trusted_message % sizeof(uint64_t) ||
	    !payload_mm_fmp_state_staging_buffer(trusted_message,
		trusted_message_size) ||
	    payload_mm_authvar_buffers_overlap(trusted_message,
		trusted_message_size, command, sizeof(*command)))
		return CB_ERR;
	if ((current_state == NULL) != (current_state_size == 0) ||
	    (current_state != NULL &&
	     (current_state_size != sizeof(current) ||
	      (uintptr_t)current_state % sizeof(uint32_t) ||
	      !payload_mm_fmp_state_staging_buffer(current_state,
		current_state_size) ||
	      payload_mm_authvar_buffers_overlap(current_state, current_state_size,
		trusted_message, trusted_message_size) ||
	      payload_mm_authvar_buffers_overlap(current_state, current_state_size,
		command, sizeof(*command)))))
		return CB_ERR;
	memset(command, 0, sizeof(*command));
	memcpy(&message, trusted_message, sizeof(message));
	if (current_state != NULL) {
		memcpy(current, current_state, sizeof(current));
		current_pointer = current;
		if (!state_canonical(current))
			return CB_ERR;
	}
	if (message.revision != PAYLOAD_MM_FMP_STATE_MESSAGE_REVISION ||
	    message.size != sizeof(message) || !message.transaction ||
	    message.transaction <= state_authority.last_transaction ||
	    message.result != PAYLOAD_MM_FMP_STATE_RESULT_PENDING ||
	    message.reserved || !message_shape_valid(&message, current_pointer))
		return CB_ERR;
	command->message = message;
	command->namespace_guid = state_authority.policy.namespace_guid;
	command->hardware_instance = state_authority.policy.hardware_instance;
	command->trusted_lowest_version =
		state_authority.policy.trusted_lowest_version;
	if (message.operation != PAYLOAD_MM_FMP_STATE_CLOSE_STATE &&
	    !build_name(command)) {
		memset(command, 0, sizeof(*command));
		return CB_ERR;
	}
	state_authority.last_transaction = message.transaction;
	if (message.operation == PAYLOAD_MM_FMP_STATE_CLOSE_STATE)
		state_authority.closed = true;
	return CB_SUCCESS;
}
