/* SPDX-License-Identifier: GPL-2.0-only */

#include "authvar_presence_bootstrap_install.h"
#include "authvar_presence_authority_policy.h"
#include "authvar_presence_route_composition.h"
#include "dma_smm_receipt_provision.h"

#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_invocation_topology.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "StarBook MTL presence bootstrap receiver is SMM-only"
#endif

enum response_phase { RESPONSE_EMPTY, RESPONSE_PENDING, RESPONSE_STAGED, RESPONSE_COMPLETE };

static struct {
	uint32_t phase;
	uint32_t initiator;
	const struct smm_invocation_save_state_ops *ops;
	struct starbook_mtl_presence_bootstrap_frame *frame;
	struct starbook_mtl_presence_bootstrap_frame snapshot;
} response;

static bool protected_storage(void *unused, const void *base, size_t size)
{
	const struct smm_invocation_runtime_view *view;

	(void)unused;
	return smm_invocation_runtime_view_get(&view) == CB_SUCCESS &&
		smm_invocation_runtime_range_is_protected(view, base, size) == CB_SUCCESS;
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
	struct payload_mm_authvar_presence_bootstrap *slot;
	uint64_t wire;
	uint32_t initiator = UINT32_MAX;

	if (!active_ops || !protected_storage(NULL, active_ops, sizeof(*active_ops)) ||
	    !active_ops->match_apmc_write || !active_ops->read_value || !active_ops->write_value ||
	    smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    smm_invocation_topology_read(runtime.topology, &topology) != CB_SUCCESS)
		return STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR;
	for (uint32_t cpu = 0; cpu < topology.active_cpus; cpu++) {
		const enum smm_invocation_match match = active_ops->match_apmc_write(
			active_ops->context, cpu, SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);

		if (match == SMM_INVOCATION_MATCH_ERROR ||
		    (match == SMM_INVOCATION_MATCHED && initiator != UINT32_MAX))
			return STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR;
		if (match == SMM_INVOCATION_MATCHED)
			initiator = cpu;
	}
	if (initiator == UINT32_MAX ||
	    active_ops->read_value(active_ops->context, initiator, &wire) != CB_SUCCESS)
		return STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR;
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
		return STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR;
	frame = (void *)(uintptr_t)memory->frame.base;
	snapshot = *frame;
	if (snapshot.revision != STARBOOK_MTL_PRESENCE_BOOTSTRAP_REVISION ||
	    snapshot.size != sizeof(snapshot) ||
	    snapshot.state != STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST ||
	    snapshot.reserved || snapshot.initiator_cpu || snapshot.maximum_cpus ||
	    memcmp(frame, &snapshot, sizeof(snapshot)))
		return STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR;
	receipts = snapshot.receipts;
	if (payload_mm_authvar_presence_bootstrap_receipts_import(&receipts) != CB_SUCCESS ||
	    memcmp(frame, &snapshot, sizeof(snapshot)) ||
	    !(slot = smm_get_payload_mm_authvar_presence_bootstrap()) ||
	    !protected_storage(NULL, slot, sizeof(*slot)) ||
	    slot->state != PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_READY)
		return STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR;
	response.initiator = initiator;
	response.ops = active_ops;
	response.frame = frame;
	response.snapshot = snapshot;
	__atomic_store_n(&response.phase, RESPONSE_PENDING, __ATOMIC_RELEASE);
	memset(&receipts, 0, sizeof(receipts));
	memset(&snapshot, 0, sizeof(snapshot));
	return STARBOOK_MTL_PRESENCE_BOOTSTRAP_IMPORTED;
}

enum cb_err starbook_mtl_presence_bootstrap_response_stage(
	const struct smm_invocation_save_state_ops *active_ops)
{
	if (!protected_storage(NULL, &response, sizeof(response)) ||
	    __atomic_load_n(&response.phase, __ATOMIC_ACQUIRE) != RESPONSE_PENDING ||
	    active_ops != response.ops ||
	    memcmp(response.frame, &response.snapshot, sizeof(response.snapshot)) ||
	    active_ops->write_value(active_ops->context, response.initiator,
		STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS) != CB_SUCCESS)
		return CB_ERR;
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
		return CB_ERR;
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
	struct payload_mm_authvar_presence_bootstrap *slot;

	if (smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    payload_mm_authvar_presence_bootstrap_binding_get(&binding) != CB_SUCCESS ||
	    starbook_mtl_authvar_presence_authority_policy_get(&policy) != CB_SUCCESS ||
	    !(slot = smm_get_payload_mm_authvar_presence_bootstrap()) ||
	    !protected_storage(NULL, slot, sizeof(*slot)))
		return CB_ERR;
	return starbook_mtl_authvar_presence_route_composition_provision(
		runtime.composition, runtime.instance, runtime.evidence, runtime.topology,
		policy, binding, &slot->page_verifier, &slot->page_receipt,
		protected_storage, NULL) == SMM_INVOCATION_TRY_SUCCESS ? CB_SUCCESS : CB_ERR;
}
