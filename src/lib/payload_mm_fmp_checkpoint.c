/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>

#include "capsule_broker_internal.h"
#include "payload_mm_authvar_internal.h"
#include "payload_mm_fmp_checkpoint_internal.h"
#include "payload_mm_fmp_dispatch_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM FMP checkpoint owner must only be built in SMM"
#endif

static struct {
	struct payload_mm_fmp_checkpoint_workspace *workspace;
	uint64_t broker_generation;
	uint64_t last_transaction;
	uint64_t pending_transaction;
	struct payload_mm_fmp_owner_record pending_record;
	uint8_t pending_digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE];
	bool installed;
	bool install_attempted;
	bool grant_invoked;
	bool finalize_invoked;
} checkpoint_authority;

static bool checkpoint_storage_overlaps(const void *buffer, size_t size)
{
	return payload_mm_authvar_buffers_overlap(buffer, size,
		&checkpoint_authority, sizeof(checkpoint_authority));
}

#if ENV_TEST
const void *payload_mm_fmp_checkpoint_test_authority(size_t *size)
{
	*size = sizeof(checkpoint_authority);
	return &checkpoint_authority;
}

bool payload_mm_fmp_checkpoint_test_storage_overlaps(const void *buffer,
	size_t size)
{
	return checkpoint_storage_overlaps(buffer, size);
}
#endif

static enum cb_err finish(enum cb_err status)
{
	if (checkpoint_authority.workspace)
		memset(checkpoint_authority.workspace, 0,
			sizeof(*checkpoint_authority.workspace));
	return status;
}

bool payload_mm_fmp_checkpoint_ready(void)
{
	return checkpoint_authority.installed &&
		!checkpoint_authority.grant_invoked;
}

enum cb_err payload_mm_fmp_checkpoint_owner_bind(uint64_t broker_generation,
	struct payload_mm_fmp_checkpoint_workspace *trusted_workspace,
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	bool authority_protected;
	bool workspace_protected;

	if (checkpoint_storage_overlaps(trusted_workspace,
		sizeof(*trusted_workspace)))
		return CB_ERR;
	if (checkpoint_authority.install_attempted)
		return CB_ERR;
	checkpoint_authority.install_attempted = true;
	if (!payload_mm_fmp_owner_ready() || !payload_mm_fmp_dispatch_ready() ||
	    !capsule_broker_generation_matches(broker_generation) ||
	    !trusted_workspace ||
	    (uintptr_t)trusted_workspace % _Alignof(*trusted_workspace) ||
	    !payload_mm_fmp_owner_buffer_available(trusted_workspace,
		sizeof(*trusted_workspace)) ||
	    !payload_mm_fmp_dispatch_buffer_available(trusted_workspace,
		sizeof(*trusted_workspace)) ||
	    !capsule_broker_buffer_available(trusted_workspace,
		sizeof(*trusted_workspace)) || !storage_is_protected)
		return CB_ERR;
	authority_protected = storage_is_protected(context, &checkpoint_authority,
		sizeof(checkpoint_authority));
	workspace_protected = storage_is_protected(context, trusted_workspace,
		sizeof(*trusted_workspace));
	if (!authority_protected || !workspace_protected) {
		memset(&checkpoint_authority, 0, sizeof(checkpoint_authority));
		checkpoint_authority.install_attempted = true;
		return CB_ERR;
	}
	memset(trusted_workspace, 0, sizeof(*trusted_workspace));
	checkpoint_authority = (typeof(checkpoint_authority)) {
		.workspace = trusted_workspace,
		.broker_generation = broker_generation,
		.installed = true,
		.install_attempted = true,
	};
	return CB_SUCCESS;
}

enum cb_err payload_mm_fmp_checkpoint_commit_bound(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version,
	const struct payload_mm_fmp_owner_record *authenticated_record,
	const uint8_t digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE])
{
	struct payload_mm_fmp_owner_record authenticated_snapshot;
	uint8_t digest_snapshot[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE];
	struct payload_mm_fmp_checkpoint_workspace *workspace =
		checkpoint_authority.workspace;
	struct payload_mm_fmp_state_identity identity;
	enum cb_err status = CB_ERR;

	if (!authenticated_record || !authenticated_record->sequence || !digest ||
	    !checkpoint_authority.installed || checkpoint_authority.grant_invoked ||
	    generation != checkpoint_authority.broker_generation || !transaction ||
	    transaction <= checkpoint_authority.last_transaction)
		return CB_ERR;
	authenticated_snapshot = *authenticated_record;
	memcpy(digest_snapshot, digest, sizeof(digest_snapshot));
	checkpoint_authority.last_transaction = transaction;
	memset(workspace, 0, sizeof(*workspace));
	if (payload_mm_fmp_owner_read(PAYLOAD_MM_FMP_STATE_KEY_STATE,
		&workspace->current) != CB_SUCCESS || !workspace->current.present)
		goto out;
	if (memcmp(&workspace->current, &authenticated_snapshot,
		 sizeof(workspace->current)))
		goto out;
	workspace->candidate = workspace->current;
	if (payload_mm_fmp_state_checkpoint_build(workspace->current.data,
		attempted_version, &identity,
		workspace->candidate.data) != CB_SUCCESS)
		goto out;
	if (memcmp(workspace->candidate.data, workspace->current.data,
	    sizeof(workspace->candidate.data)) != 0) {
		if (workspace->current.sequence == UINT64_MAX)
			goto out;
		workspace->candidate.sequence++;
		if (payload_mm_fmp_owner_commit_state(&workspace->current,
			&workspace->candidate) != CB_SUCCESS)
			goto out;
	}
	if (payload_mm_fmp_owner_read(PAYLOAD_MM_FMP_STATE_KEY_STATE,
		&workspace->verified) != CB_SUCCESS ||
	    memcmp(&workspace->verified, &workspace->candidate,
		sizeof(workspace->verified)) != 0)
		goto out;
	checkpoint_authority.grant_invoked = true;
	status = capsule_broker_checkpoint_grant_bound(generation,
		transaction, attempted_version, authenticated_snapshot.sequence,
		workspace->verified.sequence, digest_snapshot);
	if (status == CB_SUCCESS) {
		checkpoint_authority.pending_transaction = transaction;
		checkpoint_authority.pending_record = workspace->verified;
		memcpy(checkpoint_authority.pending_digest, digest_snapshot,
			sizeof(checkpoint_authority.pending_digest));
	}
out:
	return finish(status);
}

enum cb_err payload_mm_fmp_checkpoint_finalize_bound(uint64_t generation,
	uint64_t transaction,
	const uint8_t digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE])
{
	struct payload_mm_fmp_checkpoint_workspace *workspace =
		checkpoint_authority.workspace;
	struct payload_mm_fmp_owner_record pending;
	struct capsule_broker_success success;
	uint8_t digest_snapshot[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE];
	enum cb_err status = CB_ERR;

	if (!digest || !checkpoint_authority.installed ||
	    !checkpoint_authority.grant_invoked ||
	    checkpoint_authority.finalize_invoked ||
	    generation != checkpoint_authority.broker_generation ||
	    !transaction ||
	    transaction != checkpoint_authority.pending_transaction ||
	    memcmp(digest, checkpoint_authority.pending_digest,
		sizeof(digest_snapshot)))
		return CB_ERR;
	checkpoint_authority.finalize_invoked = true;
	pending = checkpoint_authority.pending_record;
	memcpy(digest_snapshot, checkpoint_authority.pending_digest,
		sizeof(digest_snapshot));
	memset(workspace, 0, sizeof(*workspace));
	if (!pending.present || !pending.sequence ||
	    payload_mm_fmp_owner_read(PAYLOAD_MM_FMP_STATE_KEY_STATE,
		&workspace->current) != CB_SUCCESS ||
	    memcmp(&workspace->current, &pending, sizeof(pending)))
		goto out;
	memset(&success, 0, sizeof(success));
	if (capsule_broker_success_claim_bound(generation, transaction,
		pending.sequence, digest_snapshot, &success) != CB_SUCCESS ||
	    checkpoint_authority.broker_generation != generation ||
	    !checkpoint_authority.installed ||
	    !checkpoint_authority.grant_invoked ||
	    !checkpoint_authority.finalize_invoked ||
	    memcmp(&checkpoint_authority.pending_record, &pending,
		sizeof(pending)) ||
	    checkpoint_authority.pending_transaction != transaction ||
	    memcmp(checkpoint_authority.pending_digest, digest_snapshot,
		sizeof(digest_snapshot)))
		goto out;
	workspace->candidate = workspace->current;
	if (payload_mm_fmp_state_success_build(workspace->current.data,
		success.version, success.lowest_supported_version,
		workspace->candidate.data) != CB_SUCCESS ||
	    workspace->current.sequence == UINT64_MAX)
		goto out;
	workspace->candidate.sequence++;
	if (payload_mm_fmp_owner_commit_state(&workspace->current,
		&workspace->candidate) != CB_SUCCESS ||
	    payload_mm_fmp_owner_read(PAYLOAD_MM_FMP_STATE_KEY_STATE,
		&workspace->verified) != CB_SUCCESS ||
	    memcmp(&workspace->verified, &workspace->candidate,
		sizeof(workspace->verified)))
		goto out;
	status = CB_SUCCESS;
out:
	memset(&success, 0, sizeof(success));
	memset(&pending, 0, sizeof(pending));
	memset(digest_snapshot, 0, sizeof(digest_snapshot));
	return finish(status);
}
