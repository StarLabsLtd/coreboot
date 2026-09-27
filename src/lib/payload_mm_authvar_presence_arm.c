/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_arm.h>
#include "bootmem_reservation_receipt_internal.h"
#include <cpu/x86/apm.h>
#include <cpu/x86/smm_command.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Authenticated-variable presence arm is SMM-only"
#endif

#if ENV_TEST
void payload_mm_authvar_presence_arm_test_before_verifier_claim(
	struct bootmem_reservation_receipt_authority *verifier);
#endif

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool overlaps(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (!object_valid(first, first_size, 1U) ||
	    !object_valid(second, second_size, 1U))
		return true;
	return first_base <= second_base ?
		second_base - first_base < first_size :
		first_base - second_base < second_size;
}

static bool nonzero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value != 0;
}

static bool binding_valid(
	const struct payload_mm_authvar_presence_transaction_binding *binding)
{
	return binding &&
		binding->revision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION &&
		binding->size == sizeof(*binding) && binding->generation &&
		binding->transaction_id && binding->nonce && binding->maximum_cpus &&
		binding->initiator_cpu < binding->maximum_cpus &&
		nonzero(binding->capability, sizeof(binding->capability)) &&
		!binding->reserved[0] && !binding->reserved[1];
}

static bool policy_valid(
	const struct payload_mm_authvar_presence_transaction_policy *policy)
{
	return policy &&
		policy->revision ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_POLICY_REVISION &&
		policy->size == sizeof(*policy) && policy->prepare && policy->commit &&
		policy->abort && policy->dma_protected && policy->claim_invocation &&
		policy->complete_invocation && policy->fail_stop &&
		policy->context_size <=
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX &&
		!!policy->context == !!policy->context_size;
}

static bool protected_range_exact(struct payload_mm_authvar_presence_arm *arm,
	payload_mm_authvar_protected_storage proof, void *context,
	const void *object, size_t size);

static bool loader_inputs_unchanged(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_loader_composition *composition_snapshot,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_loader_instance *instance_snapshot,
	const struct smm_invocation_evidence *evidence,
	const struct smm_invocation_evidence *evidence_snapshot)
{
	return !memcmp(composition, composition_snapshot, sizeof(*composition)) &&
		!memcmp(instance, instance_snapshot, sizeof(*instance)) &&
		!memcmp(evidence, evidence_snapshot, sizeof(*evidence));
}

static bool proof_closure_unchanged(
	const struct payload_mm_authvar_presence_arm *arm,
	payload_mm_authvar_protected_storage proof, void *context)
{
	return arm->protected_storage == proof &&
		arm->sealed_protected_storage == proof &&
		arm->protected_storage_context == context &&
		arm->sealed_protected_storage_context == context;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
static bool delegated_proof_closure_valid(
	const struct payload_mm_authvar_presence_arm *arm)
{
	return __atomic_load_n(&arm->protection_delegation, __ATOMIC_ACQUIRE) ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROTECTION_DELEGATION_BOUND &&
		arm->delegated_protected_storage &&
		arm->delegated_protected_storage ==
			arm->sealed_delegated_protected_storage &&
		arm->delegated_protected_storage_context ==
			arm->sealed_delegated_protected_storage_context &&
		arm->delegated_protected_storage_context_size ==
			arm->sealed_delegated_protected_storage_context_size &&
		arm->delegated_protected_storage_context_size &&
		arm->delegated_protected_storage_context_size <=
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX;
}

static bool delegation_snapshot_valid(
	const struct payload_mm_authvar_presence_arm *arm)
{
	const uint32_t state = __atomic_load_n(&arm->protection_delegation,
		__ATOMIC_ACQUIRE);

	if (state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROTECTION_DELEGATION_EMPTY)
		return !arm->delegated_protected_storage &&
			!arm->delegated_protected_storage_context &&
			!arm->delegated_protected_storage_context_size &&
			!arm->sealed_delegated_protected_storage &&
			!arm->sealed_delegated_protected_storage_context &&
			!arm->sealed_delegated_protected_storage_context_size;
	return delegated_proof_closure_valid(arm);
}

static bool effective_proof_closure(
	struct payload_mm_authvar_presence_arm *arm,
	payload_mm_authvar_protected_storage *proof, void **context)
{
	if (__atomic_load_n(&arm->protection_delegation, __ATOMIC_ACQUIRE) ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROTECTION_DELEGATION_EMPTY) {
		*proof = arm->protected_storage;
		*context = arm->protected_storage_context;
		return proof_closure_unchanged(arm, *proof, *context) &&
			delegation_snapshot_valid(arm);
	}
	if (!delegated_proof_closure_valid(arm))
		return false;
	*proof = arm->delegated_protected_storage;
	*context = arm->delegated_protected_storage_context;
	return proof_closure_unchanged(arm, arm->protected_storage,
		arm->protected_storage_context);
}
#endif

static bool loader_bundle_valid(
	const struct payload_mm_authvar_presence_arm *arm)
{
	struct smm_invocation_loader_instance current = { 0 };
	const struct smm_invocation_evidence *evidence;
	bool valid;

	if (arm->composition != arm->sealed_composition ||
	    arm->instance != arm->sealed_instance ||
	    arm->evidence != arm->sealed_evidence ||
	    memcmp(&arm->instance_snapshot, &arm->sealed_instance_snapshot,
		sizeof(arm->instance_snapshot)))
		return false;
	evidence = smm_invocation_loader_composition_evidence(arm->composition,
		arm->evidence);
	valid = evidence == arm->evidence &&
		smm_invocation_loader_instance_read(arm->instance, &current) ==
			CB_SUCCESS &&
		!memcmp(&current, &arm->instance_snapshot, sizeof(current)) &&
		!memcmp(&current, &arm->sealed_instance_snapshot, sizeof(current)) &&
		__atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE) !=
			SMM_INVOCATION_EMPTY &&
		__atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE) !=
			SMM_INVOCATION_PROVISIONING &&
		__atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE) !=
			SMM_INVOCATION_POISONED &&
		evidence->active_cpus == arm->active_cpus &&
		evidence->bsp_cpu == arm->bsp_cpu && arm->active_cpus != 0U &&
		arm->bsp_cpu < arm->active_cpus &&
		smm_invocation_loader_instance_nonce_equal(
			evidence->loader_instance_nonce,
			current.loader_instance_nonce) &&
		evidence->loader_lifecycle == current.lifecycle;
	scrub(&current, sizeof(current));
	return valid;
}

static bool loader_facts_valid(
	const struct payload_mm_authvar_presence_arm *arm)
{
	return loader_bundle_valid(arm) &&
		arm->active_cpus == arm->binding.maximum_cpus &&
		arm->bsp_cpu == arm->binding.initiator_cpu;
}

static __noreturn void arm_fail_stop(
	struct payload_mm_authvar_presence_arm *arm);

static struct payload_mm_authvar_presence_arm *context_arm(void *context)
{
	const struct payload_mm_authvar_presence_arm_context *callback = context;
	struct payload_mm_authvar_presence_arm *arm;
	payload_mm_authvar_protected_storage proof;
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
	payload_mm_authvar_protected_storage current_proof;
#endif
	void *proof_context;
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
	void *current_proof_context;
#endif

	if (!object_valid(callback, sizeof(*callback), _Alignof(*callback)))
		return NULL;
	arm = callback->arm;
	if (!object_valid(arm, sizeof(*arm), _Alignof(*arm)) ||
	    arm != platform_payload_mm_authvar_presence_arm())
		return NULL;
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
	if (!effective_proof_closure(arm, &proof, &proof_context) ||
	    arm->failure_callback != arm->sealed_failure_callback ||
	    arm->failure_context_size != arm->sealed_failure_context_size)
		arm_fail_stop(arm);
#else
	proof = arm->protected_storage;
	proof_context = arm->protected_storage_context;
	if (proof != arm->sealed_protected_storage ||
	    proof_context != arm->sealed_protected_storage_context ||
	    arm->failure_callback != arm->sealed_failure_callback ||
	    arm->failure_context_size != arm->sealed_failure_context_size)
		arm_fail_stop(arm);
#endif
	if (callback->identity != (uintptr_t)arm ||
	    memcmp(callback, &arm->sealed_callback_context, sizeof(*callback)))
		return NULL;
	if (!protected_range_exact(arm, proof, proof_context, arm, sizeof(*arm)) ||
	    !protected_range_exact(arm, proof, proof_context, callback,
		sizeof(*callback)) ||
	    !protected_range_exact(arm, proof, proof_context,
		(const void *)(uintptr_t)platform_payload_mm_authvar_presence_arm,
		1U) ||
	    !protected_range_exact(arm, proof, proof_context,
		(const void *)(uintptr_t)proof, 1U))
		arm_fail_stop(arm);
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
	if (!effective_proof_closure(arm, &current_proof,
		&current_proof_context) || current_proof != proof ||
	    current_proof_context != proof_context)
		arm_fail_stop(arm);
#else
	if (!proof_closure_unchanged(arm, proof, proof_context))
		arm_fail_stop(arm);
#endif
	if (callback->identity != (uintptr_t)arm ||
	    memcmp(callback, &arm->sealed_callback_context, sizeof(*callback)))
		arm_fail_stop(arm);
	return arm;
}

static void *original_context(struct payload_mm_authvar_presence_arm *arm)
{
	return arm->policy.context_size ? arm->policy_context : NULL;
}

static __noreturn void arm_fail_stop(
	struct payload_mm_authvar_presence_arm *arm)
{
	payload_mm_authvar_presence_transaction_fail_stop_fn fail_stop;
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX];
	size_t context_size;
	uint32_t state;

	if (!arm)
		__builtin_trap();
	if (arm->failure_callback != arm->sealed_failure_callback ||
	    arm->failure_context_size != arm->sealed_failure_context_size ||
	    arm->failure_context_size > sizeof(arm->failure_context) ||
	    memcmp(arm->failure_context, arm->sealed_failure_context,
		arm->failure_context_size))
		__builtin_trap();
	fail_stop = arm->failure_callback;
	context_size = arm->failure_context_size;
	if (!fail_stop || context_size > sizeof(context))
		__builtin_trap();
	if (context_size)
		memcpy(context, arm->failure_context, context_size);
	state = __atomic_load_n(&arm->state, __ATOMIC_ACQUIRE);
	while (state != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED &&
	       state != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_ABORTED &&
	       state != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_READY &&
	       !__atomic_compare_exchange_n(&arm->state, &state,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		;
	fail_stop(context_size ? context : NULL);
	__builtin_trap();
}

static __noreturn void arm_fail_stop_snapshot(
	struct payload_mm_authvar_presence_arm *arm,
	const struct payload_mm_authvar_presence_arm *trusted)
{
	payload_mm_authvar_presence_transaction_fail_stop_fn fail_stop;
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX];
	size_t context_size;
	uint32_t state;

	if (!arm || !trusted ||
	    trusted->failure_callback != trusted->sealed_failure_callback ||
	    trusted->failure_context_size != trusted->sealed_failure_context_size ||
	    trusted->failure_context_size > sizeof(context) ||
	    memcmp(trusted->failure_context, trusted->sealed_failure_context,
		trusted->failure_context_size))
		__builtin_trap();
	fail_stop = trusted->failure_callback;
	context_size = trusted->failure_context_size;
	if (!fail_stop)
		__builtin_trap();
	if (context_size)
		memcpy(context, trusted->failure_context, context_size);
	state = __atomic_load_n(&arm->state, __ATOMIC_ACQUIRE);
	while (state != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED &&
	       state != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_ABORTED &&
	       state != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_READY &&
	       !__atomic_compare_exchange_n(&arm->state, &state,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		;
	fail_stop(context_size ? context : NULL);
	__builtin_trap();
}

static bool protected_range_exact(struct payload_mm_authvar_presence_arm *arm,
	payload_mm_authvar_protected_storage proof, void *context,
	const void *object, size_t size)
{
	struct payload_mm_authvar_presence_arm before;
	bool protected;

	if (!proof || !object || !size)
		return false;
	before = *arm;
	protected = proof(context, object, size);
	if (memcmp(&before, arm, sizeof(before)))
		arm_fail_stop_snapshot(arm, &before);
	scrub(&before, sizeof(before));
	return protected;
}

static bool protected_range_bootstrap(
	struct payload_mm_authvar_presence_arm *arm,
	payload_mm_authvar_protected_storage proof, void *context,
	const void *object, size_t size)
{
	struct payload_mm_authvar_presence_arm before;
	bool protected;

	if (!proof || !object || !size)
		return false;
	before = *arm;
	protected = proof(context, object, size);
	if (memcmp(&before, arm, sizeof(before))) {
		__atomic_store_n(&arm->state,
			PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED, __ATOMIC_RELEASE);
		scrub(&before, sizeof(before));
		return false;
	}
	scrub(&before, sizeof(before));
	return protected;
}

static bool callback_snapshot_valid(struct payload_mm_authvar_presence_arm *arm,
	uint32_t state)
{
	return __atomic_load_n(&arm->state, __ATOMIC_ACQUIRE) == state &&
		arm->owner_attempt == 1U && !arm->reserved &&
		!memcmp(&arm->binding, &arm->sealed_binding,
			sizeof(arm->binding)) &&
		!memcmp(&arm->seed, &arm->sealed_seed, sizeof(arm->seed)) &&
		arm->protected_storage == arm->sealed_protected_storage &&
		arm->protected_storage_context ==
			arm->sealed_protected_storage_context &&
		!memcmp(&arm->callback_context, &arm->sealed_callback_context,
			sizeof(arm->callback_context)) &&
		arm->failure_callback == arm->sealed_failure_callback &&
		arm->failure_context_size == arm->sealed_failure_context_size &&
		arm->failure_context_size <= sizeof(arm->failure_context) &&
		!memcmp(arm->failure_context, arm->sealed_failure_context,
			arm->failure_context_size) &&
		arm->policy.context ==
			(arm->policy.context_size ? arm->policy_context : NULL) &&
		arm->sealed_policy.context ==
			(arm->sealed_policy.context_size ?
			 arm->sealed_policy_context : NULL) &&
		!memcmp(&arm->policy, &arm->sealed_policy,
			offsetof(struct payload_mm_authvar_presence_transaction_policy,
				context)) &&
		arm->policy.context_size == arm->sealed_policy.context_size &&
		arm->policy.context_size <= sizeof(arm->policy_context) &&
		!memcmp(arm->policy_context, arm->sealed_policy_context,
			arm->policy.context_size) &&
		loader_facts_valid(arm)
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
		&& delegation_snapshot_valid(arm)
#endif
		;
}

static bool phase_snapshot_valid(struct payload_mm_authvar_presence_arm *arm,
	uint32_t state, uint32_t audit, uint32_t dispatch,
	uint64_t completed_prepare_generation)
{
	return callback_snapshot_valid(arm, state) &&
		__atomic_load_n(&arm->audit, __ATOMIC_ACQUIRE) == audit &&
		__atomic_load_n(&arm->dispatch, __ATOMIC_ACQUIRE) == dispatch &&
		arm->completed_prepare_generation == completed_prepare_generation;
}

static bool invocation_valid(
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	const struct payload_mm_authvar_presence_transaction_binding *binding)
{
	return invocation->revision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_INVOCATION_REVISION &&
		invocation->size == sizeof(*invocation) && invocation->smi_generation &&
		invocation->rendezvous_generation == invocation->smi_generation &&
		nonzero(invocation->rendezvous_proof,
			sizeof(invocation->rendezvous_proof)) &&
		invocation->bsp == 1U && !invocation->reserved &&
		invocation->initiator_cpu == binding->initiator_cpu &&
		invocation->active_cpus == binding->maximum_cpus;
}

static void terminal_scrub(struct payload_mm_authvar_presence_arm *arm)
{
	arm->composition = NULL;
	arm->instance = NULL;
	arm->evidence = NULL;
	arm->sealed_composition = NULL;
	arm->sealed_instance = NULL;
	arm->sealed_evidence = NULL;
	arm->completed_prepare_generation = 0;
	arm->reserved = 0;
	arm->active_cpus = 0;
	arm->bsp_cpu = 0;
	scrub(&arm->instance_snapshot, sizeof(arm->instance_snapshot));
	scrub(&arm->sealed_instance_snapshot,
		sizeof(arm->sealed_instance_snapshot));
	scrub(&arm->binding, sizeof(arm->binding));
	scrub(&arm->sealed_binding, sizeof(arm->sealed_binding));
	scrub(&arm->seed, sizeof(arm->seed));
	scrub(&arm->sealed_seed, sizeof(arm->sealed_seed));
	scrub(&arm->invocation, sizeof(arm->invocation));
	scrub(&arm->policy, sizeof(arm->policy));
	scrub(&arm->sealed_policy, sizeof(arm->sealed_policy));
	scrub(&arm->wrapped_policy, sizeof(arm->wrapped_policy));
	scrub(arm->policy_context, sizeof(arm->policy_context));
	scrub(arm->sealed_policy_context, sizeof(arm->sealed_policy_context));
	__atomic_store_n(&arm->audit,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_ABORTED, __ATOMIC_RELEASE);
	__atomic_store_n(&arm->dispatch,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_ABORTED, __ATOMIC_RELEASE);
}

static bool seed_valid(const struct payload_mm_authvar_presence_seed *seed,
	const struct payload_mm_authvar_presence_transaction_binding *binding)
{
	return seed && seed->revision == PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION &&
		seed->size == sizeof(*seed) &&
		payload_mm_authvar_presence_endpoint_validate(&seed->endpoint) ==
			CB_SUCCESS &&
		seed->endpoint.generation == binding->generation &&
		seed->endpoint.transport == LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8 &&
		seed->endpoint.trigger_width == 1U &&
		seed->endpoint.trigger_address ==
			APM_CNT &&
		seed->endpoint.trigger_value ==
			SMM_APMC_AUTHVAR_PRESENCE &&
		seed->backing.revision == PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION &&
		seed->backing.size == sizeof(seed->backing) &&
		seed->backing.base == seed->endpoint.communication_base &&
		seed->backing.bytes == PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE &&
		seed->backing.generation == binding->generation &&
		seed->backing.tag == BM_MEM_RESERVED &&
		nonzero(seed->capability, sizeof(seed->capability));
}

static enum cb_err wrapped_prepare(void *context,
	const struct payload_mm_authvar_presence_seed *seed, uint64_t generation)
{
	struct payload_mm_authvar_presence_arm *arm = context_arm(context);
	struct payload_mm_authvar_presence_arm before;
	uint32_t expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_EMPTY;
	enum cb_err status;

	if (!arm)
		return CB_ERR;
	if (!callback_snapshot_valid(arm, PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND) ||
	    __atomic_load_n(&arm->dispatch, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_PREPARE_CLAIMED)
		arm_fail_stop(arm);
	if (generation != arm->binding.generation ||
	    !seed_valid(seed, &arm->binding)) {
		if (!__atomic_compare_exchange_n(&arm->audit, &expected,
			PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_PREPARE_REJECTED,
			false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			arm_fail_stop(arm);
		return CB_ERR;
	}
	arm->seed = *seed;
	arm->sealed_seed = *seed;
	if (!__atomic_compare_exchange_n(&arm->audit, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_PREPARE_ENTERED, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	before = *arm;
	status = arm->policy.prepare(original_context(arm), seed, generation);
	if (memcmp(&before, arm, sizeof(before)))
		arm_fail_stop_snapshot(arm, &before);
	if (!phase_snapshot_valid(arm, PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_PREPARE_ENTERED,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_PREPARE_CLAIMED, 0U) ||
	    memcmp(seed, &arm->seed, sizeof(*seed)))
		arm_fail_stop(arm);
	scrub(&before, sizeof(before));
	if (status != CB_SUCCESS)
		return CB_ERR;
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_PREPARE_ENTERED;
	if (!__atomic_compare_exchange_n(&arm->audit, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_PREPARED, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	return CB_SUCCESS;
}

static enum cb_err wrapped_commit(void *context, uint64_t generation)
{
	struct payload_mm_authvar_presence_arm *arm = context_arm(context);
	struct payload_mm_authvar_presence_arm before;
	uint32_t expected;
	uint64_t completed_prepare_generation;
	enum cb_err status;

	if (!arm)
		return CB_ERR;
	if (generation != arm->binding.generation ||
	    !callback_snapshot_valid(arm, PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND) ||
	    __atomic_load_n(&arm->dispatch, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_DECISION_CLAIMED ||
	    __atomic_load_n(&arm->audit, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_PREPARED ||
	    !seed_valid(&arm->seed, &arm->binding))
		arm_fail_stop(arm);
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_PREPARED;
	completed_prepare_generation = arm->completed_prepare_generation;
	if (!__atomic_compare_exchange_n(&arm->audit, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_COMMITTING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	before = *arm;
	status = arm->policy.commit(original_context(arm), generation);
	if (memcmp(&before, arm, sizeof(before)))
		arm_fail_stop_snapshot(arm, &before);
	if (status != CB_SUCCESS ||
	    !phase_snapshot_valid(arm, PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_COMMITTING,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_DECISION_CLAIMED,
		completed_prepare_generation) ||
	    !seed_valid(&arm->seed, &arm->binding))
		arm_fail_stop(arm);
	scrub(&before, sizeof(before));
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_COMMITTING;
	if (!__atomic_compare_exchange_n(&arm->audit, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_COMMITTED, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_DECISION_CLAIMED;
	if (!__atomic_compare_exchange_n(&arm->dispatch, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_COMMIT_PENDING, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	return CB_SUCCESS;
}

static enum cb_err wrapped_abort(void *context, uint64_t generation)
{
	struct payload_mm_authvar_presence_arm *arm = context_arm(context);
	struct payload_mm_authvar_presence_arm before;
	uint32_t audit, claimed_dispatch, expected;
	uint64_t completed_prepare_generation;
	enum cb_err status;

	if (!arm)
		return CB_ERR;
	if (generation != arm->binding.generation ||
	    !callback_snapshot_valid(arm, PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND) ||
	    (__atomic_load_n(&arm->dispatch, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_PREPARE_CLAIMED &&
	     __atomic_load_n(&arm->dispatch, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_DECISION_CLAIMED))
		arm_fail_stop(arm);
	audit = __atomic_load_n(&arm->audit, __ATOMIC_ACQUIRE);
	claimed_dispatch = __atomic_load_n(&arm->dispatch, __ATOMIC_ACQUIRE);
	completed_prepare_generation = arm->completed_prepare_generation;
	if (audit == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_PREPARE_REJECTED) {
		expected = audit;
		if (!__atomic_compare_exchange_n(&arm->audit, &expected,
			PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_ABORTING, false,
			__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			arm_fail_stop(arm);
		goto pending;
	}
	if (audit != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_PREPARE_ENTERED &&
	    audit != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_PREPARED)
		arm_fail_stop(arm);
	expected = audit;
	if (!__atomic_compare_exchange_n(&arm->audit, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_ABORTING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	before = *arm;
	status = arm->policy.abort(original_context(arm), generation);
	if (memcmp(&before, arm, sizeof(before)))
		arm_fail_stop_snapshot(arm, &before);
	if (status != CB_SUCCESS ||
	    !phase_snapshot_valid(arm, PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_ABORTING,
		claimed_dispatch, completed_prepare_generation))
		arm_fail_stop(arm);
	scrub(&before, sizeof(before));
pending:
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_ABORTING;
	if (!__atomic_compare_exchange_n(&arm->audit, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_ABORTED, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	audit = __atomic_load_n(&arm->dispatch, __ATOMIC_ACQUIRE);
	if (audit != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_PREPARE_CLAIMED &&
	    audit != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_DECISION_CLAIMED)
		arm_fail_stop(arm);
	expected = audit;
	if (!__atomic_compare_exchange_n(&arm->dispatch, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_ABORT_PENDING, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	return CB_SUCCESS;
}

static bool wrapped_dma_protected(void *context, uint64_t base, uint64_t size)
{
	struct payload_mm_authvar_presence_arm *arm = context_arm(context);
	struct payload_mm_authvar_presence_arm before;
	uint32_t state, audit, dispatch;
	uint64_t completed_prepare_generation;
	bool protected;

	if (!arm)
		return false;
	state = __atomic_load_n(&arm->state, __ATOMIC_ACQUIRE);
	if (state != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND ||
	    !callback_snapshot_valid(arm, state))
		arm_fail_stop(arm);
	audit = __atomic_load_n(&arm->audit, __ATOMIC_ACQUIRE);
	dispatch = __atomic_load_n(&arm->dispatch, __ATOMIC_ACQUIRE);
	completed_prepare_generation = arm->completed_prepare_generation;
	before = *arm;
	protected = arm->policy.dma_protected(original_context(arm), base, size);
	if (memcmp(&before, arm, sizeof(before)))
		arm_fail_stop_snapshot(arm, &before);
	if (!phase_snapshot_valid(arm, state, audit, dispatch,
		completed_prepare_generation))
		arm_fail_stop(arm);
	scrub(&before, sizeof(before));
	return protected;
}

static enum cb_err wrapped_claim(void *context, uint64_t sentinel,
	struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	struct payload_mm_authvar_presence_arm *arm = context_arm(context);
	struct payload_mm_authvar_presence_arm before;
	uint32_t audit, dispatch, expected;
	uint64_t completed_prepare_generation;
	enum cb_err status;

	if (!arm)
		return CB_ERR;
	if (__atomic_load_n(&arm->state, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND ||
	    sentinel != PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_RAX_SENTINEL ||
	    !callback_snapshot_valid(arm, PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND))
		return CB_ERR;
	dispatch = __atomic_load_n(&arm->dispatch, __ATOMIC_ACQUIRE);
	if (dispatch == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_IDLE)
		expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_PREPARE_CLAIMING;
	else if (dispatch == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_PREPARED)
		expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_DECISION_CLAIMING;
	else
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&arm->dispatch, &dispatch, expected,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	audit = __atomic_load_n(&arm->audit, __ATOMIC_ACQUIRE);
	completed_prepare_generation = arm->completed_prepare_generation;
	memset(invocation, 0, sizeof(*invocation));
	before = *arm;
	status = arm->policy.claim_invocation(original_context(arm), sentinel,
		invocation);
	if (memcmp(&before, arm, sizeof(before)))
		arm_fail_stop_snapshot(arm, &before);
	if (status != CB_SUCCESS || !invocation_valid(invocation, &arm->binding) ||
	    !phase_snapshot_valid(arm, PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND,
		audit, expected, completed_prepare_generation))
		arm_fail_stop(arm);
	scrub(&before, sizeof(before));
	if (expected == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_DECISION_CLAIMING &&
	    (!arm->completed_prepare_generation ||
	     arm->completed_prepare_generation == UINT64_MAX ||
	     invocation->smi_generation <= arm->completed_prepare_generation))
		arm_fail_stop(arm);
	arm->invocation = *invocation;
	dispatch = expected;
	expected = dispatch ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_PREPARE_CLAIMING ?
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_PREPARE_CLAIMED :
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_DECISION_CLAIMED;
	if (!__atomic_compare_exchange_n(&arm->dispatch, &dispatch, expected,
		false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	return status;
}

static enum cb_err wrapped_complete(void *context,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	uint64_t value)
{
	struct payload_mm_authvar_presence_arm *arm = context_arm(context);
	struct payload_mm_authvar_presence_arm before;
	uint32_t audit, state, dispatch, expected;
	uint64_t completed_prepare_generation;
	uint64_t exact_value;
	enum cb_err status;

	if (!arm)
		return CB_ERR;
	state = __atomic_load_n(&arm->state, __ATOMIC_ACQUIRE);
	if ((state != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND &&
	     state != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_READY) ||
	    !callback_snapshot_valid(arm, state) ||
	    memcmp(invocation, &arm->invocation, sizeof(*invocation)))
		arm_fail_stop(arm);
	dispatch = __atomic_load_n(&arm->dispatch, __ATOMIC_ACQUIRE);
	audit = __atomic_load_n(&arm->audit, __ATOMIC_ACQUIRE);
	if (dispatch == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_PREPARE_CLAIMED &&
	    audit == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_PREPARED) {
		exact_value = payload_mm_authvar_presence_transaction_rax(&arm->binding,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
		expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_PREPARE_COMPLETING;
	} else if (dispatch ==
		   PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_COMMIT_PENDING &&
		   state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND &&
		   audit == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_COMMITTED &&
		   arm->completed_prepare_generation &&
		   arm->completed_prepare_generation != UINT64_MAX) {
		exact_value = payload_mm_authvar_presence_transaction_rax(&arm->binding,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
		expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_COMMIT_COMPLETING;
	} else if ((dispatch ==
		    PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_ABORT_PENDING &&
		    audit == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_ABORTED) ||
		   (dispatch ==
		    PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_PREPARE_CLAIMED &&
		    audit == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_EMPTY)) {
		exact_value = payload_mm_authvar_presence_transaction_rax(&arm->binding,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
		expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_ABORT_COMPLETING;
	} else {
		arm_fail_stop(arm);
	}
	if (!exact_value || value != exact_value ||
	    !__atomic_compare_exchange_n(&arm->dispatch, &dispatch, expected,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	completed_prepare_generation = arm->completed_prepare_generation;
	before = *arm;
	status = arm->policy.complete_invocation(original_context(arm), invocation,
		value);
	if (memcmp(&before, arm, sizeof(before)))
		arm_fail_stop_snapshot(arm, &before);
	if (status != CB_SUCCESS ||
	    !phase_snapshot_valid(arm, state, audit, expected,
		completed_prepare_generation) ||
	    memcmp(invocation, &arm->invocation, sizeof(*invocation)))
		arm_fail_stop(arm);
	scrub(&before, sizeof(before));
	scrub(&arm->invocation, sizeof(arm->invocation));
	if (expected == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_PREPARE_COMPLETING) {
		if (invocation->smi_generation == UINT64_MAX)
			arm_fail_stop(arm);
		arm->completed_prepare_generation = invocation->smi_generation;
		dispatch = expected;
		if (!__atomic_compare_exchange_n(&arm->dispatch, &dispatch,
			PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_PREPARED, false,
			__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
			arm_fail_stop(arm);
		return CB_SUCCESS;
	}
	if (expected == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_COMMIT_COMPLETING) {
		dispatch = expected;
		if (!__atomic_compare_exchange_n(&arm->dispatch, &dispatch,
			PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_COMMITTED, false,
			__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
			arm_fail_stop(arm);
		expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND;
		if (!__atomic_compare_exchange_n(&arm->state, &expected,
			PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_READY, false,
			__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
			arm_fail_stop(arm);
		return CB_SUCCESS;
	}
	dispatch = expected;
	if (!__atomic_compare_exchange_n(&arm->dispatch, &dispatch,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_ABORTED, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	terminal_scrub(arm);
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND;
	if (!__atomic_compare_exchange_n(&arm->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_ABORTED, false, __ATOMIC_RELEASE,
		__ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	return CB_SUCCESS;
}

static void __noreturn wrapped_fail_stop(void *context)
{
	arm_fail_stop(context_arm(context));
}

static bool transaction_provisioning_snapshot_valid(
	struct payload_mm_authvar_presence_arm *arm, uint32_t state)
{
	return callback_snapshot_valid(arm, state) &&
		__atomic_load_n(&arm->audit, __ATOMIC_ACQUIRE) ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_EMPTY &&
		__atomic_load_n(&arm->dispatch, __ATOMIC_ACQUIRE) ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_DISPATCH_IDLE &&
		!arm->completed_prepare_generation &&
		arm->callback_context.arm == arm &&
		arm->callback_context.identity == (uintptr_t)arm &&
		!nonzero(&arm->seed, sizeof(arm->seed)) &&
		!nonzero(&arm->sealed_seed, sizeof(arm->sealed_seed)) &&
		!nonzero(&arm->invocation, sizeof(arm->invocation)) &&
		arm->wrapped_policy.prepare == wrapped_prepare &&
		arm->wrapped_policy.commit == wrapped_commit &&
		arm->wrapped_policy.abort == wrapped_abort &&
		arm->wrapped_policy.dma_protected == wrapped_dma_protected &&
		arm->wrapped_policy.claim_invocation == wrapped_claim &&
		arm->wrapped_policy.complete_invocation == wrapped_complete &&
		arm->wrapped_policy.fail_stop == wrapped_fail_stop &&
		arm->wrapped_policy.context == &arm->callback_context &&
		arm->wrapped_policy.context_size == sizeof(arm->callback_context);
}

static bool wrapped_protected_storage(void *context, const void *object,
	size_t size)
{
	struct payload_mm_authvar_presence_arm *arm = context;
	struct payload_mm_authvar_presence_arm before;
	payload_mm_authvar_protected_storage proof;
	void *proof_context;
	bool protected;

	if (!object_valid(arm, sizeof(*arm), _Alignof(*arm)) ||
	    arm != platform_payload_mm_authvar_presence_arm())
		return false;
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
	if (!effective_proof_closure(arm, &proof, &proof_context))
		arm_fail_stop(arm);
#else
	proof = arm->protected_storage;
	proof_context = arm->protected_storage_context;
	if (!proof_closure_unchanged(arm, proof, proof_context))
		arm_fail_stop(arm);
#endif
	if (!transaction_provisioning_snapshot_valid(arm,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND))
		arm_fail_stop(arm);
	before = *arm;
	protected = proof(proof_context, object, size);
	if (memcmp(&before, arm, sizeof(before)))
		arm_fail_stop_snapshot(arm, &before);
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
	if (!effective_proof_closure(arm, &proof, &proof_context) ||
	    !transaction_provisioning_snapshot_valid(arm,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND))
		arm_fail_stop(arm);
#else
	if (!proof_closure_unchanged(arm, proof, proof_context) ||
	    !transaction_provisioning_snapshot_valid(arm,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND))
		arm_fail_stop(arm);
#endif
	scrub(&before, sizeof(before));
	return protected;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
enum cb_err payload_mm_authvar_presence_arm_protection_delegate_bind(
	struct payload_mm_authvar_presence_arm *arm,
	payload_mm_authvar_protected_storage protected_storage,
	void *protected_storage_context,
	payload_mm_authvar_protected_storage delegated_protected_storage,
	void *delegated_protected_storage_context,
	size_t delegated_protected_storage_context_size)
{
	uint32_t expected =
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROTECTION_DELEGATION_EMPTY;

	if (!object_valid(arm, sizeof(*arm), _Alignof(*arm)) ||
	    arm != platform_payload_mm_authvar_presence_arm() ||
	    __atomic_load_n(&arm->state, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_LOADER_READY ||
	    !protected_storage || protected_storage != arm->protected_storage ||
	    protected_storage != arm->sealed_protected_storage ||
	    protected_storage_context != arm->protected_storage_context ||
	    protected_storage_context != arm->sealed_protected_storage_context ||
	    !delegated_protected_storage ||
	    !object_valid(delegated_protected_storage_context,
		delegated_protected_storage_context_size, 1U) ||
	    delegated_protected_storage_context_size >
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX ||
	    overlaps(arm, sizeof(*arm), delegated_protected_storage_context,
		delegated_protected_storage_context_size) ||
	    overlaps(arm->composition, sizeof(*arm->composition),
		delegated_protected_storage_context,
		delegated_protected_storage_context_size) ||
	    overlaps(arm->instance, sizeof(*arm->instance),
		delegated_protected_storage_context,
		delegated_protected_storage_context_size) ||
	    overlaps(arm->evidence, sizeof(*arm->evidence),
		delegated_protected_storage_context,
		delegated_protected_storage_context_size) ||
	    !loader_bundle_valid(arm))
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&arm->protection_delegation, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROTECTION_DELEGATION_BINDING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&arm->protection_delegation,
			PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROTECTION_DELEGATION_POISONED,
			__ATOMIC_RELEASE);
		__atomic_store_n(&arm->state,
			PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED, __ATOMIC_RELEASE);
		return CB_ERR;
	}
	if (!protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		(const void *)(uintptr_t)delegated_protected_storage, 1U) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context, delegated_protected_storage_context,
		delegated_protected_storage_context_size) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		(const void *)(uintptr_t)wrapped_protected_storage, 1U) ||
	    !proof_closure_unchanged(arm, protected_storage,
		protected_storage_context) ||
	    !loader_bundle_valid(arm))
		goto poison;
	arm->delegated_protected_storage = delegated_protected_storage;
	arm->delegated_protected_storage_context =
		delegated_protected_storage_context;
	arm->delegated_protected_storage_context_size =
		delegated_protected_storage_context_size;
	arm->sealed_delegated_protected_storage = delegated_protected_storage;
	arm->sealed_delegated_protected_storage_context =
		delegated_protected_storage_context;
	arm->sealed_delegated_protected_storage_context_size =
		delegated_protected_storage_context_size;
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROTECTION_DELEGATION_BINDING;
	if (!__atomic_compare_exchange_n(&arm->protection_delegation, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROTECTION_DELEGATION_BOUND,
		false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		goto poison;
	return CB_SUCCESS;
poison:
	__atomic_store_n(&arm->protection_delegation,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROTECTION_DELEGATION_POISONED,
		__ATOMIC_RELEASE);
	__atomic_store_n(&arm->state, PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED,
		__ATOMIC_RELEASE);
	return CB_ERR;
}
#endif

enum cb_err payload_mm_authvar_presence_arm_provision(
	struct payload_mm_authvar_presence_arm *arm,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence,
	payload_mm_authvar_protected_storage protected_storage,
	void *protected_storage_context)
{
	struct smm_invocation_loader_composition composition_snapshot;
	struct smm_invocation_loader_instance instance_input_snapshot;
	struct smm_invocation_evidence evidence_snapshot;
	struct smm_invocation_loader_instance snapshot = { 0 };
	const struct payload_mm_authvar_presence_arm empty = { 0 };
	uint32_t expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_EMPTY;
	bool dirty;

	if (!object_valid(arm, sizeof(*arm), _Alignof(*arm)) ||
	    arm != platform_payload_mm_authvar_presence_arm() ||
	    !object_valid(composition, sizeof(*composition), _Alignof(*composition)) ||
	    !object_valid(instance, sizeof(*instance), _Alignof(*instance)) ||
	    !object_valid(evidence, sizeof(*evidence), _Alignof(*evidence)) ||
	    !protected_storage ||
	    overlaps(arm, sizeof(*arm), composition, sizeof(*composition)) ||
	    overlaps(arm, sizeof(*arm), instance, sizeof(*instance)) ||
	    overlaps(arm, sizeof(*arm), evidence, sizeof(*evidence)) ||
	    overlaps(composition, sizeof(*composition), instance, sizeof(*instance)) ||
	    overlaps(composition, sizeof(*composition), evidence, sizeof(*evidence)) ||
	    overlaps(instance, sizeof(*instance), evidence, sizeof(*evidence)))
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&arm->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROVISIONING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	dirty = memcmp((const uint8_t *)arm + sizeof(arm->state),
		(const uint8_t *)&empty + sizeof(empty.state),
		sizeof(*arm) - sizeof(arm->state));
	if (dirty || arm->owner_attempt == UINT32_MAX)
		goto fail;
	arm->owner_attempt++;
	composition_snapshot = *composition;
	instance_input_snapshot = *instance;
	evidence_snapshot = *evidence;
	arm->protected_storage = protected_storage;
	arm->protected_storage_context = protected_storage_context;
	arm->sealed_protected_storage = protected_storage;
	arm->sealed_protected_storage_context = protected_storage_context;
	if (!protected_range_bootstrap(arm, protected_storage,
		protected_storage_context, arm, sizeof(*arm)) ||
	    !loader_inputs_unchanged(composition, &composition_snapshot,
		instance, &instance_input_snapshot, evidence, &evidence_snapshot) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context, composition, sizeof(*composition)) ||
	    !loader_inputs_unchanged(composition, &composition_snapshot,
		instance, &instance_input_snapshot, evidence, &evidence_snapshot) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context, instance, sizeof(*instance)) ||
	    !loader_inputs_unchanged(composition, &composition_snapshot,
		instance, &instance_input_snapshot, evidence, &evidence_snapshot) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context, evidence, sizeof(*evidence)) ||
	    !loader_inputs_unchanged(composition, &composition_snapshot,
		instance, &instance_input_snapshot, evidence, &evidence_snapshot) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		(const void *)(uintptr_t)platform_payload_mm_authvar_presence_arm,
		1U) ||
	    !loader_inputs_unchanged(composition, &composition_snapshot,
		instance, &instance_input_snapshot, evidence, &evidence_snapshot) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		(const void *)(uintptr_t)protected_storage, 1U) ||
	    !loader_inputs_unchanged(composition, &composition_snapshot,
		instance, &instance_input_snapshot, evidence, &evidence_snapshot))
		goto fail;
	arm->composition = composition;
	arm->instance = instance;
	arm->evidence = evidence;
	arm->sealed_composition = composition;
	arm->sealed_instance = instance;
	arm->sealed_evidence = evidence;
	if (arm->owner_attempt != 1U ||
	    smm_invocation_loader_composition_evidence(composition, evidence) !=
		evidence ||
	    smm_invocation_loader_instance_read(instance, &snapshot) != CB_SUCCESS ||
	    evidence->active_cpus == 0U ||
	    evidence->active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS ||
	    evidence->bsp_cpu >= evidence->active_cpus ||
	    __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_READY ||
	    !smm_invocation_loader_instance_nonce_equal(
		evidence->loader_instance_nonce, snapshot.loader_instance_nonce) ||
	    evidence->loader_lifecycle != snapshot.lifecycle ||
	    !loader_inputs_unchanged(composition, &composition_snapshot,
		instance, &instance_input_snapshot, evidence, &evidence_snapshot))
		goto fail;
	arm->instance_snapshot = snapshot;
	arm->sealed_instance_snapshot = snapshot;
	arm->active_cpus = evidence->active_cpus;
	arm->bsp_cpu = evidence->bsp_cpu;
	arm->callback_context.arm = arm;
	arm->callback_context.identity = (uintptr_t)arm;
	arm->sealed_callback_context = arm->callback_context;
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROVISIONING;
	if (!__atomic_compare_exchange_n(&arm->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_LOADER_READY, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		goto fail;
	scrub(&snapshot, sizeof(snapshot));
	scrub(&composition_snapshot, sizeof(composition_snapshot));
	scrub(&instance_input_snapshot, sizeof(instance_input_snapshot));
	scrub(&evidence_snapshot, sizeof(evidence_snapshot));
	return CB_SUCCESS;
fail:
	scrub(&snapshot, sizeof(snapshot));
	scrub(&composition_snapshot, sizeof(composition_snapshot));
	scrub(&instance_input_snapshot, sizeof(instance_input_snapshot));
	scrub(&evidence_snapshot, sizeof(evidence_snapshot));
	__atomic_store_n(&arm->state, PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED,
		__ATOMIC_RELEASE);
	return CB_ERR;
}

struct input_range {
	const void *base;
	size_t size;
};

static bool ranges_disjoint(const struct input_range *ranges, size_t count)
{
	for (size_t first = 0; first < count; first++) {
		if (!object_valid(ranges[first].base, ranges[first].size, 1U))
			return false;
		for (size_t second = first + 1U; second < count; second++)
			if (overlaps(ranges[first].base, ranges[first].size,
				ranges[second].base, ranges[second].size))
				return false;
	}
	return true;
}

static bool range_overlaps_any(const struct input_range *ranges, size_t count,
	const void *base, size_t size)
{
	for (size_t index = 0; index < count; index++)
		if (overlaps(ranges[index].base, ranges[index].size, base, size))
			return true;
	return false;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
static bool transaction_proof_arguments_valid(
	struct payload_mm_authvar_presence_arm *arm,
	payload_mm_authvar_protected_storage proof, void *context)
{
	const uint32_t state = __atomic_load_n(&arm->protection_delegation,
		__ATOMIC_ACQUIRE);

	if (state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROTECTION_DELEGATION_BOUND)
		return delegated_proof_closure_valid(arm) &&
			proof == arm->delegated_protected_storage &&
			context == arm->delegated_protected_storage_context;
	if (state != PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROTECTION_DELEGATION_EMPTY ||
	    !delegation_snapshot_valid(arm))
		return false;
	return proof && proof == arm->protected_storage &&
		proof == arm->sealed_protected_storage &&
		context == arm->protected_storage_context &&
		context == arm->sealed_protected_storage_context;
}
#endif

enum cb_err payload_mm_authvar_presence_arm_transaction_provision(
	struct payload_mm_authvar_presence_arm *arm,
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_policy *policy,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct bootmem_reservation_receipt_authority *page_verifier,
	struct bootmem_reservation_receipt *page_receipt,
	payload_mm_authvar_protected_storage protected_storage,
	void *protected_storage_context)
{
	struct payload_mm_authvar_presence_transaction_policy p = { 0 };
	struct payload_mm_authvar_presence_transaction_binding b = { 0 };
	struct bootmem_reservation_receipt_authority verifier = { 0 };
	struct bootmem_reservation_receipt receipt = { 0 };
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX] = { 0 };
	struct input_range ranges[10];
	size_t range_count = 0;
	uint32_t expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_LOADER_READY;

	if (!object_valid(arm, sizeof(*arm), _Alignof(*arm)) ||
	    arm != platform_payload_mm_authvar_presence_arm() ||
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
	    !transaction_proof_arguments_valid(arm, protected_storage,
		protected_storage_context) ||
#else
	    !protected_storage || protected_storage != arm->protected_storage ||
	    protected_storage != arm->sealed_protected_storage ||
	    protected_storage_context != arm->protected_storage_context ||
	    protected_storage_context != arm->sealed_protected_storage_context ||
#endif
	    !object_valid(slot, sizeof(*slot), _Alignof(*slot)) ||
	    !object_valid(policy, sizeof(*policy), _Alignof(*policy)) ||
	    !object_valid(binding, sizeof(*binding), _Alignof(*binding)) ||
	    !object_valid(page_verifier, sizeof(*page_verifier),
		_Alignof(*page_verifier)) ||
	    !object_valid(page_receipt, sizeof(*page_receipt),
		_Alignof(*page_receipt)))
		return CB_ERR;
	p = *policy;
	b = *binding;
	receipt = *page_receipt;
	if (p.context_size <= sizeof(context) &&
	    object_valid(p.context, p.context_size, _Alignof(uint8_t)))
		memcpy(context, p.context, p.context_size);
	if (!__atomic_compare_exchange_n(&arm->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_TRANSACTION_PROVISIONING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto fail_unclaimed;
	if (!protected_range_bootstrap(arm, protected_storage,
		protected_storage_context, arm, sizeof(*arm)) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		(const void *)(uintptr_t)platform_payload_mm_authvar_presence_arm,
		1U))
		goto fail;
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
	if (!transaction_proof_arguments_valid(arm, protected_storage,
		protected_storage_context))
		goto fail;
#else
	if (protected_storage != arm->protected_storage ||
	    protected_storage != arm->sealed_protected_storage ||
	    protected_storage_context != arm->protected_storage_context ||
	    protected_storage_context != arm->sealed_protected_storage_context)
		goto fail;
#endif
	if (receipt.base > UINTPTR_MAX ||
	    receipt.base % PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE ||
	    receipt.bytes != PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE ||
	    receipt.base > UINT64_MAX - receipt.bytes)
		goto fail;
	ranges[range_count++] = (struct input_range) { arm, sizeof(*arm) };
	ranges[range_count++] = (struct input_range) {
		arm->composition, sizeof(*arm->composition) };
	ranges[range_count++] = (struct input_range) {
		arm->instance, sizeof(*arm->instance) };
	ranges[range_count++] = (struct input_range) {
		arm->evidence, sizeof(*arm->evidence) };
	ranges[range_count++] = (struct input_range) { slot, sizeof(*slot) };
	ranges[range_count++] = (struct input_range) { policy, sizeof(*policy) };
	ranges[range_count++] = (struct input_range) { binding, sizeof(*binding) };
	ranges[range_count++] = (struct input_range) {
		page_verifier, sizeof(*page_verifier) };
	ranges[range_count++] = (struct input_range) {
		page_receipt, sizeof(*page_receipt) };
	ranges[range_count++] = (struct input_range) {
		(const void *)(uintptr_t)receipt.base,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE };
	if (!policy_valid(&p) || !binding_valid(&b) ||
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
	    (__atomic_load_n(&arm->protection_delegation, __ATOMIC_ACQUIRE) ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROTECTION_DELEGATION_BOUND &&
	     (p.context != arm->delegated_protected_storage_context ||
	      p.context_size != arm->delegated_protected_storage_context_size)) ||
#endif
	    !ranges_disjoint(ranges, range_count) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		slot, sizeof(*slot)) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		policy, sizeof(*policy)) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		binding, sizeof(*binding)) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		page_verifier, sizeof(*page_verifier)) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		page_receipt, sizeof(*page_receipt)) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		(const void *)(uintptr_t)p.prepare, 1U) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		(const void *)(uintptr_t)p.commit, 1U) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		(const void *)(uintptr_t)p.abort, 1U) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		(const void *)(uintptr_t)p.dma_protected, 1U) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		(const void *)(uintptr_t)p.claim_invocation, 1U) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		(const void *)(uintptr_t)p.complete_invocation, 1U) ||
	    !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		(const void *)(uintptr_t)p.fail_stop, 1U) ||
	    (p.context_size &&
	     (!object_valid(p.context, p.context_size, _Alignof(uint8_t)) ||
	      !protected_range_bootstrap(arm, protected_storage,
		protected_storage_context,
		p.context, p.context_size) ||
	      range_overlaps_any(ranges, range_count, p.context,
		p.context_size))) ||
	    b.maximum_cpus != arm->active_cpus ||
	    b.initiator_cpu != arm->bsp_cpu ||
	    !loader_bundle_valid(arm))
		goto fail;
	arm->binding = b;
	arm->sealed_binding = b;
	arm->policy = p;
	arm->failure_callback = p.fail_stop;
	arm->failure_context_size = p.context_size;
	arm->sealed_failure_callback = p.fail_stop;
	arm->sealed_failure_context_size = p.context_size;
	if (p.context_size) {
		memcpy(arm->policy_context, context, p.context_size);
		memcpy(arm->sealed_policy_context, context, p.context_size);
		memcpy(arm->failure_context, context, p.context_size);
		memcpy(arm->sealed_failure_context, context, p.context_size);
	}
	arm->policy.context = p.context_size ? arm->policy_context : NULL;
	arm->sealed_policy = arm->policy;
	arm->sealed_policy.context = p.context_size ?
		arm->sealed_policy_context : NULL;
	arm->wrapped_policy =
		(struct payload_mm_authvar_presence_transaction_policy) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_POLICY_REVISION,
		.size = sizeof(arm->wrapped_policy),
		.prepare = wrapped_prepare,
		.commit = wrapped_commit,
		.abort = wrapped_abort,
		.dma_protected = wrapped_dma_protected,
		.claim_invocation = wrapped_claim,
		.complete_invocation = wrapped_complete,
		.fail_stop = wrapped_fail_stop,
		.context = &arm->callback_context,
		.context_size = sizeof(arm->callback_context),
	};
	if (memcmp(&p, policy, sizeof(p)) || memcmp(&b, binding, sizeof(b)) ||
	    memcmp(&receipt, page_receipt, sizeof(receipt)) ||
	    (p.context_size && memcmp(context, p.context, p.context_size)) ||
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
	    !transaction_proof_arguments_valid(arm, protected_storage,
		protected_storage_context) ||
#else
	    !proof_closure_unchanged(arm, protected_storage,
		protected_storage_context) ||
#endif
	    memcmp(&arm->binding, &b, sizeof(b)) ||
	    memcmp(&arm->sealed_binding, &b, sizeof(b)) ||
	    arm->policy.prepare != p.prepare || arm->policy.commit != p.commit ||
	    arm->policy.abort != p.abort ||
	    arm->policy.dma_protected != p.dma_protected ||
	    arm->policy.claim_invocation != p.claim_invocation ||
	    arm->policy.complete_invocation != p.complete_invocation ||
	    arm->policy.fail_stop != p.fail_stop ||
	    !transaction_provisioning_snapshot_valid(arm,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_TRANSACTION_PROVISIONING) ||
	    !loader_facts_valid(arm))
		arm_fail_stop(arm);
#if ENV_TEST
	payload_mm_authvar_presence_arm_test_before_verifier_claim(page_verifier);
#endif
	if (!bootmem_reservation_receipt_authority_claim(page_verifier, &verifier)) {
		bootmem_reservation_receipt_close(&verifier);
		scrub(&verifier, sizeof(verifier));
		goto fail;
	}
	if (verifier.generation != b.generation ||
	    memcmp(&receipt, page_receipt, sizeof(receipt)) ||
	    !transaction_provisioning_snapshot_valid(arm,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_TRANSACTION_PROVISIONING))
		arm_fail_stop(arm);
	scrub(page_receipt, sizeof(*page_receipt));
	scrub(&p, sizeof(p));
	scrub(&b, sizeof(b));
	scrub(context, sizeof(context));
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_TRANSACTION_PROVISIONING;
	if (!__atomic_compare_exchange_n(&arm->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND, false, __ATOMIC_RELEASE,
		__ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	if (payload_mm_authvar_presence_transaction_provision(slot,
		&arm->wrapped_policy, &arm->binding, &verifier, &receipt,
		wrapped_protected_storage, arm) == CB_SUCCESS)
		return CB_SUCCESS;
	bootmem_reservation_receipt_close(&verifier);
	scrub(&receipt, sizeof(receipt));
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND;
	if (!__atomic_compare_exchange_n(&arm->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		arm_fail_stop(arm);
	terminal_scrub(arm);
	return CB_ERR;
fail:
	scrub(&p, sizeof(p));
	scrub(&b, sizeof(b));
	scrub(&verifier, sizeof(verifier));
	scrub(&receipt, sizeof(receipt));
	scrub(context, sizeof(context));
	terminal_scrub(arm);
	__atomic_store_n(&arm->state, PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED,
		__ATOMIC_RELEASE);
	return CB_ERR;
fail_unclaimed:
	scrub(&p, sizeof(p));
	scrub(&b, sizeof(b));
	scrub(&verifier, sizeof(verifier));
	scrub(&receipt, sizeof(receipt));
	scrub(context, sizeof(context));
	return CB_ERR;
}
