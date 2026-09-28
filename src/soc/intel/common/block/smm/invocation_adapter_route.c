/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/intel/smm_invocation_adapter_route.h>
#include <cpu/intel/smm_invocation_adapter_provider.h>

enum smm_invocation_try_result intel_smm_invocation_adapter_route_provision(
	struct payload_mm_authvar_presence_route_session *session,
	struct payload_mm_authvar_presence_arm *arm,
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct payload_mm_authvar_presence_route_authority_policy
		*authority_policy,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct bootmem_reservation_receipt_authority *page_verifier,
	struct bootmem_reservation_receipt *page_receipt,
	payload_mm_authvar_protected_storage protected_storage,
	void *protected_storage_context)
{
	const struct smm_invocation_save_state_ops *ops = NULL;
	const enum smm_invocation_try_result result =
		intel_smm_invocation_adapter_provider_provision(&ops);

	if (result != SMM_INVOCATION_TRY_SUCCESS)
		return result;
	if (payload_mm_authvar_presence_route_session_provision(session, arm, slot,
		composition, instance, evidence, topology, ops, authority_policy,
		binding, page_verifier, page_receipt, protected_storage,
		protected_storage_context) != CB_SUCCESS)
		return SMM_INVOCATION_TRY_ERROR;
	return SMM_INVOCATION_TRY_SUCCESS;
}
