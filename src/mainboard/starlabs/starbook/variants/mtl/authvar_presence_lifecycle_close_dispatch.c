/* SPDX-License-Identifier: GPL-2.0-only */

#include "authvar_presence_lifecycle_close_install.h"
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER) && \
	CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY) && \
	CONFIG(STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)
#include "authvar_presence_bootstrap_install.h"
#endif
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM)
#include "authvar_presence_s3_rearm.h"
#endif
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_COLD_S3_RECORD)
#include "authvar_presence_s3_cold.h"
#endif
#if CONFIG(STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)
#include "dma_smm_receipt_provision.h"
#endif

#include <cpu/intel/smm_invocation_adapter_provider.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_pre_lock_dispatch.h>
#include <intelblocks/smm_invocation_cause.h>
#include <boot/payload_mm_authvar_presence_s3_backing.h>
#include <string.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
#include <boot/payload_mm_authvar_smm_bootstrap.h>
#endif

#if !ENV_SMM && !ENV_TEST
#error "StarBook MTL lifecycle-close dispatcher is SMM-only"
#endif

enum dispatch_state { DISPATCH_IDLE, DISPATCH_ARMING, DISPATCH_ACTIVE,
	DISPATCH_DEPARTING, DISPATCH_INSTALLING, DISPATCH_INSTALL_COMPLETE,
	DISPATCH_RECEIPT_PROVISIONING, DISPATCH_RECEIPT_COMPLETE,
	DISPATCH_POISONED,
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
/* The full service composition uses the existing entry ledger, not held APs. */
	DISPATCH_BOOTSTRAP_COLLECTING, DISPATCH_SERVICE_INSTALLING,
#endif
};

#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM)
enum dispatch_mode { MODE_NONE, MODE_S3_REARM, MODE_S3_PUBLIC };
#endif

static struct {
	uint32_t state;
	uint32_t install_departures;
	uint64_t provider_generation;
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM) || \
	CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	const struct smm_invocation_save_state_ops *active_ops;
#endif
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM)
	uint32_t mode;
	uint32_t mode_reserved;
	struct payload_mm_authvar_presence_s3_facts rearm_facts;
	struct starbook_mtl_dma_smm_epoch_range rearm_ranges[2];
#endif
} owner;

#if ENV_TEST
void starbook_mtl_authvar_presence_lifecycle_close_dispatch_reset_test(void)
{
	memset(&owner, 0, sizeof(owner));
}
#endif

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
		    state == DISPATCH_INSTALL_COMPLETE
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
		    || state == DISPATCH_BOOTSTRAP_COLLECTING || state == DISPATCH_SERVICE_INSTALLING
#endif
		    )
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

#if ENV_TEST
uint32_t starbook_mtl_authvar_presence_lifecycle_close_install_departures_test(void)
{
	return __atomic_load_n(&owner.install_departures, __ATOMIC_ACQUIRE);
}
#endif

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

#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM)
static void __noreturn s3_fail_stop(void)
{
	starbook_mtl_authvar_presence_s3_rearm_poison();
	fail_stop();
}

static void s3_wait_for_state(uint32_t expected, uint32_t max_polls)
{
	for (uint32_t poll = 0; poll < max_polls; poll++) {
		if (__atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) == expected)
			return;
		__asm__ __volatile__("pause");
	}
	s3_fail_stop();
}

static void s3_wait_for_eos(struct smm_invocation_evidence *evidence,
	const struct smm_invocation_entry_ticket *ticket, uint32_t max_polls)
{
	for (uint32_t poll = 0; poll < max_polls; poll++) {
		if (smm_invocation_entry_eos_ready(evidence, ticket))
			return;
		__asm__ __volatile__("pause");
	}
	s3_fail_stop();
}

#define fail_stop s3_fail_stop
static enum smm_pre_lock_dispatch_result s3_dispatch(
	const struct smm_invocation_runtime_binding *runtime,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	uint32_t cpu, uint32_t initial_apic_id)
{
	const struct payload_mm_authvar_presence_s3_backing *backing;
	struct starbook_mtl_authvar_presence_s3_binding binding;
	struct smm_invocation_entry_ticket ticket = { 0 };
	struct smm_invocation_token token = { 0 };
	struct smm_apmc_selection_receipt selection = { 0 };
	uint64_t wire;
	uint32_t expected;
	bool bsp = cpu == topology->bsp_cpu;

	if (bsp) {
		expected = DISPATCH_IDLE;
		if (!__atomic_compare_exchange_n(&owner.state, &expected,
			DISPATCH_ARMING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
		    intel_smm_invocation_adapter_provider_provision(
			&owner.active_ops) != SMM_INVOCATION_TRY_SUCCESS ||
		    intel_smm_invocation_adapter_provider_arm(
			&owner.provider_generation) != SMM_INVOCATION_TRY_SUCCESS ||
		    owner.active_ops->read_value(owner.active_ops->context, cpu,
			&wire) != CB_SUCCESS)
			fail_stop();
		if (starbook_mtl_authvar_presence_s3_binding_get(&binding) ==
			CB_SUCCESS) {
			if (binding.retained_ops != owner.active_ops ||
			    wire == STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_REQUEST)
				fail_stop();
			owner.mode = MODE_S3_PUBLIC;
		} else {
			backing = smm_get_payload_mm_authvar_presence_s3_backing();
			if (wire != STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_REQUEST ||
			    !backing)
				fail_stop();
			owner.rearm_ranges[0] =
				(struct starbook_mtl_dma_smm_epoch_range) {
					.base = backing->presence.communication_base,
					.size = PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_PAGE_SIZE,
				};
			owner.rearm_ranges[1] =
				(struct starbook_mtl_dma_smm_epoch_range) {
					.base = backing->lifecycle_close.communication_base,
					.size =
						PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_PAGE_SIZE,
				};
			if (starbook_mtl_dma_smm_epoch_prepare(runtime->instance,
				runtime->evidence, runtime->topology,
				owner.rearm_ranges) != CB_SUCCESS)
				fail_stop();
			owner.mode = MODE_S3_REARM;
		}
		__atomic_store_n(&owner.state, DISPATCH_ACTIVE, __ATOMIC_RELEASE);
	} else {
		s3_wait_for_state(DISPATCH_ACTIVE, policy->max_polls);
	}

	if (owner.mode == MODE_S3_PUBLIC) {
		if (starbook_mtl_authvar_presence_s3_binding_get(&binding) !=
			CB_SUCCESS || binding.retained_ops != owner.active_ops ||
		    payload_mm_authvar_presence_lifecycle_close_s3_route_arrive(
			binding.route, cause, policy, cpu, initial_apic_id,
			&ticket) != CB_SUCCESS)
			fail_stop();
		if (bsp) {
			if (smm_apmc_command_select(
				SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
				&selection) != SMM_APMC_SELECT_ENABLED ||
			    payload_mm_authvar_presence_lifecycle_close_s3_route_dispatch_locked(
				binding.route, &ticket, &selection) !=
				SMM_APMC_CONSUMED_SUCCESS)
				fail_stop();
			payload_mm_authvar_presence_lifecycle_close_s3_route_prepare_lock_release(
				binding.route, &ticket);
			__atomic_store_n(&owner.state, DISPATCH_DEPARTING,
				__ATOMIC_RELEASE);
		} else {
			s3_wait_for_state(DISPATCH_DEPARTING, policy->max_polls);
		}
		if (payload_mm_authvar_presence_lifecycle_close_s3_route_depart(
			binding.route, &ticket) != (bsp ?
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_BSP_EOS_CONSUMED :
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_PARTICIPANT_DEPARTED))
			fail_stop();
	} else if (owner.mode == MODE_S3_REARM) {
		if (smm_invocation_entry_arrive(runtime->evidence, cause, policy,
			cause->loader_instance_nonce,
			SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE, cpu,
			initial_apic_id, &ticket) != CB_SUCCESS)
			fail_stop();
		if (bsp) {
			if (smm_apmc_command_select(
				SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
				&selection) != SMM_APMC_SELECT_ENABLED ||
			    smm_apmc_command_consume(
				SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
				SMM_APMC_OWNER_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
				&selection) != SMM_APMC_CONSUMED_SUCCESS ||
			    smm_invocation_evidence_claim(runtime->evidence,
				SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
				STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_REQUEST,
				owner.active_ops, &token) != CB_SUCCESS ||
			    smm_invocation_evidence_publish_and_request_close(
				runtime->evidence, &token,
				STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_CLOSING,
				owner.active_ops) != CB_SUCCESS)
				fail_stop();
			__atomic_store_n(&owner.state, DISPATCH_DEPARTING,
				__ATOMIC_RELEASE);
		} else {
			s3_wait_for_state(DISPATCH_DEPARTING, policy->max_polls);
		}
		if (smm_invocation_entry_depart(runtime->evidence, &ticket) !=
			CB_SUCCESS)
			fail_stop();
		if (bsp) {
			s3_wait_for_eos(runtime->evidence, &ticket, policy->max_polls);
			if (starbook_mtl_dma_smm_epoch_activate(runtime->instance,
				runtime->evidence, runtime->topology, &ticket,
				owner.rearm_ranges) != CB_SUCCESS ||
			    starbook_mtl_authvar_presence_s3_rearm_borrow(
				runtime->instance, &owner.rearm_facts) != CB_SUCCESS ||
			    starbook_mtl_authvar_presence_s3_rearm_complete(
				runtime->composition, runtime->instance,
				runtime->evidence, runtime->topology,
				owner.active_ops, &owner.rearm_facts) != CB_SUCCESS ||
			    owner.active_ops->write_value(owner.active_ops->context,
				cpu, STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_SUCCESS) !=
				CB_SUCCESS)
				fail_stop();
		}
	} else {
		fail_stop();
	}
	if (!bsp)
		return SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED;
	if (intel_smm_invocation_adapter_provider_retire(
		owner.provider_generation) != SMM_INVOCATION_TRY_SUCCESS)
		fail_stop();
	owner.provider_generation = 0;
	owner.active_ops = NULL;
	owner.mode = MODE_NONE;
	__atomic_store_n(&owner.state, DISPATCH_IDLE, __ATOMIC_RELEASE);
	return SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED;
}
#undef fail_stop
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
bool platform_payload_mm_authvar_service_bootstrap_admitted(void)
{
	struct smm_invocation_runtime_binding runtime, rechecked;
	const struct smm_invocation_runtime_view *view;
	const struct smm_invocation_save_state_ops *retained_ops;
	struct smm_invocation_loader_instance instance, instance_check;
	struct smm_invocation_topology topology, topology_check;
	struct smm_invocation_token token, token_check;

	if (__atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) != DISPATCH_SERVICE_INSTALLING ||
	    owner.provider_generation || !owner.active_ops ||
	    intel_smm_invocation_adapter_provider_provision(&retained_ops) !=
		SMM_INVOCATION_TRY_SUCCESS || retained_ops != owner.active_ops ||
	    smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    smm_invocation_runtime_view_get(&view) != CB_SUCCESS ||
	    smm_invocation_runtime_range_is_protected(view, &owner,
		sizeof(owner)) != CB_SUCCESS ||
	    smm_invocation_loader_instance_read(runtime.instance, &instance) != CB_SUCCESS ||
	    instance.lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD ||
	    smm_invocation_topology_read(runtime.topology, &topology) != CB_SUCCESS ||
	    smm_invocation_evidence_claimed_snapshot(runtime.evidence,
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST, &token) != CB_SUCCESS ||
	    token.lifecycle != instance.lifecycle || !token.bsp ||
	    token.initiator_cpu != topology.bsp_cpu || token.active_cpus != topology.active_cpus ||
	    !smm_invocation_loader_instance_nonce_equal(token.loader_instance_nonce,
		instance.loader_instance_nonce) ||
	    runtime.evidence->bsp_cpu != topology.bsp_cpu ||
	    memcmp(runtime.evidence->participant_apic_ids, topology.initial_apic_ids,
		sizeof(topology.initial_apic_ids)) ||
	    smm_invocation_loader_instance_read(runtime.instance, &instance_check) != CB_SUCCESS ||
	    smm_invocation_topology_read(runtime.topology, &topology_check) != CB_SUCCESS ||
	    smm_invocation_evidence_claimed_snapshot(runtime.evidence,
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST, &token_check) != CB_SUCCESS ||
	    memcmp(&instance, &instance_check, sizeof(instance)) ||
	    memcmp(&topology, &topology_check, sizeof(topology)) ||
	    memcmp(&token, &token_check, sizeof(token)) ||
	    smm_invocation_runtime_binding_get(&rechecked) != CB_SUCCESS ||
	    memcmp(&runtime, &rechecked, sizeof(runtime)) ||
	    owner.provider_generation || owner.active_ops != retained_ops ||
	    __atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) != DISPATCH_SERVICE_INSTALLING)
		return false;
	return true;
}

static enum smm_pre_lock_dispatch_result service_bootstrap_dispatch(
	const struct smm_invocation_runtime_binding *runtime,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	uint32_t cpu, uint32_t initial_apic_id)
{
	struct smm_invocation_entry_ticket ticket;
	struct smm_invocation_token token;
	const bool bsp = cpu == topology->bsp_cpu;

	if (smm_invocation_entry_arrive(runtime->evidence, cause, policy,
		cause->loader_instance_nonce, SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		cpu, initial_apic_id, &ticket) != CB_SUCCESS)
		fail_stop();
	if (bsp) {
		if (smm_invocation_evidence_claim(runtime->evidence,
			SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
			STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST, owner.active_ops,
			&token) != CB_SUCCESS)
			fail_stop();
		__atomic_store_n(&owner.state, DISPATCH_SERVICE_INSTALLING, __ATOMIC_RELEASE);
		if (starbook_mtl_presence_bootstrap_receive(owner.active_ops) !=
			STARBOOK_MTL_PRESENCE_BOOTSTRAP_IMPORTED ||
		    intel_smm_invocation_adapter_provider_retire(owner.provider_generation) !=
			SMM_INVOCATION_TRY_SUCCESS)
			fail_stop();
		owner.provider_generation = 0;
		if (!platform_payload_mm_authvar_service_bootstrap_admitted() ||
		    starbook_mtl_presence_bootstrap_route_install() != CB_SUCCESS ||
		    intel_smm_invocation_adapter_provider_arm(&owner.provider_generation) !=
			SMM_INVOCATION_TRY_SUCCESS ||
		    starbook_mtl_presence_bootstrap_response_stage(owner.active_ops) != CB_SUCCESS ||
		    smm_invocation_evidence_publish_and_request_close(runtime->evidence,
			&token, STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS,
			owner.active_ops) != CB_SUCCESS ||
		    intel_smm_invocation_adapter_provider_retire(owner.provider_generation) !=
			SMM_INVOCATION_TRY_SUCCESS ||
		    starbook_mtl_presence_bootstrap_response_publish() != CB_SUCCESS)
			fail_stop();
		owner.provider_generation = 0;
		__atomic_store_n(&owner.state, DISPATCH_DEPARTING, __ATOMIC_RELEASE);
	} else {
		wait_for_state(DISPATCH_DEPARTING, policy->max_polls);
	}
	if (smm_invocation_entry_depart(runtime->evidence, &ticket) != CB_SUCCESS)
		fail_stop();
	if (!bsp)
		return SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED;
	for (uint32_t poll = 0; ; poll++) {
		if (smm_invocation_entry_eos_ready(runtime->evidence, &ticket))
			break;
		if (poll + 1U == policy->max_polls)
			fail_stop();
		__asm__ __volatile__("pause");
	}
	owner.active_ops = NULL;
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
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM)
	struct smm_invocation_loader_instance instance;
#endif
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
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM)
	if (smm_invocation_loader_instance_read(runtime.instance, &instance) !=
		CB_SUCCESS || instance.lifecycle != cause.lifecycle ||
	    !smm_invocation_loader_instance_nonce_equal(
		instance.loader_instance_nonce, cause.loader_instance_nonce))
		fail_stop();
	if (instance.lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD)
		return s3_dispatch(&runtime, &topology, &cause, &policy, cpu,
			initial_apic_id);
	if (instance.lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD)
		fail_stop();
#endif
	if (bsp) {
		expected = DISPATCH_IDLE;
		if (!__atomic_compare_exchange_n(&owner.state, &expected,
			DISPATCH_ARMING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
		    intel_smm_invocation_adapter_provider_provision(&active_ops) !=
			SMM_INVOCATION_TRY_SUCCESS ||
		    intel_smm_invocation_adapter_provider_arm(
			&owner.provider_generation) != SMM_INVOCATION_TRY_SUCCESS)
			fail_stop();

#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
		{
			uint64_t wire;

			if (active_ops->read_value(active_ops->context, cpu, &wire) != CB_SUCCESS)
				fail_stop();
			if (wire == STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST) {
				owner.active_ops = active_ops;
				__atomic_store_n(&owner.state, DISPATCH_BOOTSTRAP_COLLECTING,
					__ATOMIC_RELEASE);
				return service_bootstrap_dispatch(&runtime, &topology, &cause,
					&policy, cpu, initial_apic_id);
			}
		}
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER) && \
	CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY) && \
	CONFIG(STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)
		{
			const enum starbook_mtl_presence_bootstrap_result bootstrap =
				starbook_mtl_presence_bootstrap_receive(active_ops);

			if (bootstrap == STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR)
				fail_stop();
			if (bootstrap == STARBOOK_MTL_PRESENCE_BOOTSTRAP_IMPORTED) {
				if (intel_smm_invocation_adapter_provider_retire(
					owner.provider_generation) != SMM_INVOCATION_TRY_SUCCESS ||
				    starbook_mtl_presence_bootstrap_route_install() != CB_SUCCESS ||
				    intel_smm_invocation_adapter_provider_arm(
					&owner.provider_generation) != SMM_INVOCATION_TRY_SUCCESS ||
				    starbook_mtl_presence_bootstrap_response_stage(active_ops) != CB_SUCCESS ||
				    intel_smm_invocation_adapter_provider_retire(
					owner.provider_generation) != SMM_INVOCATION_TRY_SUCCESS ||
				    starbook_mtl_presence_bootstrap_response_publish() != CB_SUCCESS)
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
		}
#endif
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
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_COLD_S3_RECORD)
			if (starbook_mtl_authvar_presence_s3_cold_install(runtime.instance) !=
			    CB_SUCCESS)
				fail_stop();
#endif
			__atomic_store_n(&owner.install_departures, 0U, __ATOMIC_RELAXED);
			__atomic_store_n(&owner.state, DISPATCH_IDLE, __ATOMIC_RELEASE);
			return SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED;
		}
	} else {
		mode = wait_for_active_mode(policy.max_polls);
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
		if (mode == DISPATCH_BOOTSTRAP_COLLECTING || mode == DISPATCH_SERVICE_INSTALLING)
			return service_bootstrap_dispatch(&runtime, &topology, &cause,
				&policy, cpu, initial_apic_id);
#endif
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
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_COLD_S3_RECORD)
	if (starbook_mtl_authvar_presence_s3_cold_route_complete(installed.route) !=
	    CB_SUCCESS)
		fail_stop();
#endif
	owner.provider_generation = 0;
	__atomic_store_n(&owner.state, DISPATCH_IDLE, __ATOMIC_RELEASE);
	return SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED;
}
