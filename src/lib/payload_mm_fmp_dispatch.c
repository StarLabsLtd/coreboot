/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"
#include "payload_mm_fmp_dispatch_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM FMP dispatch staging must only be built in SMM"
#endif

static struct {
	struct payload_mm_fmp_dispatch_workspace *workspace;
	bool installed;
	bool install_attempted;
	bool busy;
} dispatch_authority;

bool payload_mm_fmp_dispatch_ready(void)
{
	return dispatch_authority.installed;
}

bool payload_mm_fmp_dispatch_buffer_available(const void *buffer, size_t size)
{
	return !payload_mm_authvar_buffers_overlap(buffer, size,
			&dispatch_authority, sizeof(dispatch_authority)) &&
		(!dispatch_authority.installed ||
		 !payload_mm_authvar_buffers_overlap(buffer, size,
			dispatch_authority.workspace,
			sizeof(*dispatch_authority.workspace)));
}

enum cb_err payload_mm_fmp_dispatch_workspace_install(
	struct payload_mm_fmp_dispatch_workspace *trusted_workspace,
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	if (dispatch_authority.install_attempted)
		return CB_ERR;
	dispatch_authority.install_attempted = true;
	if (!payload_mm_fmp_state_authority_ready() || !trusted_workspace ||
	    (uintptr_t)trusted_workspace % sizeof(uint64_t) ||
	    !payload_mm_fmp_state_staging_buffer(trusted_workspace,
		sizeof(*trusted_workspace)) ||
	    payload_mm_authvar_buffers_overlap(trusted_workspace,
		sizeof(*trusted_workspace), &dispatch_authority,
		sizeof(dispatch_authority)) ||
	    !storage_is_protected ||
	    !storage_is_protected(context, &dispatch_authority,
		sizeof(dispatch_authority)) ||
	    !storage_is_protected(context, trusted_workspace,
		sizeof(*trusted_workspace)))
		return CB_ERR;
	memset(trusted_workspace, 0, sizeof(*trusted_workspace));
	dispatch_authority = (typeof(dispatch_authority)) {
		.workspace = trusted_workspace,
		.installed = true,
		.install_attempted = true,
	};
	return CB_SUCCESS;
}

enum cb_err payload_mm_fmp_dispatch_prepare(uint64_t request_address,
	const void *current_state, size_t current_state_size)
{
	struct payload_mm_fmp_dispatch_workspace *workspace =
		dispatch_authority.workspace;

	if (!dispatch_authority.installed || dispatch_authority.busy ||
	    ((current_state == NULL) != (current_state_size == 0)) ||
	    (current_state != NULL &&
	     payload_mm_authvar_buffers_overlap(current_state, current_state_size,
		workspace, sizeof(*workspace))))
		return CB_ERR;
	dispatch_authority.busy = true;
	memset(workspace, 0, sizeof(*workspace));
	if (payload_mm_authvar_request_copy(request_address, &workspace->request,
		workspace->message, sizeof(workspace->message),
		&workspace->message_size) != CB_SUCCESS ||
	    payload_mm_authvar_buffers_overlap(
		(const void *)(uintptr_t)request_address,
		sizeof(struct payload_mm_authvar_request),
		(const void *)(uintptr_t)workspace->request.message_address,
		workspace->request.message_size) ||
	    workspace->message_size != sizeof(struct payload_mm_fmp_state_message) ||
	    payload_mm_fmp_state_command_prepare(workspace->message,
		workspace->message_size, current_state, current_state_size,
		&workspace->command) != CB_SUCCESS) {
		memset(workspace, 0, sizeof(*workspace));
		dispatch_authority.busy = false;
		return CB_ERR;
	}
	return CB_SUCCESS;
}

const struct payload_mm_fmp_state_command *payload_mm_fmp_dispatch_command(void)
{
	return dispatch_authority.busy ? &dispatch_authority.workspace->command : NULL;
}

enum cb_err payload_mm_fmp_dispatch_complete(uint64_t transaction)
{
	if (!dispatch_authority.busy ||
	    dispatch_authority.workspace->command.message.transaction != transaction)
		return CB_ERR;
	memset(dispatch_authority.workspace, 0,
		sizeof(*dispatch_authority.workspace));
	dispatch_authority.busy = false;
	return CB_SUCCESS;
}
