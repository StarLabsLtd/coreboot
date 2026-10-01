/* SPDX-License-Identifier: GPL-2.0-only */

#include "authvar_presence_bootstrap_install.h"
#include "authvar_presence_authority_policy.h"
#include "authvar_presence_route_composition.h"
#include "dma_smm_receipt_provision.h"
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
#include "authvar_platform_smm.h"
#include <boot/payload_mm_authvar_service_receiver.h>
#endif

#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_invocation_topology.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "StarBook MTL presence bootstrap receiver is SMM-only"
#endif

enum response_phase { RESPONSE_EMPTY, RESPONSE_PENDING, RESPONSE_STAGED, RESPONSE_COMPLETE,
	RESPONSE_FAILED };

static struct {
	uint32_t phase;
	uint32_t initiator;
	uint64_t request_wire;
	const struct smm_invocation_save_state_ops *ops;
	struct starbook_mtl_presence_bootstrap_frame *frame;
	struct starbook_mtl_presence_bootstrap_frame snapshot;
#if CONFIG(PAYLOAD_BOOT_PRIVATE_BUFFER) && CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	struct bootmem_reservation_receipt boot_private;
#endif
} response;

static bool protected_storage(void *unused, const void *base, size_t size)
{
	const struct smm_invocation_runtime_view *view;

	(void)unused;
	return smm_invocation_runtime_view_get(&view) == CB_SUCCESS &&
		smm_invocation_runtime_range_is_protected(view, base, size) == CB_SUCCESS;
}

static enum cb_err bootstrap_failure(void)
{
#if CONFIG(PAYLOAD_BOOT_PRIVATE_BUFFER) && CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	struct payload_mm_authvar_presence_bootstrap *slot;

	starbook_mtl_boot_private_lease_close();
	slot = smm_get_payload_mm_authvar_presence_bootstrap();
	if (slot && protected_storage(NULL, slot, sizeof(*slot)))
		bootmem_reservation_receipt_close(&slot->boot_private_verifier);
	if (protected_storage(NULL, &response, sizeof(response))) {
		__atomic_store_n(&response.phase, RESPONSE_FAILED, __ATOMIC_RELEASE);
		memset(&response.boot_private, 0, sizeof(response.boot_private));
		memset(&response.snapshot, 0, sizeof(response.snapshot));
		response.frame = NULL;
		response.ops = NULL;
	}
#endif
	return CB_ERR;
}

enum starbook_mtl_presence_bootstrap_result
starbook_mtl_presence_bootstrap_receive(
	const struct smm_invocation_save_state_ops *active_ops)
{
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_topology topology;
	struct starbook_mtl_dma_smm_binding dma;
	const struct smm_dma_receipt_memory *memory;
	struct starbook_mtl_presence_bootstrap_frame *frame;
	struct starbook_mtl_presence_bootstrap_frame snapshot;
	struct payload_mm_authvar_presence_bootstrap_receipts receipts;
	struct payload_mm_authvar_presence_bootstrap *slot = NULL;
	uint64_t wire;
	uint32_t initiator = UINT32_MAX;
	const struct lb_authvar_service_endpoint no_endpoint = { 0 };
#if !CONFIG(PAYLOAD_BOOT_PRIVATE_BUFFER) || !CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	const struct bootmem_reservation_receipt no_private_receipt = { 0 };
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	struct smm_invocation_token token;
#endif

	if (!active_ops || !protected_storage(NULL, active_ops, sizeof(*active_ops)) ||
	    !active_ops->match_apmc_write || !active_ops->read_value || !active_ops->write_value ||
	    smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    smm_invocation_topology_read(runtime.topology, &topology) != CB_SUCCESS)
		goto failed;
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	if (active_ops->read_value(active_ops->context, topology.bsp_cpu, &wire) != CB_SUCCESS ||
	    smm_invocation_evidence_claimed_snapshot(runtime.evidence,
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE, wire, &token) != CB_SUCCESS ||
	    !token.bsp || token.initiator_cpu != topology.bsp_cpu ||
	    token.active_cpus != topology.active_cpus)
		goto failed;
	initiator = token.initiator_cpu;
#else
	for (uint32_t cpu = 0; cpu < topology.active_cpus; cpu++) {
		const enum smm_invocation_match match = active_ops->match_apmc_write(
			active_ops->context, cpu, SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);

		if (match == SMM_INVOCATION_MATCH_ERROR ||
		    (match == SMM_INVOCATION_MATCHED && initiator != UINT32_MAX))
			goto failed;
		if (match == SMM_INVOCATION_MATCHED)
			initiator = cpu;
	}
	if (initiator == UINT32_MAX ||
	    active_ops->read_value(active_ops->context, initiator, &wire) != CB_SUCCESS)
		goto failed;
#endif
	if ((uint32_t)wire != STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST)
		return STARBOOK_MTL_PRESENCE_NOT_BOOTSTRAP;
	if (initiator != topology.bsp_cpu ||
	    !protected_storage(NULL, &response, sizeof(response)) || response.phase != RESPONSE_EMPTY ||
	    !smm_get_dma_receipt_memory(&memory) ||
	    (uint32_t)(wire >> 32) != memory->frame.base ||
	    memory->frame.size < sizeof(*frame) ||
	    memory->frame.base % _Alignof(*frame) ||
	    memory->frame.base > UINT32_MAX - (sizeof(*frame) - 1U) ||
	    starbook_mtl_dma_smm_binding_get(&dma) != CB_SUCCESS || !dma.receipt)
		goto failed;
	frame = (void *)(uintptr_t)memory->frame.base;
	snapshot = *frame;
	if (snapshot.revision != STARBOOK_MTL_PRESENCE_BOOTSTRAP_REVISION ||
	    snapshot.size != sizeof(snapshot) ||
	    snapshot.state != STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST ||
	    snapshot.reserved || snapshot.initiator_cpu || snapshot.maximum_cpus ||
	    memcmp(&snapshot.service_endpoint, &no_endpoint, sizeof(no_endpoint)) ||
#if !CONFIG(PAYLOAD_BOOT_PRIVATE_BUFFER) || !CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	    memcmp(&snapshot.boot_private, &no_private_receipt, sizeof(no_private_receipt)) ||
#endif
	    memcmp(frame, &snapshot, sizeof(snapshot)))
		goto failed;
	receipts = snapshot.receipts;
	if (payload_mm_authvar_presence_bootstrap_receipts_import(&receipts) != CB_SUCCESS ||
	    memcmp(frame, &snapshot, sizeof(snapshot)) ||
	    !(slot = smm_get_payload_mm_authvar_presence_bootstrap()) ||
	    !protected_storage(NULL, slot, sizeof(*slot)) ||
	    slot->state != PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_READY)
		goto failed;
	response.initiator = initiator;
	response.request_wire = wire;
	response.ops = active_ops;
	response.frame = frame;
	response.snapshot = snapshot;
#if CONFIG(PAYLOAD_BOOT_PRIVATE_BUFFER) && CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	response.boot_private = snapshot.boot_private;
#endif
	__atomic_store_n(&response.phase, RESPONSE_PENDING, __ATOMIC_RELEASE);
	memset(&receipts, 0, sizeof(receipts));
	memset(&snapshot, 0, sizeof(snapshot));
	return STARBOOK_MTL_PRESENCE_BOOTSTRAP_IMPORTED;
failed:
	bootstrap_failure();
	return STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR;
}

enum cb_err starbook_mtl_presence_bootstrap_response_stage(
	const struct smm_invocation_save_state_ops *active_ops)
{
	uint64_t wire;

	if (!protected_storage(NULL, &response, sizeof(response)) ||
	    __atomic_load_n(&response.phase, __ATOMIC_ACQUIRE) != RESPONSE_PENDING ||
	    active_ops != response.ops ||
	    memcmp(response.frame, &response.snapshot, sizeof(response.snapshot)) ||
	    active_ops->match_apmc_write(active_ops->context, response.initiator,
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE) != SMM_INVOCATION_MATCHED ||
	    active_ops->read_value(active_ops->context, response.initiator, &wire) != CB_SUCCESS ||
	    wire != response.request_wire)
		return bootstrap_failure();
#if !CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	if (active_ops->write_value(active_ops->context, response.initiator,
		STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS) != CB_SUCCESS)
		return bootstrap_failure();
#else
	/* The owner copies only public metadata into this protected response body. */
	if (payload_mm_authvar_service_descriptor_copy(&response.snapshot.service_endpoint) !=
		CB_SUCCESS)
		return bootstrap_failure();
	response.frame->service_endpoint = response.snapshot.service_endpoint;
#endif
	__atomic_store_n(&response.phase, RESPONSE_STAGED, __ATOMIC_RELEASE);
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_presence_bootstrap_response_publish(void)
{
	const struct smm_dma_receipt_memory *memory;
	const struct payload_mm_authvar_presence_transaction_binding *binding;

	if (!protected_storage(NULL, &response, sizeof(response)) ||
	    __atomic_load_n(&response.phase, __ATOMIC_ACQUIRE) != RESPONSE_STAGED ||
	    !smm_get_dma_receipt_memory(&memory) ||
	    memory->frame.base != (uintptr_t)response.frame ||
	    memory->frame.size < sizeof(response.snapshot) ||
	    memcmp(response.frame, &response.snapshot, sizeof(response.snapshot)) ||
	    payload_mm_authvar_presence_bootstrap_binding_get(&binding) != CB_SUCCESS ||
	    binding->initiator_cpu != response.initiator)
		return bootstrap_failure();
	response.frame->initiator_cpu = binding->initiator_cpu;
	response.frame->maximum_cpus = binding->maximum_cpus;
	__atomic_store_n(&response.frame->state, STARBOOK_MTL_PRESENCE_BOOTSTRAP_ACCEPTED,
		__ATOMIC_RELEASE);
	memset(&response.snapshot, 0, sizeof(response.snapshot));
	response.frame = NULL;
	response.ops = NULL;
	__atomic_store_n(&response.phase, RESPONSE_COMPLETE, __ATOMIC_RELEASE);
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_presence_bootstrap_route_install(void)
{
	struct smm_invocation_runtime_binding runtime;
	const struct payload_mm_authvar_presence_route_authority_policy *policy;
	const struct payload_mm_authvar_presence_transaction_binding *binding;
	struct payload_mm_authvar_presence_bootstrap *slot = NULL;

	if (smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    payload_mm_authvar_presence_bootstrap_binding_get(&binding) != CB_SUCCESS ||
	    starbook_mtl_authvar_presence_authority_policy_get(&policy) != CB_SUCCESS ||
	    !(slot = smm_get_payload_mm_authvar_presence_bootstrap()) ||
	    !protected_storage(NULL, slot, sizeof(*slot)))
		goto failed;
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	if (starbook_mtl_authvar_service_bootstrap_install() != CB_SUCCESS)
		goto failed;
#endif
	if (starbook_mtl_authvar_presence_route_composition_provision(
		runtime.composition, runtime.instance, runtime.evidence, runtime.topology,
		policy, binding, &slot->page_verifier, &slot->page_receipt,
		protected_storage, NULL) != SMM_INVOCATION_TRY_SUCCESS)
		goto failed;
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	if (payload_mm_authvar_service_finalize() != CB_SUCCESS)
		goto failed;
#if CONFIG(PAYLOAD_BOOT_PRIVATE_BUFFER) && CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	if (!protected_storage(NULL, &response, sizeof(response)) ||
	    __atomic_load_n(&response.phase, __ATOMIC_ACQUIRE) != RESPONSE_PENDING ||
	    memcmp(response.frame, &response.snapshot, sizeof(response.snapshot)) ||
	    starbook_mtl_boot_private_lease_prepare(&slot->boot_private_verifier,
		&response.boot_private) != CB_SUCCESS ||
	    memcmp(response.frame, &response.snapshot, sizeof(response.snapshot)))
		goto failed;
#endif
	return CB_SUCCESS;
#else
	return CB_SUCCESS;
#endif
failed:
	return bootstrap_failure();
}
