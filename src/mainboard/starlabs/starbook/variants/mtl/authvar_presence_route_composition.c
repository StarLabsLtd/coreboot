/* SPDX-License-Identifier: GPL-2.0-only */

#include "authvar_presence_route_composition.h"

#include <boot/payload_mm_authvar_presence_arm.h>

#if !ENV_SMM && !ENV_TEST
#error "StarBook MTL presence-route composition owner is SMM-only"
#endif

struct starbook_mtl_authvar_presence_route_owner {
	struct payload_mm_authvar_presence_arm arm;
	struct payload_mm_authvar_presence_transaction_slot slot;
	struct payload_mm_authvar_presence_route_session session;
} __aligned(8);

static struct starbook_mtl_authvar_presence_route_owner owner;

struct payload_mm_authvar_presence_arm *
platform_payload_mm_authvar_presence_arm(void)
{
	return &owner.arm;
}

struct payload_mm_authvar_presence_transaction_slot *
smm_get_payload_mm_authvar_presence_transaction_slot(void)
{
	return &owner.slot;
}

enum smm_invocation_try_result
starbook_mtl_authvar_presence_route_composition_provision(
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
	return intel_smm_invocation_adapter_route_provision(&owner.session,
		&owner.arm, &owner.slot, composition, instance, evidence, topology,
		authority_policy, binding, page_verifier, page_receipt,
		protected_storage, protected_storage_context);
}
