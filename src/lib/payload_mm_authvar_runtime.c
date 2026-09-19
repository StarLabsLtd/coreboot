/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar.h>
#include <stdint.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM runtime authority must only be built in SMM"
#endif

static struct {
	struct payload_mm_authvar_contract contract;
	bool installed;
	bool install_attempted;
} authority;

static bool ranges_overlap(uint64_t left_base, uint64_t left_size,
	uint64_t right_base, uint64_t right_size)
{
	uint64_t left_end;
	uint64_t right_end;

	if (!payload_mm_authvar_range_end(left_base, left_size, &left_end) ||
	    !payload_mm_authvar_range_end(right_base, right_size, &right_end))
		return true;
	return left_base < right_end && right_base < left_end;
}

static bool smram_output(const void *output, size_t size)
{
	return payload_mm_authvar_range_within((uintptr_t)output, size,
			authority.contract.smram.base, authority.contract.smram.size) &&
		!ranges_overlap((uintptr_t)output, size, (uintptr_t)&authority,
			sizeof(authority));
}

enum cb_err payload_mm_authvar_authority_install(
	const struct payload_mm_authvar_contract *trusted_contract,
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	struct payload_mm_authvar_contract snapshot;

	if (authority.install_attempted)
		return CB_ERR;
	authority.install_attempted = true;
	if (!trusted_contract || !storage_is_protected ||
	    !storage_is_protected(context, &authority, sizeof(authority)))
		return CB_ERR;
	memcpy(&snapshot, trusted_contract, sizeof(snapshot));
	if (!payload_mm_authvar_contract_valid(&snapshot))
		return CB_ERR;
	authority.contract = snapshot;
	authority.installed = true;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_request_copy(uint64_t request_address,
	struct payload_mm_authvar_request *trusted_request, void *trusted_message,
	size_t trusted_message_capacity, size_t *trusted_message_size)
{
	struct payload_mm_authvar_request snapshot;

	if (!authority.installed ||
	    !smram_output(trusted_request, sizeof(*trusted_request)) ||
	    !smram_output(trusted_message_size, sizeof(*trusted_message_size)) ||
	    ranges_overlap((uintptr_t)trusted_request, sizeof(*trusted_request),
		(uintptr_t)trusted_message_size, sizeof(*trusted_message_size)))
		return CB_ERR;
	memset(trusted_request, 0, sizeof(*trusted_request));
	*trusted_message_size = 0;
	if (request_address > UINTPTR_MAX ||
	    request_address % sizeof(uint64_t) ||
	    !payload_mm_authvar_range_within(request_address, sizeof(snapshot),
		authority.contract.communication.base,
		authority.contract.communication.size))
		return CB_ERR;
	memcpy(&snapshot, (const void *)(uintptr_t)request_address,
		sizeof(snapshot));
	if (snapshot.revision != PAYLOAD_MM_AUTHVAR_REQUEST_REVISION ||
	    snapshot.size != sizeof(snapshot) ||
	    snapshot.operation != PAYLOAD_MM_AUTHVAR_COMMUNICATE ||
	    snapshot.flags || snapshot.reserved ||
	    snapshot.generation != authority.contract.generation ||
	    snapshot.message_address > UINTPTR_MAX ||
	    snapshot.message_address % sizeof(uint64_t) ||
	    snapshot.message_size < PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES ||
	    snapshot.message_size % sizeof(uint64_t) ||
	    snapshot.message_size > trusted_message_capacity ||
	    !payload_mm_authvar_range_within(snapshot.message_address,
		snapshot.message_size, authority.contract.communication.base,
		authority.contract.communication.size) ||
	    !smram_output(trusted_message, snapshot.message_size) ||
	    ranges_overlap((uintptr_t)trusted_message, snapshot.message_size,
		(uintptr_t)trusted_request, sizeof(*trusted_request)) ||
	    ranges_overlap((uintptr_t)trusted_message, snapshot.message_size,
		(uintptr_t)trusted_message_size, sizeof(*trusted_message_size)))
		return CB_ERR;
	memcpy(trusted_message, (const void *)(uintptr_t)snapshot.message_address,
		snapshot.message_size);
	*trusted_request = snapshot;
	*trusted_message_size = snapshot.message_size;
	return CB_SUCCESS;
}
