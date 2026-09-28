/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/intel/smm_invocation_adapter_route.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)

static struct payload_mm_authvar_presence_route_session session;
static struct payload_mm_authvar_presence_arm arm;
static struct payload_mm_authvar_presence_transaction_slot slot;
static struct smm_invocation_loader_composition composition;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_evidence evidence;
static struct smm_invocation_topology topology;
static struct payload_mm_authvar_presence_route_authority_policy policy;
static struct payload_mm_authvar_presence_transaction_binding binding;
static struct bootmem_reservation_receipt_authority verifier;
static struct bootmem_reservation_receipt receipt;
static struct smm_invocation_save_state_ops canonical_ops;
static uint32_t storage_context;
static enum smm_invocation_try_result provider_result;
static enum cb_err route_result;
static unsigned int sequence;
static unsigned int provider_sequence;
static unsigned int route_sequence;
static unsigned int proof_calls;

static bool protected_storage(void *context, const void *object, size_t size)
{
	(void)context;
	(void)object;
	(void)size;
	proof_calls++;
	return true;
}

enum smm_invocation_try_result
intel_smm_invocation_adapter_provider_provision(
	const struct smm_invocation_save_state_ops **ops)
{
	provider_sequence = ++sequence;
	/* Failure output is deliberately plausible to kill result bypasses. */
	*ops = &canonical_ops;
	return provider_result;
}

enum cb_err payload_mm_authvar_presence_route_session_provision(
	struct payload_mm_authvar_presence_route_session *actual_session,
	struct payload_mm_authvar_presence_arm *actual_arm,
	struct payload_mm_authvar_presence_transaction_slot *actual_slot,
	const struct smm_invocation_loader_composition *actual_composition,
	const struct smm_invocation_loader_instance *actual_instance,
	struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_topology *actual_topology,
	const struct smm_invocation_save_state_ops *actual_ops,
	const struct payload_mm_authvar_presence_route_authority_policy
		*actual_policy,
	const struct payload_mm_authvar_presence_transaction_binding
		*actual_binding,
	struct bootmem_reservation_receipt_authority *actual_verifier,
	struct bootmem_reservation_receipt *actual_receipt,
	payload_mm_authvar_protected_storage actual_proof,
	void *actual_context)
{
	route_sequence = ++sequence;
	CHECK(actual_session == &session);
	CHECK(actual_arm == &arm);
	CHECK(actual_slot == &slot);
	CHECK(actual_composition == &composition);
	CHECK(actual_instance == &instance);
	CHECK(actual_evidence == &evidence);
	CHECK(actual_topology == &topology);
	CHECK(actual_ops == &canonical_ops);
	CHECK(actual_policy == &policy);
	CHECK(actual_binding == &binding);
	CHECK(actual_verifier == &verifier);
	CHECK(actual_receipt == &receipt);
	CHECK(actual_proof == protected_storage);
	CHECK(actual_context == &storage_context);
	return route_result;
}

static enum smm_invocation_try_result provision(void)
{
	return intel_smm_invocation_adapter_route_provision(&session, &arm, &slot,
		&composition, &instance, &evidence, &topology, &policy, &binding,
		&verifier, &receipt, protected_storage, &storage_context);
}

static void reset(enum smm_invocation_try_result provider,
	enum cb_err route)
{
	provider_result = provider;
	route_result = route;
	sequence = 0;
	provider_sequence = 0;
	route_sequence = 0;
	proof_calls = 0;
}

int main(void)
{
	reset(SMM_INVOCATION_TRY_SUCCESS, CB_SUCCESS);
	CHECK(provision() == SMM_INVOCATION_TRY_SUCCESS);
	CHECK(provider_sequence == 1U);
	CHECK(route_sequence == 2U);
	CHECK(proof_calls == 0U);

	reset(SMM_INVOCATION_TRY_RETRY, CB_SUCCESS);
	CHECK(provision() == SMM_INVOCATION_TRY_RETRY);
	CHECK(provider_sequence == 1U);
	CHECK(route_sequence == 0U);
	CHECK(proof_calls == 0U);

	reset(SMM_INVOCATION_TRY_ERROR, CB_SUCCESS);
	CHECK(provision() == SMM_INVOCATION_TRY_ERROR);
	CHECK(provider_sequence == 1U);
	CHECK(route_sequence == 0U);
	CHECK(proof_calls == 0U);

	reset(SMM_INVOCATION_TRY_SUCCESS, CB_ERR);
	CHECK(provision() == SMM_INVOCATION_TRY_ERROR);
	CHECK(provider_sequence == 1U);
	CHECK(route_sequence == 2U);
	CHECK(proof_calls == 0U);
	return 0;
}
