/* SPDX-License-Identifier: GPL-2.0-only */

#include "authvar_presence_authority_policy.h"
#include "dma_smm_receipt_provision.h"

#include <boot/payload_mm_authvar_presence_authority.h>
#include <boot/payload_mm_authvar_presence_bootstrap.h>
#include <cf9_reset.h>
#include <cpu/x86/smm_invocation_evidence.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "StarBook MTL presence authority is SMM-only"
#endif

enum authority_state { AUTHORITY_EMPTY, AUTHORITY_BOUND, AUTHORITY_INSTALLING,
	AUTHORITY_PREPARED, AUTHORITY_COMMITTED, AUTHORITY_RESTRICTING,
	AUTHORITY_CLOSED, AUTHORITY_FAILED };

/* Mutable authority never lives in the route's immutable callback context. */
static struct {
	uint32_t state;
	uint32_t sealed_state;
	struct payload_mm_authvar_presence_transaction_binding binding;
	struct payload_mm_authvar_presence_transaction_binding sealed_binding;
	uint8_t capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE];
	uint8_t sealed_capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE];
} owner __aligned(8);

static void publish_state(uint32_t state)
{
	__atomic_store_n(&owner.sealed_state, state, __ATOMIC_RELEASE);
	__atomic_store_n(&owner.state, state, __ATOMIC_RELEASE);
}

static void scrub(void *object, size_t size)
{
	volatile uint8_t *bytes = object;

	while (size--)
		*bytes++ = 0;
}

static bool protected_storage(void *unused, const void *base, size_t size)
{
	const struct smm_invocation_runtime_view *view;

	(void)unused;
	return smm_invocation_runtime_view_get(&view) == CB_SUCCESS &&
		smm_invocation_runtime_range_is_protected(view, base, size) == CB_SUCCESS;
}

static bool overlaps(const void *left, size_t left_size, const void *right,
	size_t right_size)
{
	const uintptr_t first = (uintptr_t)left;
	const uintptr_t second = (uintptr_t)right;

	return first <= second ? second - first < left_size : first - second < right_size;
}

static bool binding_valid(void)
{
	const struct payload_mm_authvar_presence_transaction_binding *binding;
	const uint32_t state = __atomic_load_n(&owner.state, __ATOMIC_ACQUIRE);

	return protected_storage(NULL, &owner, sizeof(owner)) &&
		state >= AUTHORITY_BOUND && state <= AUTHORITY_CLOSED &&
		state == __atomic_load_n(&owner.sealed_state, __ATOMIC_ACQUIRE) &&
		payload_mm_authvar_presence_bootstrap_binding_get(&binding) == CB_SUCCESS &&
		!memcmp(binding, &owner.binding, sizeof(*binding)) &&
		!memcmp(&owner.binding, &owner.sealed_binding, sizeof(*binding));
}

static __noreturn void fail_stop(void *unused)
{
	(void)unused;
	smm_invocation_platform_fail_stop();
}

static bool dma_protected(void *unused, uint64_t base, uint64_t size)
{
	(void)unused;
	return binding_valid() &&
		starbook_mtl_dma_smm_cold_range_protected(NULL, base, size);
}

static bool cpu_rendezvous_proven(void)
{
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_token token;

	return binding_valid() &&
		smm_invocation_runtime_binding_get(&runtime) == CB_SUCCESS &&
		smm_invocation_evidence_claimed_snapshot(runtime.evidence,
			SMM_APMC_AUTHVAR_PRESENCE, SMM_APMC_AUTHVAR_PRESENCE_SENTINEL,
			&token) == CB_SUCCESS &&
		token.initiator_cpu == owner.binding.initiator_cpu &&
		token.active_cpus == owner.binding.maximum_cpus;
}

static bool cpu_rendezvous_active(void *unused)
{
	const uint32_t state = __atomic_load_n(&owner.state, __ATOMIC_ACQUIRE);

	(void)unused;
	/* PREPARE may install, but an operator cannot use its authority before COMMIT. */
	return (state == AUTHORITY_INSTALLING || state == AUTHORITY_COMMITTED ||
		state == AUTHORITY_RESTRICTING) &&
		cpu_rendezvous_proven() &&
		__atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) == state;
}

static enum cb_err provision(void *unused, uint64_t generation,
	uint8_t capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE])
{
	(void)unused;
	if (!capability || !protected_storage(NULL, capability,
		LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE) ||
	    overlaps(capability, LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE, &owner, sizeof(owner)) ||
	    !binding_valid() ||
	    __atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) != AUTHORITY_INSTALLING ||
	    generation != owner.binding.generation ||
	    memcmp(owner.capability, owner.sealed_capability, sizeof(owner.capability)))
		return CB_ERR;
	memcpy(capability, owner.capability, LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE);
	return CB_SUCCESS;
}

static void cold_reset(void *unused)
{
	(void)unused;
	cf9_reset_prepare();
	do_full_reset();
	/* A failed cold reset must not return to the payload or halt only one CPU. */
	fail_stop(NULL);
}

static enum cb_err prepare(void *unused,
	const struct payload_mm_authvar_presence_seed *seed, uint64_t generation)
{
	struct payload_mm_authvar_presence_policy policy;
	uint32_t expected = AUTHORITY_BOUND;
	enum cb_err result;

	(void)unused;
	if (!seed || !binding_valid() || generation != owner.binding.generation ||
	    (uintptr_t)seed % _Alignof(*seed) ||
	    !protected_storage(NULL, seed, sizeof(*seed)) ||
	    overlaps(seed, sizeof(*seed), &owner, sizeof(owner)) ||
	    seed->revision != PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION ||
	    seed->size != sizeof(*seed) || seed->endpoint.generation != generation ||
	    !__atomic_compare_exchange_n(&owner.state, &expected, AUTHORITY_INSTALLING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	publish_state(AUTHORITY_INSTALLING);
	memcpy(owner.capability, seed->capability, sizeof(owner.capability));
	memcpy(owner.sealed_capability, seed->capability, sizeof(owner.sealed_capability));
	policy = (struct payload_mm_authvar_presence_policy) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_POLICY_REVISION,
		.size = sizeof(policy),
		.endpoint = seed->endpoint,
		.backing = seed->backing,
		.provision = provision,
		.dma_protected = dma_protected,
		.cpu_rendezvous_active = cpu_rendezvous_active,
		.cold_reset = cold_reset,
		.fail_stop = fail_stop,
	};
	/* The existing authority consumes the exact authenticated backing evidence. */
	result = payload_mm_authvar_presence_authority_install(&policy,
		protected_storage, NULL);
	if (!binding_valid() || seed->revision != PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION ||
	    seed->size != sizeof(*seed) ||
	    memcmp(&seed->endpoint, &policy.endpoint, sizeof(seed->endpoint)) ||
	    memcmp(&seed->backing, &policy.backing, sizeof(seed->backing)) ||
	    memcmp(seed->capability, owner.sealed_capability, sizeof(seed->capability)))
		fail_stop(NULL);
	scrub(owner.capability, sizeof(owner.capability));
	scrub(owner.sealed_capability, sizeof(owner.sealed_capability));
	scrub(&policy, sizeof(policy));
	publish_state(result == CB_SUCCESS ? AUTHORITY_PREPARED : AUTHORITY_FAILED);
	return result;
}

static enum cb_err commit(void *unused, uint64_t generation)
{
	uint32_t expected = AUTHORITY_PREPARED;

	(void)unused;
	if (!binding_valid() || generation != owner.binding.generation ||
	    !cpu_rendezvous_proven())
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&owner.state, &expected, AUTHORITY_COMMITTED,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	publish_state(AUTHORITY_COMMITTED);
	return CB_SUCCESS;
}

static enum cb_err abort_authority(void *unused, uint64_t generation)
{
	uint32_t state;

	(void)unused;
	if (!binding_valid() || generation != owner.binding.generation)
		return CB_ERR;
	state = __atomic_load_n(&owner.state, __ATOMIC_ACQUIRE);
	if (state == AUTHORITY_CLOSED)
		return CB_SUCCESS;
	if ((state != AUTHORITY_PREPARED && state != AUTHORITY_COMMITTED) ||
	    !cpu_rendezvous_proven() ||
	    !__atomic_compare_exchange_n(&owner.state, &state, AUTHORITY_RESTRICTING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	publish_state(AUTHORITY_RESTRICTING);
	if (payload_mm_authvar_presence_authority_restrict(generation) != CB_SUCCESS) {
		publish_state(AUTHORITY_FAILED);
		return CB_ERR;
	}
	publish_state(AUTHORITY_CLOSED);
	return CB_SUCCESS;
}

static const struct payload_mm_authvar_presence_route_authority_policy route_policy = {
	.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POLICY_REVISION,
	.size = sizeof(route_policy),
	.prepare = prepare,
	.commit = commit,
	.abort = abort_authority,
	.dma_protected = dma_protected,
	.fail_stop = fail_stop,
};

enum cb_err starbook_mtl_authvar_presence_authority_policy_get(
	const struct payload_mm_authvar_presence_route_authority_policy **policy)
{
	const struct payload_mm_authvar_presence_transaction_binding *binding;
	uint32_t expected = AUTHORITY_EMPTY;

	if (!policy || (uintptr_t)policy % _Alignof(*policy) ||
	    !protected_storage(NULL, policy, sizeof(*policy)) ||
	    !protected_storage(NULL, &owner, sizeof(owner)) ||
	    !protected_storage(NULL, &route_policy, sizeof(route_policy)) ||
	    overlaps(policy, sizeof(*policy), &owner, sizeof(owner)) ||
	    overlaps(policy, sizeof(*policy), &route_policy, sizeof(route_policy)))
		return CB_ERR;
	if (payload_mm_authvar_presence_bootstrap_binding_get(&binding) != CB_SUCCESS) {
		*policy = NULL;
		return CB_ERR;
	}
	if (overlaps(policy, sizeof(*policy), binding, sizeof(*binding)))
		return CB_ERR;
	*policy = NULL;
	if (!__atomic_compare_exchange_n(&owner.state, &expected, AUTHORITY_INSTALLING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	owner.binding = owner.sealed_binding = *binding;
	publish_state(AUTHORITY_BOUND);
	if (!binding_valid()) {
		publish_state(AUTHORITY_FAILED);
		return CB_ERR;
	}
	*policy = &route_policy;
	return CB_SUCCESS;
}
