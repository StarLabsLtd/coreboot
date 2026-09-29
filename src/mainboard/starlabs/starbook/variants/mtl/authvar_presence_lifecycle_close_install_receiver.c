/* SPDX-License-Identifier: GPL-2.0-only */

#include "authvar_presence_lifecycle_close_install.h"

#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "StarBook MTL lifecycle-close install receiver is SMM-only"
#endif

enum install_state { INSTALL_EMPTY, INSTALLING, INSTALLED, INSTALL_FAILED,
	INSTALL_POISONED };

struct install_owner {
	uint32_t state;
	uint32_t reserved;
	struct payload_mm_authvar_presence_lifecycle_close_route route;
} __aligned(8);

static struct install_owner owner;

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool frame_request_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_install_frame *frame)
{
	return frame && !((uintptr_t)frame % _Alignof(*frame)) &&
		frame->revision ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REVISION &&
		frame->size == sizeof(*frame) && frame->state ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REQUEST &&
		!frame->reserved;
}

enum cb_err starbook_mtl_authvar_presence_lifecycle_close_install_receive(
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		*dependencies)
{
	struct payload_mm_authvar_presence_lifecycle_close_install_frame *frame = NULL;
	struct payload_mm_authvar_presence_lifecycle_close_install_frame snapshot;
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt receipt = { 0 };
	struct smm_invocation_runtime_binding binding;
	struct smm_invocation_topology topology;
	const struct smm_invocation_save_state_ops *ops;
	struct payload_mm_authvar_presence_transaction_slot *slot;
	uint64_t wire = 0;
	uint32_t matched_cpu = UINT32_MAX;
	uint32_t expected = INSTALL_EMPTY;
	enum cb_err status = CB_ERR;

	if (!dependencies || !dependencies->internal || !dependencies->active_ops ||
	    !dependencies->protected_storage ||
	    !dependencies->communication_range_valid ||
	    dependencies->communication_range_context_size >
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX ||
	    !!dependencies->communication_range_context !=
		!!dependencies->communication_range_context_size ||
	    !dependencies->protected_storage(dependencies->protected_storage_context,
		&owner, sizeof(owner)) ||
	    !dependencies->protected_storage(dependencies->protected_storage_context,
		dependencies, sizeof(*dependencies)) ||
	    !dependencies->protected_storage(dependencies->protected_storage_context,
		dependencies->internal, sizeof(*dependencies->internal)) ||
	    !dependencies->protected_storage(dependencies->protected_storage_context,
		dependencies->active_ops, sizeof(*dependencies->active_ops)) ||
	    !dependencies->protected_storage(dependencies->protected_storage_context,
		(const void *)(uintptr_t)dependencies->communication_range_valid, 1U) ||
	    (dependencies->communication_range_context_size &&
	     !dependencies->protected_storage(dependencies->protected_storage_context,
		dependencies->communication_range_context,
		dependencies->communication_range_context_size)) ||
	    !__atomic_compare_exchange_n(&owner.state, &expected, INSTALLING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	ops = dependencies->active_ops;
	if (!ops->match_apmc_write || !ops->read_value || !ops->write_value ||
	    ops->context_size > SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX ||
	    !!ops->context != !!ops->context_size ||
	    (ops->context_size &&
	     !dependencies->protected_storage(dependencies->protected_storage_context,
		ops->context, ops->context_size)) ||
	    !dependencies->protected_storage(dependencies->protected_storage_context,
		(const void *)(uintptr_t)ops->match_apmc_write, 1U) ||
	    !dependencies->protected_storage(dependencies->protected_storage_context,
		(const void *)(uintptr_t)ops->read_value, 1U) ||
	    !dependencies->protected_storage(dependencies->protected_storage_context,
		(const void *)(uintptr_t)ops->write_value, 1U) ||
	    smm_invocation_runtime_binding_get(&binding) != CB_SUCCESS ||
	    smm_invocation_topology_read(binding.topology, &topology) != CB_SUCCESS)
		goto out;
	for (uint32_t cpu = 0; cpu < topology.active_cpus; cpu++) {
		const enum smm_invocation_match match =
			ops->match_apmc_write(ops->context, cpu,
				SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);

		if (match == SMM_INVOCATION_MATCH_ERROR ||
		    (match == SMM_INVOCATION_MATCHED && matched_cpu != UINT32_MAX))
			goto out;
		if (match == SMM_INVOCATION_MATCHED)
			matched_cpu = cpu;
	}
	if (matched_cpu == UINT32_MAX ||
	    ops->read_value(ops->context, matched_cpu, &wire) != CB_SUCCESS ||
	    (uint32_t)wire !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_REQUEST)
		goto out;
	frame = (void *)(uintptr_t)(uint32_t)(wire >> 32);
	if (!dependencies->communication_range_valid(
		dependencies->communication_range_context, (uint64_t)(uintptr_t)frame,
		sizeof(*frame)) || !frame_request_valid(frame))
		goto out;
	snapshot = *frame;
	if (!frame_request_valid(&snapshot) || memcmp(frame, &snapshot, sizeof(snapshot)) ||
	    !(slot = smm_get_payload_mm_authvar_presence_transaction_slot()) ||
	    payload_mm_authvar_presence_lifecycle_close_route_provision(&owner.route,
		&snapshot.request, slot, binding.composition, binding.instance,
		binding.evidence, binding.topology, ops, dependencies->internal,
		binding.evidence->closed_generation, dependencies->protected_storage,
		dependencies->protected_storage_context, &receipt) != CB_SUCCESS)
		goto out;
	if (memcmp(frame, &snapshot, sizeof(snapshot))) {
		__atomic_store_n(&owner.state, INSTALL_POISONED, __ATOMIC_RELEASE);
		platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
	}
	if (!dependencies->communication_range_valid(
		dependencies->communication_range_context, (uint64_t)(uintptr_t)frame,
		sizeof(*frame))) {
		__atomic_store_n(&owner.state, INSTALL_POISONED, __ATOMIC_RELEASE);
		platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
	}
	frame->receipt = receipt;
	__atomic_store_n(&frame->state,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_RECEIPT,
		__ATOMIC_RELEASE);
	if (memcmp(&frame->request, &snapshot.request, sizeof(frame->request)) ||
	    memcmp(&frame->receipt, &receipt, sizeof(receipt)) ||
	    frame->revision != snapshot.revision || frame->size != snapshot.size ||
	    frame->reserved || frame->state !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_RECEIPT) {
		__atomic_store_n(&owner.state, INSTALL_POISONED, __ATOMIC_RELEASE);
		platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
	}
	if (ops->write_value(ops->context, matched_cpu,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_SUCCESS) !=
		CB_SUCCESS) {
		__atomic_store_n(&owner.state, INSTALL_POISONED, __ATOMIC_RELEASE);
		platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
	}
	__atomic_store_n(&owner.state, INSTALLED, __ATOMIC_RELEASE);
	status = CB_SUCCESS;
out:
	if (status != CB_SUCCESS) {
		if (frame && dependencies->communication_range_valid(
			dependencies->communication_range_context,
			(uint64_t)(uintptr_t)frame, sizeof(*frame))) {
			scrub(&frame->receipt, sizeof(frame->receipt));
			__atomic_store_n(&frame->state,
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REJECTED,
				__ATOMIC_RELEASE);
		}
		__atomic_store_n(&owner.state, INSTALL_FAILED, __ATOMIC_RELEASE);
	}
	scrub(&snapshot, sizeof(snapshot));
	scrub(&receipt, sizeof(receipt));
	scrub(&binding, sizeof(binding));
	scrub(&topology, sizeof(topology));
	return status;
}
