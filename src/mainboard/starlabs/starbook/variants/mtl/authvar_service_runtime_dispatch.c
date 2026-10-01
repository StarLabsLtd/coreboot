/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_service_receiver.h>
#include <cpu/intel/smm_invocation_adapter_provider.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/tsc.h>
#include <delay.h>
#include <intelblocks/smm_invocation_cause.h>
#include <string.h>

#include "authvar_protected_region.h"
#include "authvar_service_runtime_dispatch.h"

#if !ENV_SMM && !ENV_TEST
#error "StarBook MTL authenticated-variable runtime dispatcher is SMM-only"
#endif

enum runtime_phase {
	RUNTIME_IDLE, RUNTIME_PREPARING, RUNTIME_COLLECTING,
	RUNTIME_RECHECKING, RUNTIME_EXECUTING, RUNTIME_DEPARTING, RUNTIME_FAILED,
};

/*
 * Hard service latency policy, not a sum of the flash controller's per-transfer
 * timeouts. An exceptionally slow otherwise legal transaction fails stopped.
 * Keep short rendezvous limits separate from the BSP's crypto and flash work.
 */
#define RUNTIME_EXECUTION_BUDGET_US (60U * 1000U * 1000U)
#define RUNTIME_EXECUTION_POLL_US 100U

struct runtime_cpu_proof {
	struct region region;
	struct smm_invocation_runtime_binding runtime;
	const struct smm_invocation_runtime_view *view;
	struct smm_invocation_entry_cause cause;
	struct smm_invocation_entry_ticket ticket;
};

static struct {
	uint32_t phase;
	uint32_t initiator;
	uint64_t classified_cpus;
	uint64_t protected_cpus;
	uint64_t rechecked_cpus;
	uint64_t provider_generation;
	uint64_t sealed_provider_generation;
	const struct smm_invocation_save_state_ops *active_ops;
	struct smm_invocation_save_state_ops sealed_ops;
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_topology topology;
	struct smm_invocation_token token;
	struct smm_apmc_selection_receipt selection;
	struct smm_apmc_selection_receipt consumed_selection;
	struct runtime_cpu_proof proofs[SMM_INVOCATION_EVIDENCE_MAX_CPUS];
} owner;

#if ENV_TEST
void starbook_mtl_authvar_service_runtime_generation_drift_test(void)
{
	owner.provider_generation++;
}

void starbook_mtl_authvar_service_runtime_claim_drift_test(uint32_t field)
{
	if (field == 10)
		owner.token.smi_generation++;
	else if (field == 11)
		owner.consumed_selection.generation = 0;
	else if (field == 12)
		owner.proofs[owner.topology.bsp_cpu].view = NULL;
	else if (field == 15)
		owner.proofs[owner.topology.bsp_cpu].ticket.max_polls++;
	else if (field == 16)
		owner.proofs[owner.topology.bsp_cpu].ticket.reserved[2] = 1;
}
#endif

static void __noreturn fail_stop(void)
{
	__atomic_store_n(&owner.phase, RUNTIME_FAILED, __ATOMIC_RELEASE);
	smm_invocation_platform_fail_stop();
}

static void wait_bitmap(const uint64_t *bitmap, uint64_t expected)
{
	for (uint32_t poll = 0; poll < SMM_INVOCATION_ENTRY_MAX_POLLS; poll++) {
		if (__atomic_load_n(bitmap, __ATOMIC_ACQUIRE) == expected)
			return;
		__asm__ __volatile__("pause");
	}
	fail_stop();
}

static void wait_phase(uint32_t expected)
{
	const bool executing = expected == RUNTIME_DEPARTING;
	const uint32_t limit = executing ?
		RUNTIME_EXECUTION_BUDGET_US / RUNTIME_EXECUTION_POLL_US :
		SMM_INVOCATION_ENTRY_MAX_POLLS;

	for (uint32_t poll = 0; poll < limit; poll++) {
		const uint32_t phase = __atomic_load_n(&owner.phase, __ATOMIC_ACQUIRE);

		if (phase == expected)
			return;
		if (phase == RUNTIME_FAILED)
			fail_stop();
		if (executing)
			udelay(RUNTIME_EXECUTION_POLL_US);
		else
			__asm__ __volatile__("pause");
	}
	fail_stop();
}

static bool proof_current(uint32_t cpu)
{
	struct region region;
	struct smm_invocation_runtime_binding runtime;
	const struct smm_invocation_runtime_view *view;

	return cpu < owner.topology.active_cpus &&
		starbook_mtl_authvar_protected_region_read(&region) == CB_SUCCESS &&
		!memcmp(&region, &owner.proofs[cpu].region, sizeof(region)) &&
		smm_invocation_runtime_binding_get(&runtime) == CB_SUCCESS &&
		!memcmp(&runtime, &owner.proofs[cpu].runtime, sizeof(runtime)) &&
		smm_invocation_runtime_view_get(&view) == CB_SUCCESS &&
		view == owner.proofs[cpu].view &&
		smm_invocation_runtime_geometry_is_contained(view, region.offset,
			region.size) == CB_SUCCESS &&
		smm_invocation_runtime_range_is_protected(view, &owner, sizeof(owner)) == CB_SUCCESS;
}

static bool claim_current(void)
{
	struct smm_invocation_runtime_binding runtime, rechecked_runtime;
	struct smm_invocation_topology topology;
	struct smm_invocation_token token, rechecked_token;
	struct smm_invocation_loader_instance instance;
	struct smm_apmc_selection_receipt selection;
	const struct smm_invocation_save_state_ops *const active_ops = owner.active_ops;
	const struct smm_invocation_save_state_ops sealed_ops = owner.sealed_ops;
	const uint64_t provider_generation = owner.provider_generation;
	const uint32_t initiator = owner.initiator;
	uint64_t wire;

	selection = owner.consumed_selection;
	if (__atomic_load_n(&owner.phase, __ATOMIC_ACQUIRE) != RUNTIME_EXECUTING ||
	    !owner.provider_generation ||
	    owner.provider_generation != owner.sealed_provider_generation || !active_ops ||
	    smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    memcmp(&runtime, &owner.runtime, sizeof(runtime)) ||
	    smm_invocation_topology_read(runtime.topology, &topology) != CB_SUCCESS ||
	    memcmp(&topology, &owner.topology, sizeof(topology)) ||
	    !topology.active_cpus || topology.active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS ||
	    topology.bsp_cpu >= topology.active_cpus || owner.initiator >= topology.active_cpus ||
	    smm_invocation_loader_instance_read(runtime.instance, &instance) != CB_SUCCESS ||
	    instance.lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD ||
	    !smm_invocation_loader_instance_nonce_equal(instance.loader_instance_nonce,
		owner.proofs[topology.bsp_cpu].cause.loader_instance_nonce) ||
	    smm_invocation_evidence_claimed_snapshot(runtime.evidence, SMM_APMC_AUTHVAR_SERVICE,
		SMM_APMC_AUTHVAR_SERVICE, &token) != CB_SUCCESS ||
	    memcmp(&token, &owner.token, sizeof(token)) ||
	    token.initiator_cpu != owner.initiator || token.active_cpus != topology.active_cpus ||
	    token.bsp != (owner.initiator == topology.bsp_cpu) ||
	    memcmp(active_ops, &sealed_ops, sizeof(sealed_ops)))
		return false;
	const uint64_t expected = topology.active_cpus == 64U ? UINT64_MAX :
		(1ULL << topology.active_cpus) - 1ULL;
	if (__atomic_load_n(&owner.protected_cpus, __ATOMIC_ACQUIRE) != expected ||
	    __atomic_load_n(&owner.rechecked_cpus, __ATOMIC_ACQUIRE) != expected ||
	    owner.consumed_selection.revision != SMM_APMC_SELECTION_RECEIPT_REVISION ||
	    owner.consumed_selection.size != sizeof(owner.consumed_selection) ||
	    owner.consumed_selection.identity != (uintptr_t)&owner.selection ||
	    !owner.consumed_selection.generation || owner.consumed_selection.reserved ||
	    owner.consumed_selection.descriptor.command != SMM_APMC_AUTHVAR_SERVICE ||
	    owner.consumed_selection.descriptor.owner != SMM_APMC_OWNER_AUTHVAR_SERVICE ||
	    owner.consumed_selection.descriptor.role != SMM_APMC_EXCLUSIVE ||
	    owner.consumed_selection.descriptor.binding_count != 1 ||
	    owner.consumed_selection.descriptor.observer_count ||
	    !owner.consumed_selection.descriptor.enabled ||
	    !owner.consumed_selection.descriptor.reserved ||
	    !proof_current(topology.bsp_cpu))
		return false;
	for (uint32_t cpu = 0; cpu < topology.active_cpus; cpu++) {
		const struct runtime_cpu_proof *proof = &owner.proofs[cpu];

		if (memcmp(&proof->runtime, &runtime, sizeof(runtime)) ||
		    proof->view != owner.proofs[topology.bsp_cpu].view ||
		    memcmp(&proof->region, &owner.proofs[topology.bsp_cpu].region,
			sizeof(proof->region)) ||
		    memcmp(&proof->cause, &owner.proofs[topology.bsp_cpu].cause,
			sizeof(proof->cause)) ||
		    proof->ticket.cpu != cpu || proof->ticket.generation != token.smi_generation ||
		    proof->ticket.command != SMM_APMC_AUTHVAR_SERVICE ||
		    proof->ticket.lifecycle != instance.lifecycle ||
		    proof->ticket.max_polls != SMM_INVOCATION_ENTRY_MAX_POLLS ||
		    proof->ticket.reserved[0] || proof->ticket.reserved[1] ||
		    proof->ticket.reserved[2] ||
		    proof->cause.revision != SMM_INVOCATION_ENTRY_CAUSE_REVISION ||
		    proof->cause.size != sizeof(proof->cause) ||
		    proof->cause.command != SMM_APMC_AUTHVAR_SERVICE ||
		    proof->cause.recognized != 1 || proof->cause.reserved[0] ||
		    proof->cause.reserved[1] || proof->cause.lifecycle != instance.lifecycle ||
		    !smm_invocation_loader_instance_nonce_equal(proof->cause.loader_instance_nonce,
			instance.loader_instance_nonce) ||
		    !smm_invocation_loader_instance_nonce_equal(proof->ticket.loader_instance_nonce,
			instance.loader_instance_nonce))
			return false;
	}
	return active_ops->match_apmc_write(active_ops->context,
		initiator, SMM_APMC_AUTHVAR_SERVICE) == SMM_INVOCATION_MATCHED &&
		active_ops->read_value(active_ops->context, initiator,
			&wire) == CB_SUCCESS && wire == SMM_APMC_AUTHVAR_SERVICE &&
		smm_invocation_evidence_claimed_snapshot(runtime.evidence, SMM_APMC_AUTHVAR_SERVICE,
			SMM_APMC_AUTHVAR_SERVICE, &rechecked_token) == CB_SUCCESS &&
		!memcmp(&token, &rechecked_token, sizeof(token)) &&
		!memcmp(&owner.token, &rechecked_token, sizeof(rechecked_token)) &&
		smm_invocation_runtime_binding_get(&rechecked_runtime) == CB_SUCCESS &&
		!memcmp(&runtime, &rechecked_runtime, sizeof(runtime)) &&
		!memcmp(&owner.runtime, &rechecked_runtime, sizeof(rechecked_runtime)) &&
		!memcmp(&owner.topology, &topology, sizeof(topology)) &&
		!memcmp(&owner.consumed_selection, &selection, sizeof(selection)) &&
		owner.provider_generation == provider_generation && owner.initiator == initiator &&
		owner.sealed_provider_generation == provider_generation &&
		owner.active_ops == active_ops &&
		!memcmp(&owner.sealed_ops, &sealed_ops, sizeof(sealed_ops)) &&
		!memcmp(active_ops, &sealed_ops, sizeof(sealed_ops)) &&
		__atomic_load_n(&owner.phase, __ATOMIC_ACQUIRE) == RUNTIME_EXECUTING;
}

bool platform_payload_mm_authvar_service_runtime_admitted(void)
{
	/* Repeated provider checks observe the same held wave; never restart its barrier. */
	return claim_current();
}

enum smm_pre_lock_dispatch_result starbook_mtl_authvar_service_runtime_dispatch(
	uint32_t cpu, uint32_t initial_apic_id)
{
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_topology topology;
	struct smm_invocation_entry_cause cause;
	struct smm_invocation_entry_ticket ticket;
	const struct smm_invocation_entry_policy policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION, .size = sizeof(policy),
		.max_polls = SMM_INVOCATION_ENTRY_MAX_POLLS,
	};
	const struct smm_invocation_runtime_view *view;
	struct region region;
	enum intel_smm_invocation_cause_result classified;
	uint32_t runtime_cpus;

	if (smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    smm_invocation_topology_read(runtime.topology, &topology) != CB_SUCCESS ||
	    !topology.active_cpus || topology.active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS ||
	    topology.bsp_cpu >= topology.active_cpus || cpu >= topology.active_cpus ||
	    initial_apic_id != topology.initial_apic_ids[cpu])
		fail_stop();
	runtime_cpus = topology.active_cpus;
	classified = intel_smm_invocation_private_apmc_cause(runtime.composition,
		runtime.topology, runtime.instance, runtime.evidence, &runtime_cpus,
		SMM_APMC_AUTHVAR_SERVICE, &cause);
	if (classified == INTEL_SMM_INVOCATION_CAUSE_NOT_PRIVATE)
		return SMM_PRE_LOCK_DISPATCH_NOT_HANDLED;
	if (classified != INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID ||
	    cause.lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD)
		fail_stop();
	/* Establish current hardware placement before modifying any wave authority. */
	if (smm_invocation_runtime_view_get(&view) != CB_SUCCESS ||
	    !tsc_freq_mhz() ||
	    starbook_mtl_authvar_protected_region_read(&region) != CB_SUCCESS ||
	    smm_invocation_runtime_geometry_is_contained(view, region.offset,
		region.size) != CB_SUCCESS ||
	    smm_invocation_runtime_range_is_protected(view, &owner, sizeof(owner)) != CB_SUCCESS)
		fail_stop();
	const bool bsp = cpu == topology.bsp_cpu;
	const uint64_t expected_cpus = topology.active_cpus == 64U ? UINT64_MAX :
		(1ULL << topology.active_cpus) - 1ULL;
	const uint64_t bit = 1ULL << cpu;
	if (bsp) {
		uint32_t idle = RUNTIME_IDLE;

		if (!__atomic_compare_exchange_n(&owner.phase, &idle, RUNTIME_PREPARING,
			false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
		    owner.classified_cpus || owner.protected_cpus || owner.rechecked_cpus ||
		    owner.provider_generation || owner.sealed_provider_generation || owner.active_ops)
			fail_stop();
		owner.runtime = runtime;
		owner.topology = topology;
		owner.initiator = UINT32_MAX;
		if (intel_smm_invocation_adapter_provider_provision(&owner.active_ops) !=
			SMM_INVOCATION_TRY_SUCCESS ||
		    intel_smm_invocation_adapter_provider_arm(&owner.provider_generation) !=
			SMM_INVOCATION_TRY_SUCCESS)
			fail_stop();
		owner.sealed_provider_generation = owner.provider_generation;
		owner.sealed_ops = *owner.active_ops;
		for (uint32_t participant = 0; participant < topology.active_cpus; participant++) {
			const enum smm_invocation_match matched = owner.active_ops->match_apmc_write(
				owner.active_ops->context, participant, SMM_APMC_AUTHVAR_SERVICE);

			if (matched == SMM_INVOCATION_MATCH_ERROR ||
			    (matched == SMM_INVOCATION_MATCHED && owner.initiator != UINT32_MAX))
				fail_stop();
			if (matched == SMM_INVOCATION_MATCHED)
				owner.initiator = participant;
		}
		uint64_t wire;
		if (owner.initiator == UINT32_MAX ||
		    owner.active_ops->read_value(owner.active_ops->context, owner.initiator,
			&wire) != CB_SUCCESS || wire != SMM_APMC_AUTHVAR_SERVICE)
			fail_stop();
		__atomic_store_n(&owner.phase, RUNTIME_COLLECTING, __ATOMIC_RELEASE);
	} else {
		/* BSP initializes before AP publication: AP-first bits cannot be erased. */
		wait_phase(RUNTIME_COLLECTING);
	}
	if (memcmp(&runtime, &owner.runtime, sizeof(runtime)) ||
	    memcmp(&topology, &owner.topology, sizeof(topology)) ||
	    (__atomic_fetch_or(&owner.classified_cpus, bit, __ATOMIC_ACQ_REL) & bit))
		fail_stop();
	wait_bitmap(&owner.classified_cpus, expected_cpus);
	if (smm_invocation_entry_arrive(runtime.evidence, &cause, &policy,
		cause.loader_instance_nonce, SMM_APMC_AUTHVAR_SERVICE, cpu,
		initial_apic_id, &ticket) != CB_SUCCESS)
		fail_stop();
	owner.proofs[cpu] = (struct runtime_cpu_proof) {
		.region = region, .runtime = runtime, .view = view, .cause = cause, .ticket = ticket,
	};
	if (__atomic_fetch_or(&owner.protected_cpus, bit, __ATOMIC_ACQ_REL) & bit)
		fail_stop();
	if (bsp) {
		wait_bitmap(&owner.protected_cpus, expected_cpus);
		if (smm_invocation_evidence_claim(runtime.evidence, SMM_APMC_AUTHVAR_SERVICE,
			SMM_APMC_AUTHVAR_SERVICE, owner.active_ops, &owner.token) != CB_SUCCESS ||
		    smm_apmc_command_select(SMM_APMC_AUTHVAR_SERVICE, &owner.selection) !=
			SMM_APMC_SELECT_ENABLED)
			fail_stop();
		owner.consumed_selection = owner.selection;
		if (smm_apmc_command_consume(SMM_APMC_AUTHVAR_SERVICE,
			SMM_APMC_OWNER_AUTHVAR_SERVICE, &owner.selection) != SMM_APMC_CONSUMED_SUCCESS)
			fail_stop();
		__atomic_store_n(&owner.phase, RUNTIME_RECHECKING, __ATOMIC_RELEASE);
	} else {
		wait_phase(RUNTIME_RECHECKING);
	}
	if (!proof_current(cpu) ||
	    (__atomic_fetch_or(&owner.rechecked_cpus, bit, __ATOMIC_ACQ_REL) & bit))
		fail_stop();
	if (bsp) {
		wait_bitmap(&owner.rechecked_cpus, expected_cpus);
		__atomic_store_n(&owner.phase, RUNTIME_EXECUTING, __ATOMIC_RELEASE);
		if (!claim_current() || payload_mm_authvar_service_execute() != CB_SUCCESS ||
		    !claim_current() ||
		    smm_invocation_evidence_publish_and_request_close(runtime.evidence, &owner.token,
			0, owner.active_ops) != CB_SUCCESS ||
		    intel_smm_invocation_adapter_provider_retire(owner.sealed_provider_generation) !=
			SMM_INVOCATION_TRY_SUCCESS)
			fail_stop();
		owner.provider_generation = 0;
		__atomic_store_n(&owner.phase, RUNTIME_DEPARTING, __ATOMIC_RELEASE);
	} else {
		wait_phase(RUNTIME_DEPARTING);
	}
	if (!proof_current(cpu) || smm_invocation_entry_depart(runtime.evidence, &ticket) != CB_SUCCESS)
		fail_stop();
	if (!bsp)
		return SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED;
	for (uint32_t poll = 0; ; poll++) {
		if (smm_invocation_entry_eos_ready(runtime.evidence, &ticket))
			break;
		if (poll + 1U == policy.max_polls)
			fail_stop();
		__asm__ __volatile__("pause");
	}
	/* No participant can still read the held-wave proofs after all departures. */
	memset((uint8_t *)&owner + sizeof(owner.phase), 0, sizeof(owner) - sizeof(owner.phase));
	__atomic_store_n(&owner.phase, RUNTIME_IDLE, __ATOMIC_RELEASE);
	return SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED;
}
