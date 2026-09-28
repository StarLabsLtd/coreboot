/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_route_session.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Authenticated-variable presence route session is SMM-only"
#endif

enum route_round {
	ROUTE_ROUND_PREPARE = 1U,
	ROUTE_ROUND_DECISION,
};

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

struct route_range {
	const void *base;
	size_t size;
};

static bool ranges_disjoint(const struct route_range *ranges, size_t count)
{
	size_t first, second;

	for (first = 0; first < count; ++first) {
		if (!object_valid(ranges[first].base, ranges[first].size, 1U))
			return false;
		for (second = first + 1U; second < count; ++second) {
			if (overlaps(ranges[first].base, ranges[first].size,
				ranges[second].base, ranges[second].size))
				return false;
		}
	}
	return true;
}

static bool provision_ranges_disjoint(
	struct payload_mm_authvar_presence_route_session *session,
	struct payload_mm_authvar_presence_arm *arm,
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_save_state_ops *ops,
	const struct smm_invocation_save_state_ops *ops_snapshot,
	const struct payload_mm_authvar_presence_route_authority_policy *policy,
	const struct payload_mm_authvar_presence_route_authority_policy
		*policy_snapshot,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct bootmem_reservation_receipt_authority *page_verifier,
	struct bootmem_reservation_receipt *page_receipt,
	const struct bootmem_reservation_receipt *receipt_snapshot)
{
	struct route_range ranges[15];
	size_t count = 0;

	ranges[count++] = (struct route_range) { session, sizeof(*session) };
	ranges[count++] = (struct route_range) { arm, sizeof(*arm) };
	ranges[count++] = (struct route_range) { slot, sizeof(*slot) };
	ranges[count++] = (struct route_range) { composition, sizeof(*composition) };
	ranges[count++] = (struct route_range) { instance, sizeof(*instance) };
	ranges[count++] = (struct route_range) { evidence, sizeof(*evidence) };
	ranges[count++] = (struct route_range) { topology, sizeof(*topology) };
	ranges[count++] = (struct route_range) {
		(void *)(uintptr_t)receipt_snapshot->base,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE };
	ranges[count++] = (struct route_range) { page_verifier,
		sizeof(*page_verifier) };
	ranges[count++] = (struct route_range) { page_receipt,
		sizeof(*page_receipt) };
	ranges[count++] = (struct route_range) { ops, sizeof(*ops) };
	ranges[count++] = (struct route_range) { policy, sizeof(*policy) };
	ranges[count++] = (struct route_range) { binding, sizeof(*binding) };
	if (ops_snapshot->context_size)
		ranges[count++] = (struct route_range) {
			ops_snapshot->context, ops_snapshot->context_size };
	if (policy_snapshot->context_size)
		ranges[count++] = (struct route_range) {
			policy_snapshot->context, policy_snapshot->context_size };
	return ranges_disjoint(ranges, count);
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
	const struct payload_mm_authvar_presence_route_authority_policy *policy)
{
	return policy &&
		policy->revision == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POLICY_REVISION &&
		policy->size == sizeof(*policy) && policy->prepare && policy->commit &&
		policy->abort && policy->dma_protected && policy->fail_stop &&
		policy->context_size <=
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX &&
		!!policy->context == !!policy->context_size;
}

static bool ops_valid(const struct smm_invocation_save_state_ops *ops)
{
	return ops && ops->match_apmc_write && ops->read_rax && ops->write_rax &&
		!!ops->context == !!ops->context_size;
}

static __noreturn void route_fail_stop_untrusted(
	struct payload_mm_authvar_presence_route_session *session)
{
	payload_mm_authvar_presence_transaction_fail_stop_fn fail_stop;
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX];
	size_t context_size;
	uint32_t state;

	if (!session ||
	    session->failure_callback != session->sealed_failure_callback ||
	    session->failure_context_size != session->sealed_failure_context_size ||
	    session->failure_context_size > sizeof(context) ||
	    memcmp(session->failure_context, session->sealed_failure_context,
		session->failure_context_size))
		__builtin_trap();
	fail_stop = session->sealed_failure_callback;
	context_size = session->sealed_failure_context_size;
	if (!fail_stop)
		__builtin_trap();
	if (context_size)
		memcpy(context, session->sealed_failure_context, context_size);
	state = __atomic_load_n(&session->state, __ATOMIC_ACQUIRE);
	while (state != PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POISONED &&
	       state != PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_CLOSED &&
	       !__atomic_compare_exchange_n(&session->state, &state,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POISONED, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		;
	fail_stop(context_size ? context : NULL);
	__builtin_trap();
}

static __noreturn void route_fail_stop_snapshot(
	struct payload_mm_authvar_presence_route_session *session,
	const struct payload_mm_authvar_presence_route_session *trusted)
{
	payload_mm_authvar_presence_transaction_fail_stop_fn fail_stop;
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX];
	size_t context_size;
	uint32_t state;

	if (!session || !trusted ||
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
	state = __atomic_load_n(&session->state, __ATOMIC_ACQUIRE);
	while (state != PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POISONED &&
	       state != PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_CLOSED &&
	       !__atomic_compare_exchange_n(&session->state, &state,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POISONED, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		;
	fail_stop(context_size ? context : NULL);
	__builtin_trap();
}

static bool immutable_valid(
	struct payload_mm_authvar_presence_route_session *session)
{
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_topology topology;
	const struct smm_invocation_evidence *evidence;
	bool valid;

	if (session->identity != (uintptr_t)session || session->owner_attempt != 1U ||
	    session->arm != session->sealed_arm ||
	    session->slot != session->sealed_slot ||
	    session->page != session->sealed_page ||
	    session->evidence != session->sealed_evidence ||
	    session->composition != session->sealed_composition ||
	    session->instance != session->sealed_instance ||
	    session->topology != session->sealed_topology ||
	    memcmp(&session->instance_snapshot,
		&session->sealed_instance_snapshot,
		sizeof(session->instance_snapshot)) ||
	    memcmp(&session->topology_snapshot,
		&session->sealed_topology_snapshot,
		sizeof(session->topology_snapshot)) ||
	    memcmp(&session->binding, &session->sealed_binding,
		sizeof(session->binding)) ||
	    memcmp(&session->callback_context,
		&session->sealed_callback_context,
		sizeof(session->callback_context)) ||
	    session->protected_storage != session->sealed_protected_storage ||
	    session->protected_storage_context !=
		session->sealed_protected_storage_context ||
	    session->failure_callback != session->sealed_failure_callback ||
	    session->failure_context_size != session->sealed_failure_context_size ||
	    session->failure_context_size > sizeof(session->failure_context) ||
	    session->authority_policy.context_size >
		sizeof(session->authority_context) ||
	    session->sealed_authority_policy.context_size >
		sizeof(session->sealed_authority_context) ||
	    memcmp(session->failure_context, session->sealed_failure_context,
		session->failure_context_size) ||
	    memcmp(&session->authority_policy, &session->sealed_authority_policy,
		offsetof(struct payload_mm_authvar_presence_route_authority_policy,
			context)) ||
	    session->authority_policy.context !=
		(session->authority_policy.context_size ?
		 session->authority_context : NULL) ||
	    session->sealed_authority_policy.context !=
		(session->sealed_authority_policy.context_size ?
		 session->sealed_authority_context : NULL) ||
	    session->authority_policy.context_size !=
		session->sealed_authority_policy.context_size ||
	    memcmp(session->authority_context, session->sealed_authority_context,
		session->authority_policy.context_size) ||
	    memcmp(&session->ops, &session->sealed_ops, sizeof(session->ops)))
		return false;
	evidence = smm_invocation_loader_composition_evidence(session->composition,
		session->evidence);
	valid = evidence == session->evidence &&
		smm_invocation_loader_instance_read(session->instance, &instance) ==
			CB_SUCCESS &&
		smm_invocation_topology_read(session->topology, &topology) ==
			CB_SUCCESS &&
		!memcmp(&instance, &session->instance_snapshot, sizeof(instance)) &&
		!memcmp(&topology, &session->topology_snapshot, sizeof(topology)) &&
		session->binding.maximum_cpus == topology.active_cpus &&
		session->binding.initiator_cpu == topology.bsp_cpu &&
		evidence->active_cpus == topology.active_cpus &&
		evidence->bsp_cpu == topology.bsp_cpu &&
		smm_invocation_loader_instance_nonce_equal(
			evidence->loader_instance_nonce,
			instance.loader_instance_nonce) &&
		evidence->loader_lifecycle == instance.lifecycle;
	scrub(&instance, sizeof(instance));
	scrub(&topology, sizeof(topology));
	return valid;
}

static bool active_ticket_valid(
	const struct payload_mm_authvar_presence_route_session *session)
{
	return !memcmp(&session->active_ticket, &session->sealed_active_ticket,
		sizeof(session->active_ticket));
}

static bool ops_context_capture(
	const struct payload_mm_authvar_presence_route_session *session,
	uint8_t snapshot[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX])
{
	if (session->ops.context_size > SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX ||
	    session->ops.context_size != session->sealed_ops.context_size ||
	    session->ops.context != session->sealed_ops.context ||
	    (session->ops.context_size && !object_valid(session->ops.context,
		session->ops.context_size, 1U)))
		return false;
	if (session->ops.context_size)
		memcpy(snapshot, session->ops.context, session->ops.context_size);
	return true;
}

static bool ops_context_unchanged(
	const struct payload_mm_authvar_presence_route_session *session,
	const uint8_t snapshot[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX])
{
	return session->ops.context_size <= SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX &&
		(!session->ops.context_size ||
		 !memcmp(session->ops.context, snapshot,
			session->ops.context_size));
}

static bool protected_exact(
	struct payload_mm_authvar_presence_route_session *session,
	const void *object, size_t size)
{
	struct payload_mm_authvar_presence_route_session before;
	uint8_t ops_context[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX] = { 0 };
	bool protected;

	before = *session;
	if (!ops_context_capture(session, ops_context))
		route_fail_stop_snapshot(session, &before);
	protected = session->protected_storage(session->protected_storage_context,
		object, size);
	if (memcmp(&before, session, sizeof(before)) ||
	    !ops_context_unchanged(session, ops_context))
		route_fail_stop_snapshot(session, &before);
	scrub(ops_context, sizeof(ops_context));
	scrub(&before, sizeof(before));
	return protected;
}

static bool delegated_protected_storage(void *context, const void *object,
	size_t size)
{
	const struct payload_mm_authvar_presence_route_context *callback = context;
	struct payload_mm_authvar_presence_route_session *session;
	struct payload_mm_authvar_presence_route_session before;
	uint8_t ops_context[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX] = { 0 };
	uint32_t state;
	bool protected;

	if (!object_valid(callback, sizeof(*callback), _Alignof(*callback)))
		return false;
	session = callback->session;
	if (!object_valid(session, sizeof(*session), _Alignof(*session)))
		return false;
	state = __atomic_load_n(&session->state, __ATOMIC_ACQUIRE);
	if (callback->identity != (uintptr_t)session ||
	    memcmp(callback, &session->sealed_callback_context,
		sizeof(*callback)) ||
	    (state != PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PREPARE_IDLE &&
	     state != PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DECISION_IDLE &&
	     (state < PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_OWNING ||
	      state > PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DEPARTING)) ||
	    !immutable_valid(session))
		return false;
	before = *session;
	if (!ops_context_capture(session, ops_context))
		route_fail_stop_snapshot(session, &before);
	protected = before.protected_storage(
		before.protected_storage_context, object, size);
	if (memcmp(&before, session, sizeof(before)) ||
	    !ops_context_unchanged(session, ops_context))
		route_fail_stop_snapshot(session, &before);
	scrub(ops_context, sizeof(ops_context));
	scrub(&before, sizeof(before));
	return protected;
}

static bool arm_delegation_bound(
	const struct payload_mm_authvar_presence_route_session *session)
{
	const struct payload_mm_authvar_presence_arm *arm = session->arm;

	return __atomic_load_n(&arm->state, __ATOMIC_ACQUIRE) ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_LOADER_READY &&
		__atomic_load_n(&arm->protection_delegation, __ATOMIC_ACQUIRE) ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PROTECTION_DELEGATION_BOUND &&
		arm->delegated_protected_storage == delegated_protected_storage &&
		arm->sealed_delegated_protected_storage ==
			delegated_protected_storage &&
		arm->delegated_protected_storage_context ==
			&session->callback_context &&
		arm->sealed_delegated_protected_storage_context ==
			&session->callback_context &&
		arm->delegated_protected_storage_context_size ==
			sizeof(session->callback_context) &&
		arm->sealed_delegated_protected_storage_context_size ==
			sizeof(session->callback_context);
}

static struct payload_mm_authvar_presence_route_session *context_session(
	void *context)
{
	const struct payload_mm_authvar_presence_route_context *callback = context;
	struct payload_mm_authvar_presence_route_session *session;

	if (!object_valid(callback, sizeof(*callback), _Alignof(*callback)))
		return NULL;
	session = callback->session;
	if (!object_valid(session, sizeof(*session), _Alignof(*session)))
		return NULL;
	if (callback->identity != (uintptr_t)session ||
	    memcmp(callback, &session->sealed_callback_context,
		sizeof(*callback)))
		return NULL;
	if (!immutable_valid(session) || !active_ticket_valid(session) ||
	    !protected_exact(session, session, sizeof(*session)) ||
	    !protected_exact(session, callback, sizeof(*callback)))
		route_fail_stop_untrusted(session);
	return session;
}

static void *authority_context(
	struct payload_mm_authvar_presence_route_session *session)
{
	return session->authority_policy.context_size ?
		session->authority_context : NULL;
}

static enum cb_err route_prepare(void *context,
	const struct payload_mm_authvar_presence_seed *seed, uint64_t generation)
{
	struct payload_mm_authvar_presence_route_session *session =
		context_session(context);
	struct payload_mm_authvar_presence_route_session before;
	uint8_t ops_context[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX] = { 0 };
	enum cb_err status;

	if (!session)
		return CB_ERR;
	if (__atomic_load_n(&session->state, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_INVOCATION_CLAIMED ||
	    session->round != ROUTE_ROUND_PREPARE ||
	    generation != session->binding.generation)
		route_fail_stop_untrusted(session);
	session->decision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE;
	before = *session;
	if (!ops_context_capture(session, ops_context))
		route_fail_stop_snapshot(session, &before);
	status = session->authority_policy.prepare(authority_context(session), seed,
		generation);
	if (memcmp(&before, session, sizeof(before)) ||
	    !ops_context_unchanged(session, ops_context))
		route_fail_stop_snapshot(session, &before);
	scrub(ops_context, sizeof(ops_context));
	scrub(&before, sizeof(before));
	return status;
}

static enum cb_err route_decide(void *context, uint64_t generation,
	uint32_t decision)
{
	struct payload_mm_authvar_presence_route_session *session =
		context_session(context);
	struct payload_mm_authvar_presence_route_session before;
	uint8_t ops_context[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX] = { 0 };
	enum cb_err status;

	if (!session)
		return CB_ERR;
	if (__atomic_load_n(&session->state, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_INVOCATION_CLAIMED ||
	    generation != session->binding.generation ||
	    (decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT &&
	     session->round != ROUTE_ROUND_DECISION))
		route_fail_stop_untrusted(session);
	session->decision = decision;
	before = *session;
	if (!ops_context_capture(session, ops_context))
		route_fail_stop_snapshot(session, &before);
	status = decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT ?
		session->authority_policy.commit(authority_context(session), generation) :
		session->authority_policy.abort(authority_context(session), generation);
	if (memcmp(&before, session, sizeof(before)) ||
	    !ops_context_unchanged(session, ops_context))
		route_fail_stop_snapshot(session, &before);
	scrub(ops_context, sizeof(ops_context));
	scrub(&before, sizeof(before));
	return status;
}

static enum cb_err route_commit(void *context, uint64_t generation)
{
	return route_decide(context, generation,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
}

static enum cb_err route_abort(void *context, uint64_t generation)
{
	return route_decide(context, generation,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
}

static bool route_dma_protected(void *context, uint64_t base, uint64_t size)
{
	struct payload_mm_authvar_presence_route_session *session =
		context_session(context);
	struct payload_mm_authvar_presence_route_session before;
	uint8_t ops_context[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX] = { 0 };
	bool protected;

	if (!session)
		return false;
	if (__atomic_load_n(&session->state, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_INVOCATION_CLAIMED ||
	    base != (uintptr_t)session->page || size != sizeof(*session->page))
		route_fail_stop_untrusted(session);
	before = *session;
	if (!ops_context_capture(session, ops_context))
		route_fail_stop_snapshot(session, &before);
	protected = session->authority_policy.dma_protected(
		authority_context(session), base, size);
	if (memcmp(&before, session, sizeof(before)) ||
	    !ops_context_unchanged(session, ops_context))
		route_fail_stop_snapshot(session, &before);
	scrub(ops_context, sizeof(ops_context));
	scrub(&before, sizeof(before));
	return protected;
}

static bool invocation_matches(
	const struct payload_mm_authvar_presence_transaction_invocation *first,
	const struct payload_mm_authvar_presence_transaction_invocation *second)
{
	return !memcmp(first, second, sizeof(*first));
}

static enum cb_err route_claim(void *context, uint64_t sentinel,
	struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	struct payload_mm_authvar_presence_route_session *session =
		context_session(context);
	struct payload_mm_authvar_presence_route_session before;
	struct smm_invocation_token token = { 0 };
	uint32_t expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DISPATCHING;

	if (!session || !invocation)
		return CB_ERR;
	memset(invocation, 0, sizeof(*invocation));
	if (sentinel != PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_RAX_SENTINEL ||
	    !__atomic_compare_exchange_n(&session->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_INVOCATION_CLAIMING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		route_fail_stop_untrusted(session);
	before = *session;
	if (smm_invocation_evidence_claim(session->evidence,
		SMM_APMC_AUTHVAR_PRESENCE, sentinel, &session->ops, &token) !=
		CB_SUCCESS) {
		if (memcmp(&before, session, sizeof(before)))
			route_fail_stop_snapshot(session, &before);
		scrub(&before, sizeof(before));
		return CB_ERR;
	}
	if (memcmp(&before, session, sizeof(before)))
		route_fail_stop_snapshot(session, &before);
	scrub(&before, sizeof(before));
	if (token.revision != SMM_INVOCATION_TOKEN_REVISION ||
	    token.size != sizeof(token) || !token.smi_generation ||
	    token.rendezvous_generation != token.smi_generation ||
	    !nonzero(token.rendezvous_digest, sizeof(token.rendezvous_digest)) ||
	    token.initiator_cpu != session->binding.initiator_cpu ||
	    token.active_cpus != session->binding.maximum_cpus || token.bsp != 1U ||
	    token.reserved ||
	    token.smi_generation != session->active_ticket.generation ||
	    (session->round == ROUTE_ROUND_DECISION &&
	     token.smi_generation <= session->prepare_invocation_generation))
		route_fail_stop_untrusted(session);
	*invocation = (struct payload_mm_authvar_presence_transaction_invocation) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_INVOCATION_REVISION,
		.size = sizeof(*invocation),
		.initiator_cpu = token.initiator_cpu,
		.active_cpus = token.active_cpus,
		.smi_generation = token.smi_generation,
		.rendezvous_generation = token.rendezvous_generation,
		.rendezvous_proof = { token.rendezvous_digest[0],
			token.rendezvous_digest[1], token.rendezvous_digest[2] },
		.bsp = token.bsp,
	};
	session->token = token;
	session->invocation = *invocation;
	session->active_invocation_generation = token.smi_generation;
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_INVOCATION_CLAIMING;
	if (!__atomic_compare_exchange_n(&session->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_INVOCATION_CLAIMED, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		route_fail_stop_untrusted(session);
	return CB_SUCCESS;
}

static enum cb_err route_complete(void *context,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	uint64_t value)
{
	struct payload_mm_authvar_presence_route_session *session =
		context_session(context);
	struct payload_mm_authvar_presence_route_session before;
	uint32_t expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_INVOCATION_CLAIMED;
	uint32_t decision;
	enum cb_err status;

	if (!session || !invocation)
		return CB_ERR;
	if (!invocation_matches(invocation, &session->invocation))
		route_fail_stop_untrusted(session);
	if (value == payload_mm_authvar_presence_transaction_rax(&session->binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE))
		decision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE;
	else if (value == payload_mm_authvar_presence_transaction_rax(
		&session->binding, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT))
		decision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT;
	else if (value == payload_mm_authvar_presence_transaction_rax(
		&session->binding, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT))
		decision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT;
	else
		route_fail_stop_untrusted(session);
	if ((session->round == ROUTE_ROUND_PREPARE &&
	     decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT) ||
	    (session->round == ROUTE_ROUND_DECISION &&
	     decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE) ||
	    (session->decision && session->decision != decision))
		route_fail_stop_untrusted(session);
	if (!__atomic_compare_exchange_n(&session->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_COMPLETING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		route_fail_stop_untrusted(session);
	session->decision = decision;
	session->completion_rax = value;
	before = *session;
	status = smm_invocation_evidence_publish_and_request_close(session->evidence,
		&session->token, value, &session->ops);
	if (memcmp(&before, session, sizeof(before)))
		route_fail_stop_snapshot(session, &before);
	scrub(&before, sizeof(before));
	if (status != CB_SUCCESS)
		route_fail_stop_untrusted(session);
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_COMPLETING;
	if (!__atomic_compare_exchange_n(&session->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_EVIDENCE_CLOSING, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		route_fail_stop_untrusted(session);
	return CB_SUCCESS;
}

static __noreturn void route_policy_fail_stop(void *context)
{
	struct payload_mm_authvar_presence_route_session *session =
		context_session(context);

	if (!session)
		__builtin_trap();
	route_fail_stop_untrusted(session);
}

static bool ticket_valid(
	const struct payload_mm_authvar_presence_route_session *session,
	const struct smm_invocation_entry_ticket *ticket)
{
	return ticket && ticket->generation &&
		ticket->max_polls &&
		ticket->max_polls <= SMM_INVOCATION_ENTRY_MAX_POLLS &&
		ticket->command == SMM_APMC_AUTHVAR_PRESENCE &&
		!ticket->reserved[0] && !ticket->reserved[1] &&
		!ticket->reserved[2] &&
		smm_invocation_loader_instance_nonce_equal(
			ticket->loader_instance_nonce,
			session->instance_snapshot.loader_instance_nonce) &&
		ticket->lifecycle == session->instance_snapshot.lifecycle &&
		ticket->cpu < session->topology_snapshot.active_cpus;
}

static bool idle_snapshot_valid(
	const struct payload_mm_authvar_presence_route_session *session,
	uint32_t state)
{
	const bool transients_zero = !session->active_invocation_generation &&
		!session->decision && !session->completion_rax &&
		!nonzero(&session->token, sizeof(session->token)) &&
		!nonzero(&session->invocation, sizeof(session->invocation)) &&
		!nonzero(&session->active_ticket, sizeof(session->active_ticket)) &&
		!nonzero(&session->sealed_active_ticket,
			sizeof(session->sealed_active_ticket));

	return transients_zero &&
		((state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PREPARE_IDLE &&
		  session->round == ROUTE_ROUND_PREPARE &&
		  !session->prepare_invocation_generation) ||
		 (state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DECISION_IDLE &&
		  session->round == ROUTE_ROUND_DECISION &&
		  session->prepare_invocation_generation &&
		  session->prepare_invocation_generation != UINT64_MAX));
}

static bool selection_valid(const struct smm_apmc_selection_receipt *receipt)
{
	const struct smm_apmc_descriptor *selection;

	if (!receipt)
		return false;
	selection = &receipt->descriptor;
	return receipt->revision == SMM_APMC_SELECTION_RECEIPT_REVISION &&
		receipt->size == sizeof(*receipt) &&
		!receipt->reserved &&
		selection->command == SMM_APMC_AUTHVAR_PRESENCE &&
		selection->owner == SMM_APMC_OWNER_AUTHVAR_PRESENCE &&
		selection->role == SMM_APMC_EXCLUSIVE &&
		selection->binding_count == 1U && !selection->observer_count &&
		selection->reserved && selection->enabled;
}

static bool runtime_object_disjoint(
	const struct payload_mm_authvar_presence_route_session *session,
	const void *object, size_t size)
{
	return object_valid(object, size, 1U) &&
		!overlaps(object, size, session, sizeof(*session)) &&
		!overlaps(object, size, session->arm, sizeof(*session->arm)) &&
		!overlaps(object, size, session->slot, sizeof(*session->slot)) &&
		!overlaps(object, size, session->page, sizeof(*session->page)) &&
		!overlaps(object, size, session->evidence, sizeof(*session->evidence)) &&
		!overlaps(object, size, session->composition,
			sizeof(*session->composition)) &&
		!overlaps(object, size, session->instance,
			sizeof(*session->instance)) &&
		!overlaps(object, size, session->topology,
			sizeof(*session->topology)) &&
		(!session->ops.context_size ||
		 !overlaps(object, size, session->ops.context,
			session->ops.context_size));
}

static void terminal_scrub(
	struct payload_mm_authvar_presence_route_session *session)
{
	session->arm = session->sealed_arm = NULL;
	session->slot = session->sealed_slot = NULL;
	session->page = session->sealed_page = NULL;
	session->evidence = session->sealed_evidence = NULL;
	session->composition = session->sealed_composition = NULL;
	session->instance = session->sealed_instance = NULL;
	session->topology = session->sealed_topology = NULL;
	scrub(&session->instance_snapshot, sizeof(session->instance_snapshot));
	scrub(&session->sealed_instance_snapshot,
		sizeof(session->sealed_instance_snapshot));
	scrub(&session->topology_snapshot, sizeof(session->topology_snapshot));
	scrub(&session->sealed_topology_snapshot,
		sizeof(session->sealed_topology_snapshot));
	scrub(&session->binding, sizeof(session->binding));
	scrub(&session->sealed_binding, sizeof(session->sealed_binding));
	scrub(&session->authority_policy, sizeof(session->authority_policy));
	scrub(&session->sealed_authority_policy,
		sizeof(session->sealed_authority_policy));
	scrub(&session->ops, sizeof(session->ops));
	scrub(&session->sealed_ops, sizeof(session->sealed_ops));
	scrub(&session->token, sizeof(session->token));
	scrub(&session->invocation, sizeof(session->invocation));
	scrub(&session->active_ticket, sizeof(session->active_ticket));
	scrub(&session->sealed_active_ticket,
		sizeof(session->sealed_active_ticket));
	scrub(session->authority_context, sizeof(session->authority_context));
	scrub(session->sealed_authority_context,
		sizeof(session->sealed_authority_context));
	scrub(&session->callback_context, sizeof(session->callback_context));
	scrub(&session->sealed_callback_context,
		sizeof(session->sealed_callback_context));
	session->protected_storage = session->sealed_protected_storage = NULL;
	session->protected_storage_context =
		session->sealed_protected_storage_context = NULL;
	session->prepare_invocation_generation = 0;
	session->active_invocation_generation = 0;
	session->completion_rax = 0;
	session->decision = 0;
	session->round = 0;
	session->identity = 0;
	session->owner_attempt = 0;
	session->failure_callback = session->sealed_failure_callback = NULL;
	session->failure_context_size = session->sealed_failure_context_size = 0;
	scrub(session->failure_context, sizeof(session->failure_context));
	scrub(session->sealed_failure_context,
		sizeof(session->sealed_failure_context));
}

/*
 * Keep the large, one-use loader validation images out of the runtime
 * publication call chain.  This helper returns success only after the
 * delegated proof closure is sealed and the enclosing route is ready for the
 * earliest lower-layer callback.
 */
static __noinline enum cb_err route_session_validate_and_bind(
	struct payload_mm_authvar_presence_route_session *session,
	struct payload_mm_authvar_presence_arm *arm,
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_save_state_ops *ops,
	const struct payload_mm_authvar_presence_route_authority_policy
		*authority_policy,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct bootmem_reservation_receipt_authority *page_verifier,
	struct bootmem_reservation_receipt *page_receipt,
	payload_mm_authvar_protected_storage protected_storage,
	void *protected_storage_context)
{
	union {
		struct payload_mm_authvar_presence_arm arm;
		struct payload_mm_authvar_presence_route_session session;
	} phase_image;
	struct payload_mm_authvar_presence_transaction_slot slot_image;
	struct smm_invocation_loader_composition composition_image;
	struct smm_invocation_loader_instance instance_image;
	struct smm_invocation_topology topology_image;
	struct smm_invocation_evidence evidence_image;
	struct smm_invocation_save_state_ops ops_snapshot;
	struct payload_mm_authvar_presence_route_authority_policy policy_snapshot;
	struct payload_mm_authvar_presence_transaction_binding binding_snapshot;
	struct bootmem_reservation_receipt_authority page_verifier_snapshot;
	struct bootmem_reservation_receipt page_receipt_snapshot;
	uint8_t authority_context_snapshot[
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX] = { 0 };
	uint8_t ops_context_snapshot[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX] = { 0 };
	uint32_t expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_EMPTY;
	enum cb_err status;

	if (!object_valid(session, sizeof(*session), _Alignof(*session)) ||
	    !object_valid(arm, sizeof(*arm), _Alignof(*arm)) ||
	    !object_valid(slot, sizeof(*slot), _Alignof(*slot)) ||
	    !object_valid(composition, sizeof(*composition), _Alignof(*composition)) ||
	    !object_valid(instance, sizeof(*instance), _Alignof(*instance)) ||
	    !object_valid(evidence, sizeof(*evidence), _Alignof(*evidence)) ||
	    !object_valid(topology, sizeof(*topology), _Alignof(*topology)) ||
	    !object_valid(ops, sizeof(*ops), _Alignof(*ops)) ||
	    !object_valid(authority_policy, sizeof(*authority_policy),
		_Alignof(*authority_policy)) ||
	    !object_valid(binding, sizeof(*binding), _Alignof(*binding)) ||
	    !object_valid(page_verifier, sizeof(*page_verifier),
		_Alignof(*page_verifier)) ||
	    !object_valid(page_receipt, sizeof(*page_receipt),
		_Alignof(*page_receipt)) || !protected_storage)
		return CB_ERR;
	ops_snapshot = *ops;
	policy_snapshot = *authority_policy;
	binding_snapshot = *binding;
	page_verifier_snapshot = *page_verifier;
	page_receipt_snapshot = *page_receipt;
	if (!binding_valid(&binding_snapshot) || !policy_valid(&policy_snapshot) ||
	    !ops_valid(&ops_snapshot) ||
	    ops_snapshot.context_size > SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX ||
	    (ops_snapshot.context_size &&
	     !object_valid(ops_snapshot.context, ops_snapshot.context_size, 1U)) ||
	    (policy_snapshot.context_size &&
	     !object_valid(policy_snapshot.context, policy_snapshot.context_size,
		1U)) || page_receipt_snapshot.base > UINTPTR_MAX ||
	    page_receipt_snapshot.bytes !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE ||
	    page_receipt_snapshot.base %
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE ||
	    page_receipt_snapshot.generation != binding_snapshot.generation)
		return CB_ERR;
	phase_image.arm = *arm;
	slot_image = *slot;
	composition_image = *composition;
	instance_image = *instance;
	topology_image = *topology;
	evidence_image = *evidence;
	if (policy_snapshot.context_size)
		memcpy(authority_context_snapshot, policy_snapshot.context,
			policy_snapshot.context_size);
	if (ops_snapshot.context_size)
		memcpy(ops_context_snapshot, ops_snapshot.context,
			ops_snapshot.context_size);
	if (!provision_ranges_disjoint(session, arm, slot, composition, instance,
		evidence, topology, ops, &ops_snapshot, authority_policy,
		&policy_snapshot, binding, page_verifier, page_receipt,
		&page_receipt_snapshot))
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&session->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PROVISIONING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	if (nonzero((const uint8_t *)session + sizeof(session->state),
		sizeof(*session) - sizeof(session->state)) ||
	    ++session->owner_attempt == 0U || session->owner_attempt != 1U ||
	    smm_invocation_loader_instance_read(instance,
		&session->instance_snapshot) !=
		CB_SUCCESS ||
	    smm_invocation_topology_read(topology,
		&session->topology_snapshot) != CB_SUCCESS ||
	    memcmp(ops, &ops_snapshot, sizeof(ops_snapshot)) ||
	    memcmp(authority_policy, &policy_snapshot, sizeof(policy_snapshot)) ||
	    memcmp(binding, &binding_snapshot, sizeof(binding_snapshot)) ||
	    memcmp(page_receipt, &page_receipt_snapshot,
		sizeof(page_receipt_snapshot)) ||
	    memcmp(page_verifier, &page_verifier_snapshot,
		sizeof(page_verifier_snapshot)) ||
	    (policy_snapshot.context_size &&
	     memcmp(policy_snapshot.context, authority_context_snapshot,
		policy_snapshot.context_size)) ||
	    binding_snapshot.maximum_cpus !=
		session->topology_snapshot.active_cpus ||
	    binding_snapshot.initiator_cpu != session->topology_snapshot.bsp_cpu ||
	    smm_invocation_loader_composition_evidence(composition, evidence) !=
		evidence || evidence->active_cpus !=
		session->topology_snapshot.active_cpus ||
	    evidence->bsp_cpu != session->topology_snapshot.bsp_cpu ||
	    !smm_invocation_loader_instance_nonce_equal(
		evidence->loader_instance_nonce,
		session->instance_snapshot.loader_instance_nonce) ||
	    evidence->loader_lifecycle != session->instance_snapshot.lifecycle)
		goto reject;
	session->identity = (uintptr_t)session;
	session->arm = session->sealed_arm = arm;
	session->slot = session->sealed_slot = slot;
	session->page = session->sealed_page =
		(void *)(uintptr_t)page_receipt_snapshot.base;
	session->evidence = session->sealed_evidence = evidence;
	session->composition = session->sealed_composition = composition;
	session->instance = session->sealed_instance = instance;
	session->topology = session->sealed_topology = topology;
	session->sealed_instance_snapshot = session->instance_snapshot;
	session->sealed_topology_snapshot = session->topology_snapshot;
	session->binding = session->sealed_binding = binding_snapshot;
	session->authority_policy = session->sealed_authority_policy =
		policy_snapshot;
	if (policy_snapshot.context_size) {
		memcpy(session->authority_context, authority_context_snapshot,
			policy_snapshot.context_size);
		memcpy(session->sealed_authority_context, authority_context_snapshot,
			policy_snapshot.context_size);
	}
	session->authority_policy.context = session->authority_policy.context_size ?
		session->authority_context : NULL;
	session->sealed_authority_policy.context =
		session->sealed_authority_policy.context_size ?
		session->sealed_authority_context : NULL;
	session->ops = session->sealed_ops = ops_snapshot;
	session->protected_storage = session->sealed_protected_storage =
		protected_storage;
	session->protected_storage_context =
		session->sealed_protected_storage_context = protected_storage_context;
	session->failure_callback = session->sealed_failure_callback =
		policy_snapshot.fail_stop;
	session->failure_context_size = session->sealed_failure_context_size =
		policy_snapshot.context_size;
	if (policy_snapshot.context_size) {
		memcpy(session->failure_context, authority_context_snapshot,
			policy_snapshot.context_size);
		memcpy(session->sealed_failure_context, authority_context_snapshot,
			policy_snapshot.context_size);
	}
	session->callback_context = session->sealed_callback_context =
		(struct payload_mm_authvar_presence_route_context) {
			.session = session,
			.identity = (uintptr_t)session,
		};
	session->round = ROUTE_ROUND_PREPARE;
	if (!protected_exact(session, session, sizeof(*session)) ||
	    !protected_exact(session, arm, sizeof(*arm)) ||
	    !protected_exact(session, slot, sizeof(*slot)) ||
	    !protected_exact(session, composition, sizeof(*composition)) ||
	    !protected_exact(session, instance, sizeof(*instance)) ||
	    !protected_exact(session, evidence, sizeof(*evidence)) ||
	    !protected_exact(session, topology, sizeof(*topology)) ||
	    !protected_exact(session, &session->ops, sizeof(session->ops)) ||
	    (session->ops.context_size &&
	     !protected_exact(session, session->ops.context,
		session->ops.context_size)) ||
	    (session->authority_policy.context_size &&
	     !protected_exact(session, session->authority_policy.context,
		session->authority_policy.context_size)) ||
	    !protected_exact(session, page_verifier, sizeof(*page_verifier)) ||
	    !protected_exact(session, page_receipt, sizeof(*page_receipt)) ||
	    !protected_exact(session, (const void *)(uintptr_t)route_prepare, 1U) ||
	    !protected_exact(session, (const void *)(uintptr_t)route_claim, 1U) ||
	    !protected_exact(session, (const void *)(uintptr_t)route_complete, 1U) ||
	    !protected_exact(session, (const void *)(uintptr_t)route_commit, 1U) ||
	    !protected_exact(session, (const void *)(uintptr_t)route_abort, 1U) ||
	    !protected_exact(session,
		(const void *)(uintptr_t)route_dma_protected, 1U) ||
	    !protected_exact(session,
		(const void *)(uintptr_t)route_policy_fail_stop, 1U) ||
	    !protected_exact(session,
		(const void *)(uintptr_t)policy_snapshot.prepare, 1U) ||
	    !protected_exact(session,
		(const void *)(uintptr_t)policy_snapshot.commit, 1U) ||
	    !protected_exact(session,
		(const void *)(uintptr_t)policy_snapshot.abort, 1U) ||
	    !protected_exact(session,
		(const void *)(uintptr_t)policy_snapshot.dma_protected, 1U) ||
	    !protected_exact(session,
		(const void *)(uintptr_t)policy_snapshot.fail_stop, 1U) ||
	    !protected_exact(session,
		(const void *)(uintptr_t)ops_snapshot.match_apmc_write, 1U) ||
	    !protected_exact(session,
		(const void *)(uintptr_t)ops_snapshot.read_rax, 1U) ||
	    !protected_exact(session,
		(const void *)(uintptr_t)ops_snapshot.write_rax, 1U) ||
	    !protected_exact(session,
		(const void *)(uintptr_t)protected_storage, 1U) ||
	    !protected_exact(session, ops, sizeof(*ops)) ||
	    !protected_exact(session, authority_policy,
		sizeof(*authority_policy)) ||
	    !protected_exact(session, binding, sizeof(*binding)) ||
	    !immutable_valid(session) ||
	    memcmp(ops, &ops_snapshot, sizeof(ops_snapshot)) ||
	    memcmp(authority_policy, &policy_snapshot, sizeof(policy_snapshot)) ||
	    memcmp(binding, &binding_snapshot, sizeof(binding_snapshot)) ||
	    memcmp(page_verifier, &page_verifier_snapshot,
		sizeof(page_verifier_snapshot)) ||
	    memcmp(page_receipt, &page_receipt_snapshot,
		sizeof(page_receipt_snapshot)) ||
	    memcmp(arm, &phase_image.arm, sizeof(phase_image.arm)) ||
	    memcmp(slot, &slot_image, sizeof(slot_image)) ||
	    memcmp(composition, &composition_image, sizeof(composition_image)) ||
	    memcmp(instance, &instance_image, sizeof(instance_image)) ||
	    memcmp(topology, &topology_image, sizeof(topology_image)) ||
	    memcmp(evidence, &evidence_image, sizeof(evidence_image)) ||
	    (ops_snapshot.context_size &&
	     memcmp(ops_snapshot.context, ops_context_snapshot,
		ops_snapshot.context_size)) ||
	    (policy_snapshot.context_size &&
	     memcmp(policy_snapshot.context, authority_context_snapshot,
		policy_snapshot.context_size)))
		goto reject;
	phase_image.session = *session;
	status = payload_mm_authvar_presence_arm_protection_delegate_bind(arm,
		protected_storage, protected_storage_context,
		delegated_protected_storage, &session->callback_context,
		sizeof(session->callback_context));
	if (status != CB_SUCCESS || memcmp(&phase_image.session, session,
		sizeof(phase_image.session)) ||
	    !ops_context_unchanged(session, ops_context_snapshot) ||
	    !arm_delegation_bound(session) ||
	    memcmp(slot, &slot_image, sizeof(slot_image)) ||
	    memcmp(composition, &composition_image, sizeof(composition_image)) ||
	    memcmp(instance, &instance_image, sizeof(instance_image)) ||
	    memcmp(topology, &topology_image, sizeof(topology_image)) ||
	    memcmp(evidence, &evidence_image, sizeof(evidence_image)) ||
	    memcmp(ops, &ops_snapshot, sizeof(ops_snapshot)) ||
	    memcmp(authority_policy, &policy_snapshot, sizeof(policy_snapshot)) ||
	    memcmp(binding, &binding_snapshot, sizeof(binding_snapshot)) ||
	    memcmp(page_verifier, &page_verifier_snapshot,
		sizeof(page_verifier_snapshot)) ||
	    memcmp(page_receipt, &page_receipt_snapshot,
		sizeof(page_receipt_snapshot)) ||
	    (policy_snapshot.context_size &&
	     memcmp(policy_snapshot.context, authority_context_snapshot,
		policy_snapshot.context_size)))
		route_fail_stop_snapshot(session, &phase_image.session);
	scrub(&phase_image, sizeof(phase_image));
	scrub(ops_context_snapshot, sizeof(ops_context_snapshot));
	__atomic_store_n(&session->state,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PREPARE_IDLE, __ATOMIC_RELEASE);
	return CB_SUCCESS;
reject:
	scrub(ops_context_snapshot, sizeof(ops_context_snapshot));
	scrub((uint8_t *)session + offsetof(
		struct payload_mm_authvar_presence_route_session, owner_attempt),
		sizeof(*session) - offsetof(
		struct payload_mm_authvar_presence_route_session, owner_attempt));
	__atomic_store_n(&session->state,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POISONED, __ATOMIC_RELEASE);
	return CB_ERR;
}

enum cb_err payload_mm_authvar_presence_route_session_provision(
	struct payload_mm_authvar_presence_route_session *session,
	struct payload_mm_authvar_presence_arm *arm,
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_save_state_ops *ops,
	const struct payload_mm_authvar_presence_route_authority_policy
		*authority_policy,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct bootmem_reservation_receipt_authority *page_verifier,
	struct bootmem_reservation_receipt *page_receipt,
	payload_mm_authvar_protected_storage protected_storage,
	void *protected_storage_context)
{
	const struct payload_mm_authvar_presence_transaction_policy composed_policy = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_POLICY_REVISION,
		.size = sizeof(composed_policy),
		.prepare = route_prepare,
		.commit = route_commit,
		.abort = route_abort,
		.dma_protected = route_dma_protected,
		.claim_invocation = route_claim,
		.complete_invocation = route_complete,
		.fail_stop = route_policy_fail_stop,
		.context = &session->callback_context,
		.context_size = sizeof(session->callback_context),
	};
	enum cb_err status;

	status = route_session_validate_and_bind(session, arm, slot, composition,
		instance, evidence, topology, ops, authority_policy, binding,
		page_verifier, page_receipt, protected_storage,
		protected_storage_context);
	if (status != CB_SUCCESS)
		return status;
	status = payload_mm_authvar_presence_arm_transaction_provision(arm, slot,
		&composed_policy, &session->binding, page_verifier, page_receipt,
		delegated_protected_storage, &session->callback_context);
	if (status == CB_SUCCESS)
		return CB_SUCCESS;
	route_fail_stop_untrusted(session);
}

enum cb_err payload_mm_authvar_presence_route_session_arrive(
	struct payload_mm_authvar_presence_route_session *session,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	uint32_t cpu, uint32_t initial_apic_id,
	struct smm_invocation_entry_ticket *ticket)
{
	struct smm_invocation_entry_cause cause_snapshot;
	struct smm_invocation_entry_policy policy_snapshot;
	uint32_t state;
	enum cb_err status;

	if (!object_valid(session, sizeof(*session), _Alignof(*session)) ||
	    !object_valid(cause, sizeof(*cause), _Alignof(*cause)) ||
	    !object_valid(policy, sizeof(*policy), _Alignof(*policy)) ||
	    !object_valid(ticket, sizeof(*ticket), _Alignof(*ticket)))
		return CB_ERR;
	state = __atomic_load_n(&session->state, __ATOMIC_ACQUIRE);
	if (!immutable_valid(session))
		return CB_ERR;
	if ((state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PREPARE_IDLE ||
	     state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DECISION_IDLE) &&
	    !idle_snapshot_valid(session, state))
		route_fail_stop_untrusted(session);
	if (!idle_snapshot_valid(session, state) ||
	    !runtime_object_disjoint(session, cause, sizeof(*cause)) ||
	    !runtime_object_disjoint(session, policy, sizeof(*policy)) ||
	    !runtime_object_disjoint(session, ticket, sizeof(*ticket)) ||
	    overlaps(cause, sizeof(*cause), policy, sizeof(*policy)) ||
	    overlaps(cause, sizeof(*cause), ticket, sizeof(*ticket)) ||
	    overlaps(policy, sizeof(*policy), ticket, sizeof(*ticket)))
		return CB_ERR;
	cause_snapshot = *cause;
	policy_snapshot = *policy;
	status = smm_invocation_entry_arrive(session->evidence, cause, policy,
		session->instance_snapshot.loader_instance_nonce,
		SMM_APMC_AUTHVAR_PRESENCE, cpu, initial_apic_id, ticket);
	if (memcmp(cause, &cause_snapshot, sizeof(cause_snapshot)) ||
	    memcmp(policy, &policy_snapshot, sizeof(policy_snapshot)))
		route_fail_stop_untrusted(session);
	if (!immutable_valid(session))
		route_fail_stop_untrusted(session);
	if (status != CB_SUCCESS)
		return status;
	if (!ticket_valid(session, ticket))
		route_fail_stop_untrusted(session);
	return CB_SUCCESS;
}

enum smm_apmc_dispatch_result
payload_mm_authvar_presence_route_session_dispatch_locked(
	struct payload_mm_authvar_presence_route_session *session,
	const struct smm_invocation_entry_ticket *ticket,
	struct smm_apmc_selection_receipt *receipt)
{
	struct smm_invocation_entry_ticket ticket_snapshot;
	struct smm_apmc_selection_receipt receipt_snapshot;
	uint32_t expected;
	enum cb_err status;

	if (!object_valid(session, sizeof(*session), _Alignof(*session)) ||
	    !object_valid(ticket, sizeof(*ticket), _Alignof(*ticket)) ||
	    !object_valid(receipt, sizeof(*receipt), _Alignof(*receipt)))
		return SMM_APMC_CONSUMED_REJECT;
	expected = __atomic_load_n(&session->state, __ATOMIC_ACQUIRE);
	if (!immutable_valid(session))
		return SMM_APMC_CONSUMED_REJECT;
	if ((expected == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PREPARE_IDLE ||
	     expected == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DECISION_IDLE) &&
	    !idle_snapshot_valid(session, expected))
		route_fail_stop_untrusted(session);
	if (!idle_snapshot_valid(session, expected) ||
	    !runtime_object_disjoint(session, ticket, sizeof(*ticket)) ||
	    !runtime_object_disjoint(session, receipt, sizeof(*receipt)) ||
	    overlaps(ticket, sizeof(*ticket), receipt, sizeof(*receipt)))
		return SMM_APMC_CONSUMED_REJECT;
	ticket_snapshot = *ticket;
	receipt_snapshot = *receipt;
	if (!ticket_valid(session, &ticket_snapshot) ||
	    ticket_snapshot.cpu != session->topology_snapshot.bsp_cpu ||
	    !selection_valid(&receipt_snapshot) ||
	    receipt_snapshot.identity != (uint64_t)(uintptr_t)receipt ||
	    !__atomic_compare_exchange_n(&session->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_OWNING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return SMM_APMC_CONSUMED_REJECT;
	if (session->active_invocation_generation || session->decision ||
	    session->completion_rax)
		route_fail_stop_untrusted(session);
	session->active_ticket = session->sealed_active_ticket = ticket_snapshot;
	if (!active_ticket_valid(session))
		route_fail_stop_untrusted(session);
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_OWNING;
	if (!__atomic_compare_exchange_n(&session->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DISPATCHING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		route_fail_stop_untrusted(session);
	if (memcmp(ticket, &ticket_snapshot, sizeof(ticket_snapshot)) ||
	    memcmp(receipt, &receipt_snapshot, sizeof(receipt_snapshot)))
		route_fail_stop_untrusted(session);
	if (smm_apmc_command_consume(SMM_APMC_AUTHVAR_PRESENCE,
		SMM_APMC_OWNER_AUTHVAR_PRESENCE, receipt) !=
		SMM_APMC_CONSUMED_SUCCESS)
		route_fail_stop_untrusted(session);
	status = payload_mm_authvar_presence_transaction_dispatch(session->slot);
	if (status != CB_SUCCESS ||
	    __atomic_load_n(&session->state, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_EVIDENCE_CLOSING ||
	    !session->decision || !session->completion_rax)
		route_fail_stop_untrusted(session);
	if (!payload_mm_authvar_presence_transaction_dispatch_ack_valid(
		session->slot, &session->binding, session->decision, session->page))
		route_fail_stop_untrusted(session);
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_EVIDENCE_CLOSING;
	if (!__atomic_compare_exchange_n(&session->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_ACKED, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		route_fail_stop_untrusted(session);
	return SMM_APMC_CONSUMED_SUCCESS;
}

void payload_mm_authvar_presence_route_session_prepare_lock_release(
	struct payload_mm_authvar_presence_route_session *session,
	const struct smm_invocation_entry_ticket *ticket)
{
	struct smm_invocation_entry_ticket ticket_snapshot;
	uint32_t expected = PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_ACKED;

	if (!object_valid(session, sizeof(*session), _Alignof(*session)) ||
	    !object_valid(ticket, sizeof(*ticket), _Alignof(*ticket)))
		__builtin_trap();
	if (!immutable_valid(session) || !active_ticket_valid(session) ||
	    !runtime_object_disjoint(session, ticket, sizeof(*ticket)))
		route_fail_stop_untrusted(session);
	ticket_snapshot = *ticket;
	if (!ticket_valid(session, &ticket_snapshot) ||
	    ticket_snapshot.cpu != session->topology_snapshot.bsp_cpu ||
	    ticket_snapshot.generation != session->active_invocation_generation ||
	    memcmp(&ticket_snapshot, &session->sealed_active_ticket,
		sizeof(ticket_snapshot)) ||
	    memcmp(ticket, &ticket_snapshot, sizeof(ticket_snapshot)) ||
	    !__atomic_compare_exchange_n(&session->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DEPARTING, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		route_fail_stop_untrusted(session);
}

enum payload_mm_authvar_presence_route_departure
payload_mm_authvar_presence_route_session_depart(
	struct payload_mm_authvar_presence_route_session *session,
	const struct smm_invocation_entry_ticket *ticket)
{
	struct payload_mm_authvar_presence_route_session before;
	struct smm_invocation_entry_ticket ticket_snapshot;
	struct smm_invocation_evidence *evidence;
	bool is_bsp;
	uint32_t poll;

	if (!object_valid(session, sizeof(*session), _Alignof(*session)) ||
	    !object_valid(ticket, sizeof(*ticket), _Alignof(*ticket)))
		__builtin_trap();
	if (!immutable_valid(session) || !active_ticket_valid(session) ||
	    !runtime_object_disjoint(session, ticket, sizeof(*ticket)))
		route_fail_stop_untrusted(session);
	ticket_snapshot = *ticket;
	if (!ticket_valid(session, &ticket_snapshot) ||
	    ticket_snapshot.generation != session->active_invocation_generation ||
	    __atomic_load_n(&session->state, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DEPARTING ||
	    ticket_snapshot.max_polls != session->sealed_active_ticket.max_polls ||
	    memcmp(ticket, &ticket_snapshot, sizeof(ticket_snapshot)))
		route_fail_stop_untrusted(session);
	evidence = session->evidence;
	is_bsp = ticket_snapshot.cpu == session->topology_snapshot.bsp_cpu;
	if (is_bsp && memcmp(&ticket_snapshot, &session->sealed_active_ticket,
		sizeof(ticket_snapshot)))
		route_fail_stop_untrusted(session);
	if (smm_invocation_entry_depart(evidence, &ticket_snapshot) != CB_SUCCESS)
		route_fail_stop_untrusted(session);
	if (!is_bsp)
		return PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PARTICIPANT_DEPARTED;
	for (poll = 0; poll < ticket_snapshot.max_polls; ++poll) {
		if (smm_invocation_evidence_phase(evidence) ==
			SMM_INVOCATION_READY)
			break;
		__asm__ __volatile__("pause");
	}
	if (poll == ticket_snapshot.max_polls) {
		(void)smm_invocation_evidence_ticket_fail(evidence,
			ticket_snapshot.generation);
		route_fail_stop_untrusted(session);
	}
	if (!immutable_valid(session))
		route_fail_stop_untrusted(session);
	before = *session;
	if (!smm_invocation_entry_eos_ready(evidence, &ticket_snapshot))
		route_fail_stop_snapshot(session, &before);
	if (memcmp(&before, session, sizeof(before)))
		route_fail_stop_snapshot(session, &before);
	scrub(&before, sizeof(before));
	if (session->decision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE) {
		session->prepare_invocation_generation =
			session->active_invocation_generation;
		session->active_invocation_generation = 0;
		session->completion_rax = 0;
		session->decision = 0;
		scrub(&session->token, sizeof(session->token));
		scrub(&session->invocation, sizeof(session->invocation));
		scrub(&session->active_ticket, sizeof(session->active_ticket));
		scrub(&session->sealed_active_ticket,
			sizeof(session->sealed_active_ticket));
		session->round = ROUTE_ROUND_DECISION;
		__atomic_store_n(&session->state,
			PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DECISION_IDLE,
			__ATOMIC_RELEASE);
	} else {
		terminal_scrub(session);
		__atomic_store_n(&session->state,
			PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_CLOSED,
			__ATOMIC_RELEASE);
		return PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_BSP_EOS_CONSUMED;
	}
	return PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_BSP_EOS_CONSUMED;
}
