/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"
#include "payload_mm_fmp_dispatch_internal.h"
#if CONFIG(CAPSULE_BROKER_CONTRACT)
#include "capsule_broker_internal.h"

_Static_assert(PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256 ==
	CAPSULE_BROKER_DIGEST_SHA256 && PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE ==
	CAPSULE_BROKER_DIGEST_SIZE, "Payload-MM capsule digest ABI mismatch");
#endif

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM FMP dispatch staging must only be built in SMM"
#endif

static struct {
	struct payload_mm_fmp_dispatch_workspace *workspace;
	bool installed;
	bool install_attempted;
	bool busy;
	bool capsule_intent;
	uint64_t last_intent_transaction;
} dispatch_authority;

#if CONFIG(CAPSULE_BROKER_CONTRACT)
static bool capsule_intent_valid(const struct payload_mm_fmp_capsule_intent *intent)
{
	return payload_mm_fmp_state_authority_ready() &&
		intent->revision == PAYLOAD_MM_FMP_CAPSULE_INTENT_REVISION &&
		intent->size == sizeof(*intent) &&
		(intent->operation == PAYLOAD_MM_FMP_CAPSULE_CHECK ||
		 intent->operation == PAYLOAD_MM_FMP_CAPSULE_SET) &&
		!intent->flags && intent->broker_generation &&
		capsule_broker_intent_matches(intent->broker_generation,
			intent->capsule_size) &&
		intent->transaction &&
		intent->transaction > dispatch_authority.last_intent_transaction &&
		intent->digest_algorithm == PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256 &&
		intent->digest_size == sizeof(intent->digest) && !intent->reserved;
}
#endif

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
	bool prepared = false;

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
		workspace->request.message_size))
		goto fail;
	if (workspace->message_size == sizeof(struct payload_mm_fmp_state_message))
		prepared = payload_mm_fmp_state_command_prepare(workspace->message,
		workspace->message_size, current_state, current_state_size,
		&workspace->command) == CB_SUCCESS;
#if CONFIG(CAPSULE_BROKER_CONTRACT)
	else if (workspace->message_size ==
		 sizeof(struct payload_mm_fmp_capsule_intent) && !current_state &&
		 !current_state_size) {
		struct payload_mm_fmp_capsule_intent intent;

		memcpy(&intent, workspace->message, sizeof(intent));
		if (capsule_intent_valid(&intent)) {
			workspace->intent = intent;
			dispatch_authority.last_intent_transaction =
				intent.transaction;
			dispatch_authority.capsule_intent = true;
			prepared = true;
		}
	}
#endif
	if (!prepared) {
fail:
		memset(workspace, 0, sizeof(*workspace));
		dispatch_authority.busy = false;
		return CB_ERR;
	}
	if (workspace->message_size == sizeof(struct payload_mm_fmp_state_message))
		dispatch_authority.capsule_intent = false;
	return CB_SUCCESS;
}

const struct payload_mm_fmp_state_command *payload_mm_fmp_dispatch_command(void)
{
	return dispatch_authority.busy && !dispatch_authority.capsule_intent ?
		&dispatch_authority.workspace->command : NULL;
}

const struct payload_mm_fmp_capsule_intent *
payload_mm_fmp_dispatch_capsule_intent(void)
{
	return dispatch_authority.busy && dispatch_authority.capsule_intent ?
		&dispatch_authority.workspace->intent : NULL;
}

enum cb_err payload_mm_fmp_dispatch_complete(uint64_t transaction)
{
	uint64_t expected;

	if (!dispatch_authority.busy)
		return CB_ERR;
	expected = dispatch_authority.capsule_intent ?
		dispatch_authority.workspace->intent.transaction :
		dispatch_authority.workspace->command.message.transaction;
	if (expected != transaction)
		return CB_ERR;
	memset(dispatch_authority.workspace, 0,
		sizeof(*dispatch_authority.workspace));
	dispatch_authority.busy = false;
	dispatch_authority.capsule_intent = false;
	return CB_SUCCESS;
}
