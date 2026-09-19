/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>

#include "capsule_broker_internal.h"
#include "payload_mm_fmp_checkpoint_internal.h"
#include "payload_mm_fmp_dispatch_internal.h"
#include "payload_mm_fmp_owner_internal.h"
#include "payload_mm_fmp_transaction_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM FMP transaction executor must only be built in SMM"
#endif

struct transaction_control {
	uint64_t last_transaction;
	bool installed;
	bool install_attempted;
	bool busy;
	bool closed;
};

static struct transaction_control transaction_authority;

static struct transaction_control control_state(void)
{
	struct transaction_control state;

	memcpy(&state, &transaction_authority, sizeof(state));
	return state;
}

static bool control_matches(const struct transaction_control *expected)
{
	return !memcmp(&transaction_authority, expected, sizeof(*expected));
}

static bool prerequisites_ready(void)
{
	return payload_mm_fmp_dispatch_ready() && payload_mm_fmp_owner_ready() &&
		payload_mm_fmp_checkpoint_ready() &&
		capsule_broker_execution_ready();
}

#if ENV_TEST
const void *payload_mm_fmp_transaction_test_authority(size_t *size)
{
	*size = sizeof(transaction_authority);
	return &transaction_authority;
}
#endif

enum cb_err payload_mm_fmp_transaction_install(
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	struct transaction_control expected;
	bool protected;

	if (transaction_authority.install_attempted)
		return CB_ERR;
	transaction_authority.install_attempted = true;
	if (!storage_is_protected || !prerequisites_ready())
		return CB_ERR;
	expected = control_state();
	protected = storage_is_protected(context, &transaction_authority,
		sizeof(transaction_authority));
	if (!protected || !control_matches(&expected)) {
		memset(&transaction_authority, 0, sizeof(transaction_authority));
		transaction_authority.install_attempted = true;
		return CB_ERR;
	}
	transaction_authority.installed = true;
	return CB_SUCCESS;
}

static enum cb_err finish(uint64_t transaction, enum cb_err status,
	bool close)
{
	if (payload_mm_fmp_dispatch_complete(transaction) != CB_SUCCESS)
		status = CB_ERR;
	transaction_authority.busy = false;
	if (close || status != CB_SUCCESS) {
		transaction_authority.closed = true;
		capsule_broker_close_for_s3();
	}
	return status;
}

static enum cb_err execute(uint64_t request_address,
	const struct payload_mm_fmp_capsule_intent *direct_intent)
{
	struct payload_mm_fmp_owner_record before;
	struct payload_mm_fmp_owner_record after;
	const struct payload_mm_fmp_capsule_intent *staged;
	struct payload_mm_fmp_capsule_intent intent;
	struct transaction_control expected;
	enum cb_err status = CB_ERR;
	bool set_operation = false;

	if (!transaction_authority.installed || transaction_authority.busy ||
	    transaction_authority.closed ||
	    ((!request_address) == (!direct_intent)))
		return CB_ERR;
	transaction_authority.busy = true;
	expected = control_state();
	if (!prerequisites_ready() || !control_matches(&expected)) {
		transaction_authority.busy = false;
		transaction_authority.closed = true;
		capsule_broker_close_for_s3();
		return CB_ERR;
	}
	if ((direct_intent ? payload_mm_fmp_dispatch_prepare_intent(direct_intent) :
		payload_mm_fmp_dispatch_prepare(request_address, NULL, 0)) != CB_SUCCESS) {
		transaction_authority.busy = false;
		transaction_authority.closed = true;
		capsule_broker_close_for_s3();
		return CB_ERR;
	}
	if (!control_matches(&expected)) {
		const struct payload_mm_fmp_capsule_intent *prepared_intent =
			payload_mm_fmp_dispatch_capsule_intent();
		const struct payload_mm_fmp_state_command *prepared_command =
			payload_mm_fmp_dispatch_command();

		if (prepared_intent)
			payload_mm_fmp_dispatch_complete(
				prepared_intent->transaction);
		else if (prepared_command)
			payload_mm_fmp_dispatch_complete(
				prepared_command->message.transaction);
		memset(&transaction_authority, 0, sizeof(transaction_authority));
		transaction_authority.install_attempted = true;
		transaction_authority.closed = true;
		capsule_broker_close_for_s3();
		return CB_ERR;
	}
	staged = payload_mm_fmp_dispatch_capsule_intent();
	if (!staged) {
		const struct payload_mm_fmp_state_command *command =
			payload_mm_fmp_dispatch_command();

		transaction_authority.closed = true;
		if (command)
			payload_mm_fmp_dispatch_complete(
				command->message.transaction);
		capsule_broker_close_for_s3();
		return CB_ERR;
	}
	memcpy(&intent, staged, sizeof(intent));
	set_operation = intent.operation == PAYLOAD_MM_FMP_CAPSULE_SET;
	if (intent.transaction <= transaction_authority.last_transaction) {
		return finish(intent.transaction, CB_ERR, true);
	}
	transaction_authority.last_transaction = intent.transaction;
	expected = control_state();
	memset(&before, 0, sizeof(before));
	if (payload_mm_fmp_owner_read(PAYLOAD_MM_FMP_STATE_KEY_STATE, &before) !=
		CB_SUCCESS || !control_matches(&expected) || !prerequisites_ready() ||
		!before.present ||
		!before.sequence)
		goto out;
	if (capsule_broker_authenticate_intent_bound(staged, &before) !=
		CB_SUCCESS || !control_matches(&expected) || !prerequisites_ready())
		goto out;
	memset(&after, 0, sizeof(after));
	if (payload_mm_fmp_owner_read(PAYLOAD_MM_FMP_STATE_KEY_STATE, &after) !=
		CB_SUCCESS || !control_matches(&expected) || !prerequisites_ready() ||
	    memcmp(&before, &after, sizeof(before)))
		goto out;
	if (intent.operation == PAYLOAD_MM_FMP_CAPSULE_CHECK) {
		status = CB_SUCCESS;
		goto out;
	}
	if (payload_mm_fmp_checkpoint_commit_bound(intent.broker_generation,
		intent.transaction, intent.attempted_version, &before,
		intent.digest) != CB_SUCCESS || !control_matches(&expected))
		goto out;
	if (capsule_broker_apply_intent(staged) != CB_SUCCESS ||
	    !control_matches(&expected))
		goto out;
	status = CB_SUCCESS;
out:
	memset(&before, 0, sizeof(before));
	memset(&after, 0, sizeof(after));
	memset(&intent, 0, sizeof(intent));
	return finish(expected.last_transaction, status, set_operation);
}

enum cb_err payload_mm_fmp_transaction_execute(uint64_t request_address)
{
	return execute(request_address, NULL);
}

enum cb_err payload_mm_fmp_transaction_execute_intent(
	const struct payload_mm_fmp_capsule_intent *intent)
{
	return execute(0, intent);
}

void payload_mm_fmp_transaction_close(void)
{
	transaction_authority.closed = true;
	capsule_broker_close_for_s3();
}
