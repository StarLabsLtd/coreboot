/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_INTEL_SMM_INVOCATION_ADAPTER_ROUTE_H
#define CPU_INTEL_SMM_INVOCATION_ADAPTER_ROUTE_H

#include <boot/payload_mm_authvar_presence_route_session.h>

#if ENV_SMM || ENV_TEST
/*
 * Provision the sole statically selected protected route only from the
 * canonical Intel adapter provider. The caller must be the sole provisioning
 * owner and no invocation may be live. RETRY is returned only for provider
 * contention before route provisioning.
 */
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
	void *protected_storage_context);
#endif

#endif
