/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker.h>
#include <stdint.h>
#include <string.h>

#include "capsule_broker_internal.h"
#include "payload_mm_authvar_internal.h"
#include "payload_mm_fmp_checkpoint_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM FMP checkpoint owner must only be built in SMM"
#endif

static struct {
	struct payload_mm_fmp_checkpoint_backend backend;
	uint8_t context[PAYLOAD_MM_FMP_CHECKPOINT_CONTEXT_SIZE] __aligned(8);
	uint64_t last_transaction;
	bool installed;
	bool install_attempted;
	bool grant_invoked;
} checkpoint_authority;

static bool context_valid(const void *context, size_t size)
{
	return (context == NULL && size == 0) ||
		(context != NULL && size &&
		 size <= PAYLOAD_MM_FMP_CHECKPOINT_CONTEXT_SIZE &&
		 !payload_mm_authvar_buffers_overlap(context, size,
			&checkpoint_authority, sizeof(checkpoint_authority)));
}

static bool record_valid(const struct payload_mm_fmp_checkpoint_record *record)
{
	if (!record->sequence ||
	    record->attributes != PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES ||
	    record->data_size != PAYLOAD_MM_FMP_STATE_WIRE_SIZE || record->reserved)
		return false;
	for (size_t i = 0; i < 4; i++)
		if (record->data[i] > 1)
			return false;
	return true;
}

enum cb_err payload_mm_fmp_checkpoint_backend_install(
	const struct payload_mm_fmp_checkpoint_backend *trusted_backend,
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	struct payload_mm_fmp_checkpoint_backend snapshot;
	uint8_t context_snapshot[PAYLOAD_MM_FMP_CHECKPOINT_CONTEXT_SIZE];

	if (checkpoint_authority.install_attempted)
		return CB_ERR;
	checkpoint_authority.install_attempted = true;
	if (!payload_mm_fmp_state_authority_ready() || !trusted_backend ||
	    !storage_is_protected)
		return CB_ERR;
	memcpy(&snapshot, trusted_backend, sizeof(snapshot));
	if (snapshot.revision != PAYLOAD_MM_FMP_CHECKPOINT_BACKEND_REVISION ||
	    snapshot.size != sizeof(snapshot) || !snapshot.broker_generation ||
	    !snapshot.read || !snapshot.commit ||
	    !context_valid(snapshot.context, snapshot.context_size))
		return CB_ERR;
	if (snapshot.context_size)
		memcpy(context_snapshot, snapshot.context, snapshot.context_size);
	if (!storage_is_protected(context, &checkpoint_authority,
		    sizeof(checkpoint_authority)))
		return CB_ERR;
	if (snapshot.context_size) {
		memcpy(checkpoint_authority.context, context_snapshot,
			snapshot.context_size);
		snapshot.context = checkpoint_authority.context;
	}
	checkpoint_authority.backend = snapshot;
	checkpoint_authority.installed = true;
	return CB_SUCCESS;
}

enum cb_err payload_mm_fmp_checkpoint_commit(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version)
{
	struct payload_mm_fmp_checkpoint_record current;
	struct payload_mm_fmp_checkpoint_record expected_current;
	struct payload_mm_fmp_checkpoint_record candidate;
	struct payload_mm_fmp_checkpoint_record expected_candidate;
	struct payload_mm_fmp_checkpoint_record verified;
	struct payload_mm_fmp_state_identity identity;
	struct payload_mm_fmp_state_identity call_identity;
	struct payload_mm_fmp_checkpoint_backend *backend =
		&checkpoint_authority.backend;

	if (!checkpoint_authority.installed || checkpoint_authority.grant_invoked ||
	    generation != backend->broker_generation || !transaction ||
	    transaction <= checkpoint_authority.last_transaction)
		return CB_ERR;
	checkpoint_authority.last_transaction = transaction;
	if (payload_mm_fmp_state_identity_get(&identity) != CB_SUCCESS)
		return CB_ERR;
	memset(&current, 0, sizeof(current));
	call_identity = identity;
	if (backend->read(backend->context, &call_identity, &current) != CB_SUCCESS ||
	    memcmp(&call_identity, &identity, sizeof(identity)) != 0 ||
	    !record_valid(&current))
		return CB_ERR;
	candidate = current;
	if (payload_mm_fmp_state_checkpoint_build(current.data, attempted_version,
		&identity, candidate.data) != CB_SUCCESS)
		return CB_ERR;
	if (memcmp(candidate.data, current.data, sizeof(candidate.data)) != 0) {
		if (current.sequence == UINT64_MAX)
			return CB_ERR;
		candidate.sequence++;
		expected_current = current;
		expected_candidate = candidate;
		call_identity = identity;
		if (backend->commit(backend->context, &call_identity, &current,
			&candidate) != CB_SUCCESS ||
		    memcmp(&call_identity, &identity, sizeof(identity)) != 0 ||
		    memcmp(&current, &expected_current, sizeof(current)) != 0 ||
		    memcmp(&candidate, &expected_candidate, sizeof(candidate)) != 0)
			return CB_ERR;
	}
	memset(&verified, 0, sizeof(verified));
	call_identity = identity;
	if (backend->read(backend->context, &call_identity, &verified) != CB_SUCCESS ||
	    memcmp(&call_identity, &identity, sizeof(identity)) != 0 ||
	    !record_valid(&verified) || verified.sequence != candidate.sequence ||
	    memcmp(verified.data, candidate.data, sizeof(verified.data)) != 0)
		return CB_ERR;
	checkpoint_authority.grant_invoked = true;
	return capsule_broker_checkpoint_grant(generation, transaction,
		attempted_version);
}
