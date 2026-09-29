/* SPDX-License-Identifier: GPL-2.0-only */

#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_route_composition.h"

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)

static struct smm_invocation_loader_composition composition;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_evidence evidence;
static struct smm_invocation_topology topology;
static struct payload_mm_authvar_presence_route_authority_policy policy;
static struct payload_mm_authvar_presence_transaction_binding binding;
static struct bootmem_reservation_receipt_authority verifier;
static struct bootmem_reservation_receipt receipt;
static uint32_t proof_context;
static enum smm_invocation_try_result provision_result;
static struct payload_mm_authvar_presence_route_session *observed_session;
static struct payload_mm_authvar_presence_transaction_slot *observed_slot;
static unsigned int calls;

static bool protected_storage(void *context, const void *object, size_t size)
{
	(void)context;
	(void)object;
	(void)size;
	return true;
}

enum smm_invocation_try_result intel_smm_invocation_adapter_route_provision(
	struct payload_mm_authvar_presence_route_session *session,
	struct payload_mm_authvar_presence_arm *arm,
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct smm_invocation_loader_composition *actual_composition,
	const struct smm_invocation_loader_instance *actual_instance,
	struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_topology *actual_topology,
	const struct payload_mm_authvar_presence_route_authority_policy
		*actual_policy,
	const struct payload_mm_authvar_presence_transaction_binding
		*actual_binding,
	struct bootmem_reservation_receipt_authority *actual_verifier,
	struct bootmem_reservation_receipt *actual_receipt,
	payload_mm_authvar_protected_storage actual_proof,
	void *actual_context)
{
	CHECK(session != NULL && arm != NULL && slot != NULL);
	CHECK(arm == platform_payload_mm_authvar_presence_arm());
	CHECK(slot == smm_get_payload_mm_authvar_presence_transaction_slot());
	CHECK(actual_composition == &composition);
	CHECK(actual_instance == &instance);
	CHECK(actual_evidence == &evidence);
	CHECK(actual_topology == &topology);
	CHECK(actual_policy == &policy);
	CHECK(actual_binding == &binding);
	CHECK(actual_verifier == &verifier);
	CHECK(actual_receipt == &receipt);
	CHECK(actual_proof == protected_storage);
	CHECK(actual_context == &proof_context);
	CHECK((uintptr_t)session + sizeof(*session) <= (uintptr_t)arm ||
		(uintptr_t)arm + sizeof(*arm) <= (uintptr_t)session);
	CHECK((uintptr_t)slot + sizeof(*slot) <= (uintptr_t)arm ||
		(uintptr_t)arm + sizeof(*arm) <= (uintptr_t)slot);
	CHECK((uintptr_t)session + sizeof(*session) <= (uintptr_t)slot ||
		(uintptr_t)slot + sizeof(*slot) <= (uintptr_t)session);
	if (calls) {
		CHECK(session == observed_session);
		CHECK(slot == observed_slot);
	} else {
		observed_session = session;
		observed_slot = slot;
	}
	calls++;
	return provision_result;
}

static enum smm_invocation_try_result provision(void)
{
	return starbook_mtl_authvar_presence_route_composition_provision(
		&composition, &instance, &evidence, &topology, &policy, &binding,
		&verifier, &receipt, protected_storage, &proof_context);
}

int main(void)
{
	CHECK(platform_payload_mm_authvar_presence_arm() != NULL);
	CHECK(platform_payload_mm_authvar_presence_arm() ==
		platform_payload_mm_authvar_presence_arm());
	CHECK(smm_get_payload_mm_authvar_presence_transaction_slot() != NULL);
	CHECK(smm_get_payload_mm_authvar_presence_transaction_slot() ==
		smm_get_payload_mm_authvar_presence_transaction_slot());

	provision_result = SMM_INVOCATION_TRY_SUCCESS;
	CHECK(provision() == SMM_INVOCATION_TRY_SUCCESS);
	provision_result = SMM_INVOCATION_TRY_RETRY;
	CHECK(provision() == SMM_INVOCATION_TRY_RETRY);
	provision_result = SMM_INVOCATION_TRY_ERROR;
	CHECK(provision() == SMM_INVOCATION_TRY_ERROR);
	CHECK(calls == 3U);
	return 0;
}
