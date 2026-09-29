/* SPDX-License-Identifier: GPL-2.0-only */

#include "authvar_presence_lifecycle_close_install.h"
#if CONFIG(STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)
#include "dma_smm_receipt_provision.h"
#endif

#include <cpu/intel/smm_invocation_adapter_provider.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_pre_lock_dispatch.h>
#include <intelblocks/smm_invocation_cause.h>

#if !ENV_SMM && !ENV_TEST
#error "StarBook MTL lifecycle-close dispatcher is SMM-only"
#endif

enum dispatch_state { DISPATCH_IDLE, DISPATCH_ARMING, DISPATCH_ACTIVE,
	DISPATCH_DEPARTING, DISPATCH_INSTALLING, DISPATCH_INSTALL_COMPLETE,
	DISPATCH_RECEIPT_PROVISIONING, DISPATCH_RECEIPT_COMPLETE,
	DISPATCH_POISONED };

static struct {
	uint32_t state;
	uint32_t install_departures;
	uint64_t provider_generation;
} owner;

__weak enum cb_err starbook_mtl_authvar_presence_lifecycle_close_install_policy(
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		**dependencies,
	const struct smm_invocation_save_state_ops *active_ops)
{
	(void)active_ops;
	if (dependencies)
		*dependencies = NULL;
	return CB_ERR;
}

void __noreturn
platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop(void)
{
	smm_invocation_platform_fail_stop();
}

static void __noreturn fail_stop(void)
{
	__atomic_store_n(&owner.state, DISPATCH_POISONED, __ATOMIC_RELEASE);
	platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
}

static void wait_for_state(uint32_t expected, uint32_t max_polls)
{
	for (uint32_t poll = 0; poll < max_polls; poll++) {
		if (__atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) == expected)
			return;
		__asm__ __volatile__("pause");
	}
	fail_stop();
}

static uint32_t wait_for_active_mode(uint32_t max_polls)
{
	for (uint32_t poll = 0; poll < max_polls; poll++) {
		const uint32_t state = __atomic_load_n(&owner.state, __ATOMIC_ACQUIRE);

		if (state == DISPATCH_ACTIVE || state == DISPATCH_INSTALLING ||
		    state == DISPATCH_INSTALL_COMPLETE)
			return state;
		__asm__ __volatile__("pause");
	}
	fail_stop();
}

static enum smm_pre_lock_dispatch_result install_participant(
	uint32_t active_cpus, uint32_t max_polls)
{
	wait_for_state(DISPATCH_INSTALL_COMPLETE, max_polls);
	if (__atomic_add_fetch(&owner.install_departures, 1U, __ATOMIC_ACQ_REL) >=
		active_cpus)
		fail_stop();
	return SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED;
}

static void wait_for_install_departures(uint32_t expected, uint32_t max_polls)
{
	for (uint32_t poll = 0; poll < max_polls; poll++) {
		if (__atomic_load_n(&owner.install_departures, __ATOMIC_ACQUIRE) ==
			expected)
			return;
		__asm__ __volatile__("pause");
	}
	fail_stop();
}

#if CONFIG(STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)
static enum smm_pre_lock_dispatch_result receipt_provision(
	bool bsp, uint32_t active_cpus, uint32_t max_polls)
{
	const struct smm_invocation_save_state_ops *active_ops;
	uint32_t expected;

	if (!bsp) {
		wait_for_state(DISPATCH_RECEIPT_COMPLETE, max_polls);
		if (__atomic_add_fetch(&owner.install_departures, 1U,
			__ATOMIC_ACQ_REL) >= active_cpus)
			fail_stop();
		return SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED;
	}
	expected = DISPATCH_IDLE;
	if (!__atomic_compare_exchange_n(&owner.state, &expected,
		DISPATCH_ARMING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
	    intel_smm_invocation_adapter_provider_provision(&active_ops) !=
		SMM_INVOCATION_TRY_SUCCESS ||
	    intel_smm_invocation_adapter_provider_arm(
		&owner.provider_generation) != SMM_INVOCATION_TRY_SUCCESS)
		fail_stop();
	__atomic_store_n(&owner.state, DISPATCH_RECEIPT_PROVISIONING,
		__ATOMIC_RELEASE);
	if (starbook_mtl_dma_receipt_provision_receive(active_ops) != CB_SUCCESS ||
	    intel_smm_invocation_adapter_provider_retire(
		owner.provider_generation) != SMM_INVOCATION_TRY_SUCCESS)
		fail_stop();
	owner.provider_generation = 0;
	__atomic_store_n(&owner.state, DISPATCH_RECEIPT_COMPLETE,
		__ATOMIC_RELEASE);
	wait_for_install_departures(active_cpus - 1U, max_polls);
	__atomic_store_n(&owner.install_departures, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&owner.state, DISPATCH_IDLE, __ATOMIC_RELEASE);
	return SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED;
}
#endif

enum smm_pre_lock_dispatch_result smm_pre_lock_dispatch(
	uint32_t cpu, uint32_t initial_apic_id)
{
	struct starbook_mtl_authvar_presence_lifecycle_close_installed_route installed;
	struct starbook_mtl_authvar_presence_lifecycle_close_installed_route rechecked;
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		*install_policy;
	const struct smm_invocation_save_state_ops *active_ops;
	struct smm_invocation_runtime_binding runtime;
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DMA_POLICY)
	struct starbook_mtl_dma_smm_binding dma_binding;
#endif
	struct smm_invocation_topology topology;
	struct smm_invocation_entry_cause cause = { 0 };
	struct smm_invocation_entry_ticket ticket = { 0 };
	struct smm_apmc_selection_receipt selection = { 0 };
	const struct smm_invocation_entry_policy policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(policy),
		.max_polls = SMM_INVOCATION_ENTRY_MAX_POLLS,
	};
	uint32_t runtime_cpus;
	uint32_t expected;
	uint32_t mode;
	bool bsp;
	enum intel_smm_invocation_cause_result classified;

	if (smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    smm_invocation_topology_read(runtime.topology, &topology) != CB_SUCCESS ||
	    !topology.active_cpus || cpu >= topology.active_cpus ||
	    initial_apic_id != topology.initial_apic_ids[cpu])
		fail_stop();
	runtime_cpus = topology.active_cpus;
#if CONFIG(STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)
	classified = intel_smm_invocation_private_apmc_cause(runtime.composition,
		runtime.topology, runtime.instance, runtime.evidence, &runtime_cpus,
		SMM_APMC_STARBOOK_MTL_DMA_RECEIPT, &cause);
	if (classified == INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID)
		return receipt_provision(cpu == topology.bsp_cpu,
			topology.active_cpus, policy.max_polls);
	if (classified != INTEL_SMM_INVOCATION_CAUSE_NOT_PRIVATE)
		fail_stop();
#endif
	classified = intel_smm_invocation_private_apmc_cause(runtime.composition,
		runtime.topology, runtime.instance, runtime.evidence, &runtime_cpus,
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE, &cause);
	if (classified == INTEL_SMM_INVOCATION_CAUSE_NOT_PRIVATE)
		return SMM_PRE_LOCK_DISPATCH_NOT_HANDLED;
	if (classified != INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID)
		fail_stop();
	bsp = cpu == topology.bsp_cpu;
	if (bsp) {
		expected = DISPATCH_IDLE;
		if (!__atomic_compare_exchange_n(&owner.state, &expected,
			DISPATCH_ARMING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
		    intel_smm_invocation_adapter_provider_provision(&active_ops) !=
			SMM_INVOCATION_TRY_SUCCESS ||
		    intel_smm_invocation_adapter_provider_arm(
			&owner.provider_generation) != SMM_INVOCATION_TRY_SUCCESS)
			fail_stop();
		if (starbook_mtl_authvar_presence_lifecycle_close_installed_route(
			&installed) == CB_SUCCESS) {
			if (installed.retained_ops != active_ops)
				fail_stop();
			__atomic_store_n(&owner.state, DISPATCH_ACTIVE, __ATOMIC_RELEASE);
		} else {
			if (starbook_mtl_authvar_presence_lifecycle_close_install_policy(
				&install_policy, active_ops) != CB_SUCCESS || !install_policy)
				fail_stop();
			__atomic_store_n(&owner.state, DISPATCH_INSTALLING,
				__ATOMIC_RELEASE);
			if (starbook_mtl_authvar_presence_lifecycle_close_install_receive(
				install_policy, active_ops) != CB_SUCCESS ||
			    intel_smm_invocation_adapter_provider_retire(
				owner.provider_generation) != SMM_INVOCATION_TRY_SUCCESS)
				fail_stop();
			owner.provider_generation = 0;
			__atomic_store_n(&owner.state, DISPATCH_INSTALL_COMPLETE,
				__ATOMIC_RELEASE);
			wait_for_install_departures(topology.active_cpus - 1U,
				policy.max_polls);
			__atomic_store_n(&owner.install_departures, 0U, __ATOMIC_RELAXED);
			__atomic_store_n(&owner.state, DISPATCH_IDLE, __ATOMIC_RELEASE);
			return SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED;
		}
	} else {
		mode = wait_for_active_mode(policy.max_polls);
		if (mode != DISPATCH_ACTIVE)
			return install_participant(topology.active_cpus, policy.max_polls);
		if (starbook_mtl_authvar_presence_lifecycle_close_installed_route(
			&installed) != CB_SUCCESS)
			fail_stop();
	}
	if (payload_mm_authvar_presence_lifecycle_close_route_arrive(installed.route,
		&cause, &policy, cpu, initial_apic_id, &ticket) != CB_SUCCESS)
		fail_stop();
	if (bsp) {
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DMA_POLICY)
		if (starbook_mtl_dma_smm_binding_get(&dma_binding) != CB_SUCCESS ||
		    !dma_binding.receipt)
			fail_stop();
#endif
		if (smm_apmc_command_select(
			SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE, &selection) !=
			SMM_APMC_SELECT_ENABLED ||
		    payload_mm_authvar_presence_lifecycle_close_route_dispatch_locked(
			installed.route, &ticket, &selection) !=
			SMM_APMC_CONSUMED_SUCCESS)
			fail_stop();
		payload_mm_authvar_presence_lifecycle_close_route_prepare_lock_release(
			installed.route, &ticket);
		__atomic_store_n(&owner.state, DISPATCH_DEPARTING, __ATOMIC_RELEASE);
	} else {
		wait_for_state(DISPATCH_DEPARTING, policy.max_polls);
	}
	if (payload_mm_authvar_presence_lifecycle_close_route_depart(installed.route,
		&ticket) != (bsp ?
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_BSP_EOS_CONSUMED :
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_PARTICIPANT_DEPARTED))
		fail_stop();
	if (!bsp)
		return SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED;
	if (intel_smm_invocation_adapter_provider_retire(
		owner.provider_generation) != SMM_INVOCATION_TRY_SUCCESS ||
	    starbook_mtl_authvar_presence_lifecycle_close_installed_route(
		&rechecked) != CB_SUCCESS || rechecked.route != installed.route ||
	    rechecked.retained_ops != installed.retained_ops)
		fail_stop();
	owner.provider_generation = 0;
	__atomic_store_n(&owner.state, DISPATCH_IDLE, __ATOMIC_RELEASE);
	return SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED;
}
