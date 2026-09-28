/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_authority.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close.h>
#include <commonlib/bsd/compiler.h>
#include <commonlib/helpers.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <stdint.h>
#include <string.h>

#include "payload_mm_authvar_presence_lifecycle_close_internal.h"

enum close_state {
	CLOSE_EMPTY,
	CLOSE_PROVISIONING,
	CLOSE_READY,
	CLOSE_CLAIMING,
	CLOSE_CLAIMED,
	CLOSE_RESTRICTING,
	CLOSE_PROVING,
	CLOSE_COMPLETING,
	CLOSE_POISONED,
};

enum source_state {
	SOURCE_EMPTY,
	SOURCE_CLAIMED,
	SOURCE_CONSUMED,
};

#define PRE_EXTERNAL_CLAIM 0x50524558434c4f53ULL
#define PAYLOAD_FAILURE_CLAIM 0x5041594c434c4f53ULL
#define WARM_RESET_CLAIM 0x5741524d434c4f53ULL
#define S3_RESUME_CLAIM 0x53335245434c4f53ULL
#define CLOSED_REPROOF_CLAIM 0x52455052434c4f53ULL

struct close_owner {
	struct payload_mm_authvar_presence_lifecycle_close_policy policy;
	struct payload_mm_authvar_presence_lifecycle_close_policy sealed_policy;
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX];
	uint8_t sealed_context[
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX];
	payload_mm_authvar_protected_storage protected_storage;
	payload_mm_authvar_protected_storage sealed_protected_storage;
	void *protected_storage_context;
	void *sealed_protected_storage_context;
	uint64_t generation;
	uint64_t sealed_generation;
	uint64_t last_invocation_generation;
	uint64_t last_invocation_generation_inverse;
	uint32_t state;
	uint32_t state_inverse;
} __aligned(8);

static struct close_owner owner;

#if ENV_TEST
static payload_mm_authvar_presence_lifecycle_close_test_hook_fn after_claim_hook;
static payload_mm_authvar_presence_lifecycle_close_test_hook_fn
	after_restrict_hook;
static payload_mm_authvar_presence_lifecycle_close_test_hook_fn after_proof_hook;
static payload_mm_authvar_presence_lifecycle_close_test_hook_fn
	after_complete_hook;
#endif

static void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r"(buffer) : "memory");
}

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool objects_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	uintptr_t first_base = (uintptr_t)first;
	uintptr_t second_base = (uintptr_t)second;

	return first_base <= second_base + second_size - 1U &&
		second_base <= first_base + first_size - 1U;
}

static bool invocation_valid(
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	const struct payload_mm_authvar_presence_lifecycle_close_policy *policy)
{
	return invocation->revision ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_INVOCATION_REVISION &&
		invocation->size == sizeof(*invocation) && invocation->active_cpus &&
		invocation->initiator_cpu == policy->initiator_cpu &&
		invocation->active_cpus == policy->maximum_cpus &&
		invocation->smi_generation &&
		invocation->rendezvous_generation == invocation->smi_generation &&
		!bytes_zero(invocation->rendezvous_proof,
			sizeof(invocation->rendezvous_proof)) &&
		invocation->bsp == 1U && !invocation->reserved;
}

static bool policy_equal(const struct close_owner *trusted)
{
	return !memcmp(&trusted->policy, &trusted->sealed_policy,
			offsetof(struct payload_mm_authvar_presence_lifecycle_close_policy,
				context)) &&
		trusted->policy.context_size == trusted->sealed_policy.context_size &&
		trusted->policy.context ==
			(trusted->policy.context_size ? trusted->context : NULL) &&
		trusted->sealed_policy.context ==
			(trusted->sealed_policy.context_size ?
			 trusted->sealed_context : NULL) &&
		!memcmp(trusted->context, trusted->sealed_context,
			trusted->policy.context_size) &&
		trusted->generation &&
		trusted->generation == trusted->sealed_generation &&
		trusted->generation == trusted->policy.generation &&
		trusted->generation == trusted->sealed_policy.generation &&
		trusted->last_invocation_generation != UINT64_MAX &&
		trusted->last_invocation_generation_inverse ==
			~trusted->last_invocation_generation &&
		trusted->protected_storage == trusted->sealed_protected_storage &&
		trusted->protected_storage_context ==
			trusted->sealed_protected_storage_context;
}

static void owner_snapshot(struct close_owner *snapshot)
{
	*snapshot = owner;
	snapshot->policy.context = snapshot->policy.context_size ?
		snapshot->context : NULL;
	snapshot->sealed_policy.context = snapshot->sealed_policy.context_size ?
		snapshot->sealed_context : NULL;
}

static bool owner_matches(const struct close_owner *snapshot, uint32_t state)
{
	struct close_owner expected = *snapshot;
	bool equal;

	expected.policy.context = expected.policy.context_size ? owner.context : NULL;
	expected.sealed_policy.context = expected.sealed_policy.context_size ?
		owner.sealed_context : NULL;
	expected.state = state;
	expected.state_inverse = ~state;
	equal = !memcmp(&owner, &expected, sizeof(expected));
	scrub(&expected, sizeof(expected));
	return equal;
}

static bool protected_exact(const struct close_owner *trusted,
	const void *base, size_t size)
{
	struct close_owner before;
	bool protected;

	if (!policy_equal(trusted) || !trusted->sealed_protected_storage)
		return false;
	before = *trusted;
	protected = trusted->sealed_protected_storage(
		trusted->sealed_protected_storage_context, base, size);
	if (memcmp(&before, trusted, sizeof(before))) {
		scrub(&before, sizeof(before));
		return false;
	}
	scrub(&before, sizeof(before));
	return protected;
}

static bool owner_valid(const struct close_owner *trusted, uint32_t state)
{
	return __atomic_load_n(&trusted->state, __ATOMIC_ACQUIRE) == state &&
		__atomic_load_n(&trusted->state_inverse, __ATOMIC_ACQUIRE) == ~state &&
		policy_equal(trusted) && protected_exact(trusted, trusted,
			sizeof(*trusted)) &&
		trusted->policy.transaction_slot ==
			smm_get_payload_mm_authvar_presence_transaction_slot() &&
		payload_mm_authvar_presence_transaction_dispatch_enabled(
			trusted->policy.transaction_slot, trusted->generation) &&
		protected_exact(trusted, trusted->policy.transaction_slot,
			sizeof(*trusted->policy.transaction_slot)) &&
		protected_exact(trusted,
			(const void *)(uintptr_t)trusted->policy.claim_invocation, 1U) &&
		protected_exact(trusted,
			(const void *)(uintptr_t)trusted->policy.complete_invocation, 1U) &&
		protected_exact(trusted,
			(const void *)(uintptr_t)trusted->protected_storage, 1U) &&
		(!trusted->policy.context_size ||
		 protected_exact(trusted, trusted->policy.context,
			trusted->policy.context_size));
}

static void state_publish(uint32_t state)
{
	__atomic_store_n(&owner.state_inverse, ~state, __ATOMIC_RELAXED);
	__atomic_store_n(&owner.state, state, __ATOMIC_RELEASE);
}

static __noreturn void fail_stop(const struct close_owner *trusted)
{
	(void)trusted;
	state_publish(CLOSE_POISONED);
	smm_invocation_platform_fail_stop();
	__builtin_trap();
}

static uint64_t completion_value(uint64_t source, uint64_t generation,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	uint64_t value = 0x434c4f5345440000ULL ^ generation ^
		(invocation->smi_generation << 17 | invocation->smi_generation >> 47) ^
		source;

	return value ? value : 0x434c4f534544ffffULL;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_provision(
	const struct payload_mm_authvar_presence_lifecycle_close_policy *policy,
	payload_mm_authvar_protected_storage storage_is_protected,
	void *storage_context)
{
	struct payload_mm_authvar_presence_lifecycle_close_policy snapshot = { 0 };
	struct payload_mm_authvar_presence_lifecycle_close_slot *slots[] = {
		payload_mm_authvar_presence_pre_external_image_close_slot(),
		payload_mm_authvar_presence_payload_failure_close_slot(),
		payload_mm_authvar_presence_warm_reset_close_slot(),
		payload_mm_authvar_presence_s3_resume_close_slot(),
		payload_mm_authvar_presence_closed_reproof_slot(),
	};
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX] = { 0 };
	uint32_t expected = CLOSE_EMPTY;

	if (!object_valid(policy, sizeof(*policy), _Alignof(*policy)) ||
		!storage_is_protected ||
		!__atomic_compare_exchange_n(&owner.state, &expected,
			CLOSE_PROVISIONING, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
		return CB_ERR;
	if (!bytes_zero(&owner, offsetof(struct close_owner, state)) ||
		__atomic_load_n(&owner.state_inverse, __ATOMIC_ACQUIRE))
		goto reject;
	owner.state_inverse = ~(uint32_t)CLOSE_PROVISIONING;
	snapshot = *policy;
	if (snapshot.revision !=
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_POLICY_REVISION ||
		snapshot.size != sizeof(snapshot) || !snapshot.generation ||
		!snapshot.predecessor_invocation_generation ||
		snapshot.predecessor_invocation_generation == UINT64_MAX ||
		!snapshot.maximum_cpus ||
		snapshot.initiator_cpu >= snapshot.maximum_cpus ||
		!snapshot.transaction_slot ||
		!snapshot.claim_invocation || !snapshot.complete_invocation ||
		snapshot.context_size > sizeof(context) ||
		!!snapshot.context != !!snapshot.context_size ||
		(snapshot.context_size &&
		 ((uintptr_t)snapshot.context + snapshot.context_size <
		  (uintptr_t)snapshot.context)))
		goto reject;
	if (!object_valid(snapshot.transaction_slot,
			sizeof(*snapshot.transaction_slot),
			_Alignof(*snapshot.transaction_slot)) ||
		snapshot.transaction_slot !=
			smm_get_payload_mm_authvar_presence_transaction_slot() ||
		(snapshot.context_size &&
		 !object_valid(snapshot.context, snapshot.context_size,
			_Alignof(uint8_t))))
		goto reject;
	if (snapshot.context_size)
		memcpy(context, snapshot.context, snapshot.context_size);
	if (objects_overlap(&owner, sizeof(owner), policy, sizeof(*policy)) ||
		objects_overlap(&owner, sizeof(owner), snapshot.transaction_slot,
			sizeof(*snapshot.transaction_slot)) ||
		objects_overlap(policy, sizeof(*policy), snapshot.transaction_slot,
			sizeof(*snapshot.transaction_slot)) ||
		(snapshot.context_size &&
		 (objects_overlap(&owner, sizeof(owner), snapshot.context,
			snapshot.context_size) ||
		  objects_overlap(policy, sizeof(*policy), snapshot.context,
			snapshot.context_size) ||
		  objects_overlap(snapshot.transaction_slot,
			sizeof(*snapshot.transaction_slot), snapshot.context,
			snapshot.context_size))))
		goto reject;
	for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
		if (!slots[i] || !bytes_zero(slots[i], sizeof(*slots[i])) ||
			!storage_is_protected(storage_context, slots[i],
				sizeof(*slots[i])))
			goto reject;
		for (size_t j = 0; j < i; j++)
			if (slots[i] == slots[j])
				goto reject;
		if (objects_overlap(&owner, sizeof(owner), slots[i],
				sizeof(*slots[i])) ||
			objects_overlap(policy, sizeof(*policy), slots[i],
				sizeof(*slots[i])) ||
			objects_overlap(snapshot.transaction_slot,
				sizeof(*snapshot.transaction_slot), slots[i],
				sizeof(*slots[i])) ||
			(snapshot.context_size && objects_overlap(snapshot.context,
				snapshot.context_size, slots[i], sizeof(*slots[i]))))
			goto reject;
	}
	if (!storage_is_protected(storage_context,
			(const void *)(uintptr_t)storage_is_protected, 1U) ||
		!storage_is_protected(storage_context, &owner, sizeof(owner)) ||
		!storage_is_protected(storage_context, policy, sizeof(*policy)) ||
		!storage_is_protected(storage_context, snapshot.transaction_slot,
			sizeof(*snapshot.transaction_slot)) ||
		!storage_is_protected(storage_context,
			(const void *)(uintptr_t)snapshot.claim_invocation, 1U) ||
		!storage_is_protected(storage_context,
			(const void *)(uintptr_t)snapshot.complete_invocation, 1U) ||
		!payload_mm_authvar_presence_transaction_dispatch_enabled(
			snapshot.transaction_slot, snapshot.generation) ||
		(snapshot.context_size &&
		 !storage_is_protected(storage_context, snapshot.context,
			snapshot.context_size)) || memcmp(&snapshot, policy, sizeof(snapshot)) ||
		(snapshot.context_size &&
		 memcmp(context, snapshot.context, snapshot.context_size)))
		goto reject;
	if (!bytes_zero(&owner, offsetof(struct close_owner, state)) ||
		__atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) !=
			CLOSE_PROVISIONING ||
		__atomic_load_n(&owner.state_inverse, __ATOMIC_ACQUIRE) !=
			~(uint32_t)CLOSE_PROVISIONING)
		goto reject;
	for (size_t i = 0; i < ARRAY_SIZE(slots); i++)
		if (!bytes_zero(slots[i], sizeof(*slots[i])))
			goto reject;
	owner.policy = owner.sealed_policy = snapshot;
	if (snapshot.context_size) {
		memcpy(owner.context, context, snapshot.context_size);
		memcpy(owner.sealed_context, context, snapshot.context_size);
	}
	owner.policy.context = owner.policy.context_size ? owner.context : NULL;
	owner.sealed_policy.context = owner.sealed_policy.context_size ?
		owner.sealed_context : NULL;
	owner.protected_storage = owner.sealed_protected_storage =
		storage_is_protected;
	owner.protected_storage_context = owner.sealed_protected_storage_context =
		storage_context;
	owner.generation = owner.sealed_generation = snapshot.generation;
	owner.last_invocation_generation =
		snapshot.predecessor_invocation_generation;
	owner.last_invocation_generation_inverse =
		~snapshot.predecessor_invocation_generation;
	if (!owner_valid(&owner, CLOSE_PROVISIONING))
		goto reject;
	for (size_t i = 0; i < ARRAY_SIZE(slots); i++)
		__atomic_store_n(&slots[i]->state_inverse,
			~(uint32_t)SOURCE_EMPTY, __ATOMIC_RELEASE);
	scrub(context, sizeof(context));
	scrub(&snapshot, sizeof(snapshot));
	state_publish(CLOSE_READY);
	return CB_SUCCESS;

reject:
	scrub(context, sizeof(context));
	scrub(&snapshot, sizeof(snapshot));
	scrub((uint8_t *)&owner, offsetof(struct close_owner, state));
	state_publish(CLOSE_POISONED);
	return CB_ERR;
}

static bool slot_state(
	const struct payload_mm_authvar_presence_lifecycle_close_slot *slot,
	uint32_t state)
{
	return protected_exact(&owner, slot, sizeof(*slot)) &&
		__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) == state &&
		__atomic_load_n(&slot->state_inverse, __ATOMIC_ACQUIRE) == ~state;
}

static bool prerequisite_valid(bool required)
{
	return !required ||
		slot_state(payload_mm_authvar_presence_pre_external_image_close_slot(),
			SOURCE_CONSUMED);
}

static enum cb_err close_source(
	struct payload_mm_authvar_presence_lifecycle_close_slot *slot,
	uint64_t claim_value, bool require_pre_external)
{
	struct payload_mm_authvar_presence_transaction_invocation invocation = { 0 };
	struct payload_mm_authvar_presence_transaction_invocation claimed;
	struct close_owner trusted;
	uint64_t value;
	uint32_t expected = CLOSE_READY;
	uint32_t source_expected = SOURCE_EMPTY;
	enum cb_err status;

	if (!slot || !claim_value)
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&owner.state, &expected, CLOSE_CLAIMING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		if (expected == CLOSE_EMPTY)
			return CB_ERR;
		smm_invocation_platform_fail_stop();
	}
	owner.state_inverse = ~(uint32_t)CLOSE_CLAIMING;
	owner_snapshot(&trusted);
	if (!owner_valid(&owner, CLOSE_CLAIMING))
		fail_stop(&trusted);
	if (!prerequisite_valid(require_pre_external))
		fail_stop(&trusted);
	if (!protected_exact(&owner, slot, sizeof(*slot)) ||
		__atomic_load_n(&slot->state_inverse, __ATOMIC_ACQUIRE) !=
			~(uint32_t)SOURCE_EMPTY ||
		!__atomic_compare_exchange_n(&slot->state, &source_expected,
			SOURCE_CLAIMED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		fail_stop(&trusted);
	__atomic_store_n(&slot->state_inverse, ~(uint32_t)SOURCE_CLAIMED,
		__ATOMIC_RELEASE);
	status = trusted.policy.claim_invocation(trusted.policy.context,
		claim_value, &invocation);
	if (status != CB_SUCCESS) {
		if (!bytes_zero(&invocation, sizeof(invocation)) ||
			!owner_matches(&trusted, CLOSE_CLAIMING) ||
			!owner_valid(&owner, CLOSE_CLAIMING) ||
			__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
				SOURCE_CLAIMED ||
			__atomic_load_n(&slot->state_inverse, __ATOMIC_ACQUIRE) !=
				~(uint32_t)SOURCE_CLAIMED)
			fail_stop(&trusted);
		__atomic_store_n(&slot->state_inverse, ~(uint32_t)SOURCE_EMPTY,
			__ATOMIC_RELAXED);
		__atomic_store_n(&slot->state, SOURCE_EMPTY, __ATOMIC_RELEASE);
		scrub(&trusted, sizeof(trusted));
		scrub(&invocation, sizeof(invocation));
		state_publish(CLOSE_READY);
		return CB_ERR;
	}
#if ENV_TEST
	if (after_claim_hook)
		after_claim_hook();
#endif
	if (!invocation_valid(&invocation, &trusted.policy) ||
		invocation.smi_generation <= trusted.last_invocation_generation ||
		invocation.smi_generation == UINT64_MAX ||
		!owner_matches(&trusted, CLOSE_CLAIMING) ||
		!owner_valid(&owner, CLOSE_CLAIMING) ||
		!slot_state(slot, SOURCE_CLAIMED) ||
		!prerequisite_valid(require_pre_external))
		fail_stop(&trusted);
	claimed = invocation;
	owner.last_invocation_generation = invocation.smi_generation;
	owner.last_invocation_generation_inverse = ~invocation.smi_generation;
	trusted.last_invocation_generation = invocation.smi_generation;
	trusted.last_invocation_generation_inverse = ~invocation.smi_generation;
	state_publish(CLOSE_CLAIMED);
	if (!owner_valid(&owner, CLOSE_CLAIMED) ||
		!slot_state(slot, SOURCE_CLAIMED) ||
		!prerequisite_valid(require_pre_external))
		fail_stop(&trusted);
	state_publish(CLOSE_RESTRICTING);
	if (payload_mm_authvar_presence_authority_restrict(
			trusted.generation) != CB_SUCCESS)
		fail_stop(&trusted);
#if ENV_TEST
	if (after_restrict_hook)
		after_restrict_hook();
#endif
	if (!owner_valid(&owner, CLOSE_RESTRICTING) ||
		!slot_state(slot, SOURCE_CLAIMED) ||
		!prerequisite_valid(require_pre_external))
		fail_stop(&trusted);
	state_publish(CLOSE_PROVING);
	if (payload_mm_authvar_presence_authority_restrict(
			trusted.generation) != CB_SUCCESS)
		fail_stop(&trusted);
#if ENV_TEST
	if (after_proof_hook)
		after_proof_hook();
#endif
	if (!owner_valid(&owner, CLOSE_PROVING) ||
		!slot_state(slot, SOURCE_CLAIMED) ||
		!prerequisite_valid(require_pre_external))
		fail_stop(&trusted);
	value = completion_value(claim_value, trusted.generation,
		&invocation);
	state_publish(CLOSE_COMPLETING);
	status = trusted.policy.complete_invocation(trusted.policy.context,
		&invocation, value);
#if ENV_TEST
	if (after_complete_hook)
		after_complete_hook();
#endif
	if (status != CB_SUCCESS || memcmp(&invocation, &claimed, sizeof(claimed)) ||
		!owner_matches(&trusted, CLOSE_COMPLETING) ||
		!owner_valid(&owner, CLOSE_COMPLETING) ||
		!slot_state(slot, SOURCE_CLAIMED) ||
		!prerequisite_valid(require_pre_external))
		fail_stop(&trusted);
	__atomic_store_n(&slot->state_inverse, ~(uint32_t)SOURCE_CONSUMED,
		__ATOMIC_RELAXED);
	__atomic_store_n(&slot->state, SOURCE_CONSUMED, __ATOMIC_RELEASE);
	scrub(&invocation, sizeof(invocation));
	scrub(&claimed, sizeof(claimed));
	scrub(&trusted, sizeof(trusted));
	state_publish(CLOSE_READY);
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_pre_external_image(
	struct payload_mm_authvar_presence_lifecycle_close_slot *slot)
{
	if (slot != payload_mm_authvar_presence_pre_external_image_close_slot())
		return CB_ERR;
	return close_source(slot, PRE_EXTERNAL_CLAIM, false);
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_payload_failure(
	struct payload_mm_authvar_presence_lifecycle_close_slot *slot)
{
	if (slot != payload_mm_authvar_presence_payload_failure_close_slot())
		return CB_ERR;
	return close_source(slot, PAYLOAD_FAILURE_CLAIM, false);
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_warm_reset(
	struct payload_mm_authvar_presence_lifecycle_close_slot *slot)
{
	if (slot != payload_mm_authvar_presence_warm_reset_close_slot())
		return CB_ERR;
	return close_source(slot, WARM_RESET_CLAIM, false);
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_s3_resume(
	struct payload_mm_authvar_presence_lifecycle_close_slot *slot)
{
	if (slot != payload_mm_authvar_presence_s3_resume_close_slot())
		return CB_ERR;
	return close_source(slot, S3_RESUME_CLAIM, false);
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_closed_reproof(
	struct payload_mm_authvar_presence_lifecycle_close_slot *slot)
{
	if (slot != payload_mm_authvar_presence_closed_reproof_slot())
		return CB_ERR;
	return close_source(slot, CLOSED_REPROOF_CLAIM, true);
}

#if ENV_TEST
void payload_mm_authvar_presence_lifecycle_close_reset_test(void)
{
	memset(&owner, 0, sizeof(owner));
	payload_mm_authvar_presence_pre_external_image_close_reset_test();
	payload_mm_authvar_presence_payload_failure_close_reset_test();
	payload_mm_authvar_presence_warm_reset_close_reset_test();
	payload_mm_authvar_presence_s3_resume_close_reset_test();
	payload_mm_authvar_presence_closed_reproof_reset_test();
	after_claim_hook = NULL;
	after_restrict_hook = NULL;
	after_proof_hook = NULL;
	after_complete_hook = NULL;
}

const void *payload_mm_authvar_presence_lifecycle_close_state_test(size_t *size)
{
	if (size)
		*size = sizeof(owner);
	return &owner;
}

void payload_mm_authvar_presence_lifecycle_close_after_claim_test_hook(
	payload_mm_authvar_presence_lifecycle_close_test_hook_fn hook)
{
	after_claim_hook = hook;
}

void payload_mm_authvar_presence_lifecycle_close_after_restrict_test_hook(
	payload_mm_authvar_presence_lifecycle_close_test_hook_fn hook)
{
	after_restrict_hook = hook;
}

void payload_mm_authvar_presence_lifecycle_close_after_proof_test_hook(
	payload_mm_authvar_presence_lifecycle_close_test_hook_fn hook)
{
	after_proof_hook = hook;
}

void payload_mm_authvar_presence_lifecycle_close_after_complete_test_hook(
	payload_mm_authvar_presence_lifecycle_close_test_hook_fn hook)
{
	after_complete_hook = hook;
}

uint64_t payload_mm_authvar_presence_lifecycle_close_value_test(uint64_t source,
	uint64_t generation,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	return completion_value(source, generation, invocation);
}
#endif
