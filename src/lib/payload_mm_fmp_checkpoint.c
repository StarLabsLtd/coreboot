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
	bool installed;
	bool install_attempted;
	bool grant_invoked;
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

enum cb_err payload_mm_fmp_checkpoint_commit(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version)
{
	struct payload_mm_fmp_checkpoint_workspace *workspace =
		checkpoint_authority.workspace;
	struct payload_mm_fmp_state_identity identity;
	enum cb_err status = CB_ERR;

	if (!checkpoint_authority.installed || checkpoint_authority.grant_invoked ||
	    generation != checkpoint_authority.broker_generation || !transaction ||
	    transaction <= checkpoint_authority.last_transaction)
		return CB_ERR;
	checkpoint_authority.last_transaction = transaction;
	memset(workspace, 0, sizeof(*workspace));
	if (payload_mm_fmp_owner_read(PAYLOAD_MM_FMP_STATE_KEY_STATE,
		&workspace->current) != CB_SUCCESS || !workspace->current.present)
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
	status = capsule_broker_checkpoint_grant(generation, transaction,
		attempted_version);
out:
	return finish(status);
}
