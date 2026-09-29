/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_ROUTE_COMPOSITION_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_ROUTE_COMPOSITION_H

#include <cpu/intel/smm_invocation_adapter_route.h>

#if ENV_SMM || ENV_TEST
/*
 * Provision the one canonical StarBook MTL predecessor-presence route.
 *
 * This owner supplies only protected object identity.  The caller still owns
 * the loader composition, transaction authority, DMA policy and installation
 * timing.  No caller or SMI handler is installed by this contract.
 */
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
	void *protected_storage_context);
#endif

#endif
