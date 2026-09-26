/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_transaction.h>
#include <string.h>

static bool binding_valid(
	const struct payload_mm_authvar_presence_transaction_binding *binding)
{
	uint8_t value = 0;

	if (!binding ||
	    binding->revision != PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION ||
	    binding->size != sizeof(*binding) || !binding->generation ||
	    !binding->transaction_id || !binding->nonce || !binding->maximum_cpus ||
	    binding->initiator_cpu >= binding->maximum_cpus || binding->reserved[0] ||
	    binding->reserved[1])
		return false;
	for (size_t index = 0; index < sizeof(binding->capability); index++)
		value |= binding->capability[index];
	return value != 0;
}

uint64_t payload_mm_authvar_presence_transaction_rax(
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision)
{
	uint64_t value;

	if (!binding_valid(binding) ||
	    decision < PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE ||
	    decision > PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT)
		return 0;
	value = binding->generation ^ binding->transaction_id ^ binding->nonce ^
		((uint64_t)binding->initiator_cpu << 32) ^ binding->maximum_cpus ^
		((uint64_t)decision << 56) ^ 0x5452414e53414354ULL;
	for (size_t index = 0; index < sizeof(binding->capability); index++) {
		value = (value << 7) | (value >> 57);
		value ^= binding->capability[index];
	}
	return value &&
		value != PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_RAX_SENTINEL ?
		value : 0;
}

bool payload_mm_authvar_presence_transaction_ack_valid(
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision,
	const struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t saved_rax)
{
	return binding_valid(binding) && ack &&
		ack->binding.revision == binding->revision &&
		ack->binding.size == binding->size &&
		ack->binding.generation == binding->generation &&
		ack->binding.transaction_id == binding->transaction_id &&
		ack->binding.nonce == binding->nonce &&
		ack->binding.initiator_cpu == binding->initiator_cpu &&
		ack->binding.maximum_cpus == binding->maximum_cpus &&
		!memcmp(ack->binding.capability, binding->capability,
			sizeof(binding->capability)) &&
		!ack->binding.reserved[0] && !ack->binding.reserved[1] &&
		ack->decision == decision &&
		ack->transport_status ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ACCEPTED &&
		ack->operation_status ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ACCEPTED &&
		!ack->reserved &&
		saved_rax == payload_mm_authvar_presence_transaction_rax(binding,
			decision);
}
