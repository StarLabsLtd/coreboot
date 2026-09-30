/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_bootstrap.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_invocation_topology.h>

#if !ENV_SMM && !ENV_TEST
#error "Presence bootstrap ownership facts are SMM-only"
#endif

__weak struct payload_mm_authvar_presence_bootstrap *
smm_get_payload_mm_authvar_presence_bootstrap(void)
{
	return NULL;
}

enum cb_err payload_mm_authvar_presence_bootstrap_binding_get(
	const struct payload_mm_authvar_presence_transaction_binding **binding)
{
	const struct smm_invocation_runtime_view *view;
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_topology topology;
	struct payload_mm_authvar_presence_bootstrap *slot;

	if (!binding || (uintptr_t)binding % _Alignof(*binding) ||
	    smm_invocation_runtime_view_get(&view) != CB_SUCCESS ||
	    smm_invocation_runtime_range_is_protected(view, binding, sizeof(*binding)) !=
		CB_SUCCESS)
		return CB_ERR;
	slot = smm_get_payload_mm_authvar_presence_bootstrap();
	if (!slot || (uintptr_t)slot % _Alignof(*slot) ||
	    smm_invocation_runtime_range_is_protected(view, slot, sizeof(*slot)) !=
		CB_SUCCESS)
		goto fail;
	/* Output aliases must not modify the protected ownership record. */
	if ((uintptr_t)binding <= (uintptr_t)slot ?
	    (uintptr_t)slot - (uintptr_t)binding < sizeof(*binding) :
	    (uintptr_t)binding - (uintptr_t)slot < sizeof(*slot))
		return CB_ERR;
	if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_READY ||
	    slot->reserved || slot->cold_boot_proven != 1U ||
	    !payload_mm_authvar_presence_transaction_result(&slot->binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE) ||
	    smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    smm_invocation_loader_instance_read(runtime.instance, &instance) != CB_SUCCESS ||
	    smm_invocation_topology_read(runtime.topology, &topology) != CB_SUCCESS ||
	    instance.lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD ||
	    slot->loader_lifecycle != instance.lifecycle ||
	    !smm_invocation_loader_instance_nonce_equal(slot->loader_nonce,
		instance.loader_instance_nonce) ||
	    slot->binding.initiator_cpu != topology.bsp_cpu ||
	    slot->binding.maximum_cpus != topology.active_cpus)
		goto fail;
	*binding = &slot->binding;
	return CB_SUCCESS;
fail:
	*binding = NULL;
	return CB_ERR;
}
