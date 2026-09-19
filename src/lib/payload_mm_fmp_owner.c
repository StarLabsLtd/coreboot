/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"
#include "payload_mm_fmp_owner_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM FMP owner must only be built in SMM"
#endif

static struct {
	struct payload_mm_fmp_owner_backend backend;
	uint8_t context[PAYLOAD_MM_FMP_OWNER_CONTEXT_SIZE] __aligned(8);
	bool installed;
	bool install_attempted;
} owner_authority;

static bool bytes_zero(const uint8_t *data, size_t size)
{
	uint8_t bits = 0;

	for (size_t i = 0; i < size; i++)
		bits |= data[i];
	return bits == 0;
}

static bool key_valid(uint32_t key)
{
	return key <= PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION;
}

static bool record_valid(uint32_t key,
	const struct payload_mm_fmp_owner_record *record)
{
	uint32_t expected_size = key == PAYLOAD_MM_FMP_STATE_KEY_STATE ?
		PAYLOAD_MM_FMP_STATE_WIRE_SIZE : sizeof(uint32_t);

	if (!key_valid(key) || !record->sequence || record->present > 1 ||
	    record->reserved || record->reserved2)
		return false;
	if (!record->present)
		return !record->attributes && !record->data_size &&
			bytes_zero(record->data, sizeof(record->data));
	if (record->attributes != PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES ||
	    record->data_size != expected_size ||
	    (key != PAYLOAD_MM_FMP_STATE_KEY_STATE &&
	     !bytes_zero(record->data + sizeof(uint32_t),
		sizeof(record->data) - sizeof(uint32_t))))
		return false;
	return key != PAYLOAD_MM_FMP_STATE_KEY_STATE ||
		payload_mm_fmp_state_data_valid(record->data);
}

static bool owner_buffer(const void *buffer, size_t size)
{
	return buffer &&
		(uintptr_t)buffer %
			_Alignof(struct payload_mm_fmp_owner_record) == 0 &&
		payload_mm_fmp_state_staging_buffer(buffer, size) &&
		!payload_mm_authvar_buffers_overlap(buffer, size, &owner_authority,
			sizeof(owner_authority));
}

enum cb_err payload_mm_fmp_owner_install(
	const struct payload_mm_fmp_owner_backend *trusted_backend,
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	struct payload_mm_fmp_owner_backend snapshot;
	uint8_t context_snapshot[PAYLOAD_MM_FMP_OWNER_CONTEXT_SIZE];
	bool protected;

	if (owner_authority.install_attempted)
		return CB_ERR;
	owner_authority.install_attempted = true;
	if (!payload_mm_fmp_state_authority_ready() || !trusted_backend ||
	    !storage_is_protected)
		return CB_ERR;
	memcpy(&snapshot, trusted_backend, sizeof(snapshot));
	if (snapshot.revision != PAYLOAD_MM_FMP_OWNER_REVISION ||
	    snapshot.size != sizeof(snapshot) || !snapshot.read || !snapshot.commit ||
	    ((snapshot.context == NULL) != (snapshot.context_size == 0)) ||
	    snapshot.context_size > sizeof(context_snapshot) ||
	    (snapshot.context != NULL &&
	     payload_mm_authvar_buffers_overlap(snapshot.context,
		snapshot.context_size, &owner_authority,
		sizeof(owner_authority))))
		return CB_ERR;
	if (snapshot.context_size)
		memcpy(context_snapshot, snapshot.context, snapshot.context_size);
	protected = storage_is_protected(context, &owner_authority,
		sizeof(owner_authority));
	if (!protected) {
		memset(&owner_authority, 0, sizeof(owner_authority));
		owner_authority.install_attempted = true;
		return CB_ERR;
	}
	memset(&owner_authority, 0, sizeof(owner_authority));
	owner_authority.backend = snapshot;
	if (snapshot.context_size) {
		memcpy(owner_authority.context, context_snapshot,
			snapshot.context_size);
		owner_authority.backend.context = owner_authority.context;
	}
	owner_authority.installed = true;
	owner_authority.install_attempted = true;
	return CB_SUCCESS;
}

bool payload_mm_fmp_owner_ready(void)
{
	return owner_authority.installed;
}

bool payload_mm_fmp_owner_storage_overlaps(const void *buffer, size_t size)
{
	return payload_mm_authvar_buffers_overlap(buffer, size, &owner_authority,
		sizeof(owner_authority));
}

bool payload_mm_fmp_owner_buffer_available(const void *buffer, size_t size)
{
	return payload_mm_fmp_state_staging_buffer(buffer, size) &&
		!payload_mm_fmp_owner_storage_overlaps(buffer, size);
}

enum cb_err payload_mm_fmp_owner_read(uint32_t key,
	struct payload_mm_fmp_owner_record *record)
{
	struct payload_mm_fmp_owner_record snapshot;
	struct payload_mm_fmp_state_identity identity;
	struct payload_mm_fmp_state_identity expected_identity;

	if (!owner_authority.installed || !key_valid(key) || !record ||
	    !owner_buffer(record, sizeof(*record)))
		return CB_ERR;
	memset(record, 0, sizeof(*record));
	if (payload_mm_fmp_state_identity_get_for_key(key, &identity) != CB_SUCCESS)
		return CB_ERR;
	expected_identity = identity;
	memset(&snapshot, 0, sizeof(snapshot));
	if (owner_authority.backend.read(owner_authority.backend.context, &identity,
		key, &snapshot) != CB_SUCCESS ||
	    memcmp(&identity, &expected_identity, sizeof(identity)) != 0 ||
	    !record_valid(key, &snapshot))
		return CB_ERR;
	*record = snapshot;
	return CB_SUCCESS;
}

static enum cb_err commit(uint32_t key,
	const struct payload_mm_fmp_owner_record *current_record,
	const struct payload_mm_fmp_owner_record *candidate_record)
{
	struct payload_mm_fmp_owner_record current = *current_record;
	struct payload_mm_fmp_owner_record candidate = *candidate_record;
	const struct payload_mm_fmp_owner_record expected_current = current;
	const struct payload_mm_fmp_owner_record expected_candidate = candidate;
	struct payload_mm_fmp_owner_record verified;
	struct payload_mm_fmp_state_identity identity;
	struct payload_mm_fmp_state_identity expected_identity;

	if (!owner_authority.installed)
		return CB_ERR;
	if (!record_valid(key, &current) || !record_valid(key, &candidate) ||
	    current.sequence == UINT64_MAX ||
	    candidate.sequence != current.sequence + 1)
		return CB_ERR;
	if (payload_mm_fmp_state_identity_get_for_key(key, &identity) != CB_SUCCESS)
		return CB_ERR;
	expected_identity = identity;
	if (owner_authority.backend.commit(owner_authority.backend.context, &identity,
		key, &current, &candidate) != CB_SUCCESS ||
	    memcmp(&identity, &expected_identity, sizeof(identity)) != 0 ||
	    memcmp(&current, &expected_current, sizeof(current)) != 0 ||
	    memcmp(&candidate, &expected_candidate, sizeof(candidate)) != 0)
		return CB_ERR;
	identity = expected_identity;
	memset(&verified, 0, sizeof(verified));
	if (owner_authority.backend.read(owner_authority.backend.context, &identity,
		key, &verified) != CB_SUCCESS ||
	    memcmp(&identity, &expected_identity, sizeof(identity)) != 0 ||
	    !record_valid(key, &verified) ||
	    memcmp(&verified, &candidate, sizeof(verified)) != 0)
		return CB_ERR;
	return CB_SUCCESS;
}

enum cb_err payload_mm_fmp_owner_commit_state(
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate)
{
	struct payload_mm_fmp_owner_record current_snapshot;
	struct payload_mm_fmp_owner_record candidate_snapshot;

	if (!current || !candidate || !owner_buffer(current, sizeof(*current)) ||
	    !owner_buffer(candidate, sizeof(*candidate)) ||
	    payload_mm_authvar_buffers_overlap(current, sizeof(*current), candidate,
		sizeof(*candidate)))
		return CB_ERR;
	current_snapshot = *current;
	candidate_snapshot = *candidate;
	if (!candidate_snapshot.present ||
	    !payload_mm_fmp_state_transition_valid(
		current_snapshot.present ? current_snapshot.data : NULL,
		candidate_snapshot.data))
		return CB_ERR;
	return commit(PAYLOAD_MM_FMP_STATE_KEY_STATE, &current_snapshot,
		&candidate_snapshot);
}

enum cb_err payload_mm_fmp_owner_remove_legacy(uint32_t key,
	const struct payload_mm_fmp_owner_record *current)
{
	struct payload_mm_fmp_owner_record current_snapshot;
	struct payload_mm_fmp_owner_record candidate;

	if (key < PAYLOAD_MM_FMP_STATE_KEY_VERSION ||
	    key > PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION || !current ||
	    !owner_buffer(current, sizeof(*current)))
		return CB_ERR;
	current_snapshot = *current;
	if (!record_valid(key, &current_snapshot) || !current_snapshot.present ||
	    current_snapshot.sequence == UINT64_MAX)
		return CB_ERR;
	candidate = (struct payload_mm_fmp_owner_record) {
		.sequence = current_snapshot.sequence + 1,
	};
	return commit(key, &current_snapshot, &candidate);
}
