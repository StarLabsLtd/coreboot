/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_authvar_presence_lifecycle_close_claims.h"
#include <boot/payload_mm_authvar_presence_lifecycle_close_route.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Authenticated-variable presence lifecycle-close route is SMM-only"
#endif

enum route_state {
	ROUTE_EMPTY,
	ROUTE_PROVISIONING,
	ROUTE_IDLE,
	ROUTE_DISPATCHING,
	ROUTE_CLAIMED,
	ROUTE_CLOSING,
	ROUTE_RESPONDED,
	ROUTE_DEPARTING,
	ROUTE_INTERNAL,
	ROUTE_POISONED,
};

struct route_context {
	struct payload_mm_authvar_presence_lifecycle_close_route *route;
	uintptr_t identity;
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

static bool overlap(const void *first, size_t first_size,
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

struct route_range { const void *base; size_t size; };

static bool ranges_disjoint(const struct route_range *ranges, size_t count)
{
	for (size_t first = 0; first < count; first++) {
		if (!object_valid(ranges[first].base, ranges[first].size, 1U))
			return false;
		for (size_t second = first + 1U; second < count; second++)
			if (overlap(ranges[first].base, ranges[first].size,
				ranges[second].base, ranges[second].size))
				return false;
	}
	return true;
}

static bool callback_protected(payload_mm_authvar_protected_storage proof,
	void *context, const void *callback)
{
	return callback && proof(context, callback, 1U);
}

static bool nonzero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value != 0;
}

static __noreturn void fail_stop(
	struct payload_mm_authvar_presence_lifecycle_close_route *route)
{
	if (object_valid(route, sizeof(*route), _Alignof(*route)))
		__atomic_store_n(&route->state, ROUTE_POISONED, __ATOMIC_RELEASE);
	/* Never indirect through route-resident data on the corruption path. */
	smm_invocation_platform_fail_stop();
	__builtin_trap();
}

static bool protected_exact(
	const struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const void *object, size_t size)
{
	struct payload_mm_authvar_presence_lifecycle_close_route snapshot;
	bool protected;

	if (!route->sealed_protected_storage)
		return false;
	snapshot = *route;
	protected = route->sealed_protected_storage(
		route->sealed_protected_storage_context, object, size);
	if (memcmp(&snapshot, route, sizeof(snapshot)))
		protected = false;
	scrub(&snapshot, sizeof(snapshot));
	return protected;
}

static bool immutable_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_route *route)
{
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_topology topology;
	bool valid;

	if (!route || route->internal.context_size >
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX ||
		route->sealed_internal.context_size >
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX ||
		route->ops.context_size > SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX ||
		route->sealed_ops.context_size >
			SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX)
		return false;
	valid =
		route->generation == route->sealed_generation &&
		route->predecessor_invocation_generation ==
			route->sealed_predecessor_invocation_generation &&
		route->backing_base == route->sealed_backing_base &&
		route->backing_size == route->sealed_backing_size &&
		route->evidence == route->sealed_evidence &&
		route->composition == route->sealed_composition &&
		route->instance == route->sealed_instance &&
		route->topology == route->sealed_topology &&
		route->transaction_slot == route->sealed_transaction_slot &&
		!memcmp(&route->instance_snapshot,
			&route->sealed_instance_snapshot,
			sizeof(route->instance_snapshot)) &&
		!memcmp(&route->topology_snapshot,
			&route->sealed_topology_snapshot,
			sizeof(route->topology_snapshot)) &&
		!memcmp(&route->ops, &route->sealed_ops, sizeof(route->ops)) &&
		!memcmp(&route->internal, &route->sealed_internal,
			offsetof(typeof(route->internal), context)) &&
		route->internal.context_size == route->sealed_internal.context_size &&
		route->internal.context == (route->internal.context_size ?
			(void *)route->internal_context : NULL) &&
		route->sealed_internal.context ==
			(route->sealed_internal.context_size ?
			 (void *)route->sealed_internal_context : NULL) &&
		!memcmp(route->internal_context, route->sealed_internal_context,
			route->internal.context_size) &&
		route->ops.context == (route->ops.context_size ?
			(void *)route->ops_context : NULL) &&
		route->sealed_ops.context == (route->sealed_ops.context_size ?
			(void *)route->sealed_ops_context : NULL) &&
		!memcmp(route->ops_context, route->sealed_ops_context,
			route->ops.context_size) &&
		route->protected_storage == route->sealed_protected_storage &&
		route->protected_storage_context ==
			route->sealed_protected_storage_context &&
		route->last_invocation_generation != UINT64_MAX &&
		route->last_invocation_generation_inverse ==
			~route->last_invocation_generation;
	if (!valid || !protected_exact(route, route, sizeof(*route)) ||
	    smm_invocation_loader_instance_read(route->instance, &instance) !=
		CB_SUCCESS ||
	    smm_invocation_topology_read(route->topology, &topology) != CB_SUCCESS)
		return false;
	valid = !memcmp(&instance, &route->instance_snapshot, sizeof(instance)) &&
		!memcmp(&topology, &route->topology_snapshot, sizeof(topology)) &&
		smm_invocation_loader_composition_evidence(route->composition,
			route->evidence) == route->evidence &&
		route->evidence->active_cpus == topology.active_cpus &&
		route->evidence->bsp_cpu == topology.bsp_cpu &&
		smm_invocation_loader_instance_nonce_equal(
			route->evidence->loader_instance_nonce,
			instance.loader_instance_nonce) &&
		route->evidence->loader_lifecycle == instance.lifecycle &&
		payload_mm_authvar_presence_transaction_dispatch_enabled(
			route->transaction_slot, route->generation);
	scrub(&instance, sizeof(instance));
	scrub(&topology, sizeof(topology));
	return valid;
}

static bool dma_protected(
	struct payload_mm_authvar_presence_lifecycle_close_route *route)
{
	return immutable_valid(route) && route->sealed_internal.dma_protected(
		route->sealed_internal.context, route->backing_base,
		route->backing_size) && immutable_valid(route);
}

/* Internal reset/S3 adapters own invocation evidence independently. */
static bool internal_route_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_route *route)
{
	return route && route->internal.context_size <=
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX &&
		route->sealed_internal.context_size == route->internal.context_size &&
		!memcmp(&route->internal, &route->sealed_internal,
			offsetof(typeof(route->internal), context)) &&
		route->internal.context == (route->internal.context_size ?
			(void *)route->internal_context : NULL) &&
		route->sealed_internal.context ==
			(route->sealed_internal.context_size ?
			 (void *)route->sealed_internal_context : NULL) &&
		!memcmp(route->internal_context, route->sealed_internal_context,
			route->internal.context_size) &&
		route->protected_storage == route->sealed_protected_storage &&
		route->protected_storage_context ==
			route->sealed_protected_storage_context &&
		protected_exact(route, route, sizeof(*route));
}

static struct payload_mm_authvar_presence_lifecycle_close_route *context_route(
	void *context)
{
	const struct route_context *route_context = context;
	struct payload_mm_authvar_presence_lifecycle_close_route *route;

	if (!object_valid(route_context, sizeof(*route_context),
		_Alignof(*route_context)))
		return NULL;
	route = route_context->route;
	if (!object_valid(route, sizeof(*route), _Alignof(*route)) ||
	    route_context->identity != (uintptr_t)route ||
	    !internal_route_valid(route))
		return NULL;
	return route;
}

static uint32_t claim_source(uint64_t claim)
{
	switch (claim) {
	case PAYLOAD_MM_AUTHVAR_PRESENCE_PRE_EXTERNAL_CLAIM:
		return LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PRE_EXTERNAL_IMAGE;
	case PAYLOAD_MM_AUTHVAR_PRESENCE_PAYLOAD_FAILURE_CLAIM:
		return LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PAYLOAD_FAILURE_OR_RETURN;
	case PAYLOAD_MM_AUTHVAR_PRESENCE_CLOSED_REPROOF_CLAIM:
		return LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_REPROOF;
	default:
		return 0;
	}
}

static bool internal_claim(uint64_t claim)
{
	return claim == PAYLOAD_MM_AUTHVAR_PRESENCE_WARM_RESET_CLAIM ||
		claim == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_RESUME_CLAIM;
}

static bool invocation_valid(
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	const struct payload_mm_authvar_presence_lifecycle_close_route *route)
{
	return invocation->revision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_INVOCATION_REVISION &&
		invocation->size == sizeof(*invocation) &&
		invocation->initiator_cpu == route->topology_snapshot.bsp_cpu &&
		invocation->active_cpus == route->topology_snapshot.active_cpus &&
		invocation->smi_generation &&
		invocation->rendezvous_generation == invocation->smi_generation &&
		nonzero(invocation->rendezvous_proof,
			sizeof(invocation->rendezvous_proof)) &&
		invocation->bsp == 1U && !invocation->reserved;
}

static enum cb_err route_claim(void *context, uint64_t claim,
	struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	struct payload_mm_authvar_presence_lifecycle_close_route *route =
		context_route(context);
	struct smm_invocation_token token = { 0 };
	uint32_t source = claim_source(claim);
	uint32_t expected;
	enum cb_err status;

	if (!route || !invocation)
		return CB_ERR;
	memset(invocation, 0, sizeof(*invocation));
	if (source) {
		if (!immutable_valid(route) || !dma_protected(route))
			fail_stop(route);
		expected = ROUTE_DISPATCHING;
		if (route->active_source != source ||
		    !__atomic_compare_exchange_n(&route->state, &expected,
			ROUTE_CLAIMED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			fail_stop(route);
		status = smm_invocation_evidence_claim(route->evidence,
			SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SENTINEL,
			&route->ops, &token);
		if (status != CB_SUCCESS || !immutable_valid(route) ||
		    !dma_protected(route))
			fail_stop(route);
		*invocation =
			(struct payload_mm_authvar_presence_transaction_invocation) {
				.revision =
					PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_INVOCATION_REVISION,
				.size = sizeof(*invocation),
				.initiator_cpu = token.initiator_cpu,
				.active_cpus = token.active_cpus,
				.smi_generation = token.smi_generation,
				.rendezvous_generation = token.rendezvous_generation,
				.rendezvous_proof = { token.rendezvous_digest[0],
					token.rendezvous_digest[1],
					token.rendezvous_digest[2] },
				.bsp = token.bsp,
			};
		if (!invocation_valid(invocation, route) ||
		    token.smi_generation != route->active_ticket.generation ||
		    token.smi_generation <= route->last_invocation_generation)
			fail_stop(route);
		route->token = route->sealed_token = token;
		route->invocation = route->sealed_invocation = *invocation;
		route->last_invocation_generation = token.smi_generation;
		route->last_invocation_generation_inverse = ~token.smi_generation;
		return CB_SUCCESS;
	}
	if (!internal_claim(claim))
		fail_stop(route);
	/* An admitted public round owns the evidence even while route is IDLE. */
	if (smm_invocation_evidence_phase(route->evidence) !=
		SMM_INVOCATION_READY)
		fail_stop(route);
	expected = ROUTE_IDLE;
	if (!__atomic_compare_exchange_n(&route->state, &expected, ROUTE_INTERNAL,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		fail_stop(route);
	status = route->internal.claim(route->internal.context, claim, invocation);
	/* Adapter errors are ambiguous after entry and therefore terminal. */
	if (status != CB_SUCCESS || !internal_route_valid(route))
		fail_stop(route);
	if (!invocation_valid(invocation, route) ||
	    invocation->smi_generation <= route->last_invocation_generation)
		fail_stop(route);
	route->invocation = route->sealed_invocation = *invocation;
	route->last_invocation_generation = invocation->smi_generation;
	route->last_invocation_generation_inverse = ~invocation->smi_generation;
	return CB_SUCCESS;
}

static enum cb_err route_complete(void *context,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	uint64_t value)
{
	struct payload_mm_authvar_presence_lifecycle_close_route *route =
		context_route(context);
	uint32_t state;
	enum cb_err status;

	if (!route || !invocation || !value ||
	    memcmp(invocation, &route->invocation, sizeof(*invocation)) ||
	    memcmp(invocation, &route->sealed_invocation, sizeof(*invocation)) ||
	    memcmp(&route->token, &route->sealed_token, sizeof(route->token)))
		fail_stop(route);
	state = __atomic_load_n(&route->state, __ATOMIC_ACQUIRE);
	if (state == ROUTE_CLAIMED) {
		if (!immutable_valid(route) || !dma_protected(route))
			fail_stop(route);
		status = smm_invocation_evidence_publish_and_request_close(
			route->evidence, &route->token,
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SUCCESS,
			&route->ops);
		if (status != CB_SUCCESS || !immutable_valid(route) ||
		    !dma_protected(route))
			fail_stop(route);
		__atomic_store_n(&route->state, ROUTE_CLOSING, __ATOMIC_RELEASE);
		return CB_SUCCESS;
	}
	if (state != ROUTE_INTERNAL)
		fail_stop(route);
	status = route->internal.complete(route->internal.context, invocation, value);
	if (status != CB_SUCCESS || !internal_route_valid(route))
		fail_stop(route);
	scrub(&route->invocation, sizeof(route->invocation));
	scrub(&route->sealed_invocation, sizeof(route->sealed_invocation));
	__atomic_store_n(&route->state, ROUTE_IDLE, __ATOMIC_RELEASE);
	return CB_SUCCESS;
}

static bool descriptor_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		*descriptor)
{
	return descriptor && descriptor->revision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_REVISION &&
		descriptor->size == sizeof(*descriptor) && descriptor->generation &&
		descriptor->backing_base &&
		descriptor->backing_size ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE &&
		!(descriptor->backing_base &
		  (PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_ALIGNMENT - 1U)) &&
		descriptor->backing_base <= UINTPTR_MAX - descriptor->backing_size &&
		descriptor->backing_base + descriptor->backing_size <= (1ULL << 32) &&
		descriptor->message_size ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE &&
		descriptor->trigger_address == 0xb2U &&
		descriptor->trigger_value ==
			SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE &&
		descriptor->source_mask ==
			LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_SOURCE_MASK &&
		nonzero(descriptor->challenge, sizeof(descriptor->challenge)) &&
		!descriptor->reserved;
}

static bool internal_policy_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_internal_policy
		*policy)
{
	return policy && policy->revision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_POLICY_REVISION &&
		policy->size == sizeof(*policy) && policy->claim && policy->complete &&
		policy->dma_protected &&
		policy->context_size <=
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX &&
		!!policy->context == !!policy->context_size;
}

static bool ticket_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_ticket *ticket);
static bool runtime_object_disjoint(
	const struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const void *object, size_t size);

enum cb_err payload_mm_authvar_presence_lifecycle_close_route_provision(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		*descriptor,
	struct payload_mm_authvar_presence_transaction_slot *transaction_slot,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_save_state_ops *ops,
	const struct payload_mm_authvar_presence_lifecycle_close_internal_policy
		*internal,
	uint64_t predecessor_invocation_generation,
	payload_mm_authvar_protected_storage protected_storage,
	void *protected_storage_context,
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt *receipt)
{
	struct payload_mm_authvar_presence_lifecycle_close_install_descriptor frozen;
	struct payload_mm_authvar_presence_lifecycle_close_policy owner_policy;
	struct smm_invocation_loader_instance instance_snapshot;
	struct smm_invocation_topology topology_snapshot;
	struct smm_invocation_save_state_ops ops_snapshot;
	struct payload_mm_authvar_presence_lifecycle_close_internal_policy
		internal_snapshot;
	uint8_t ops_context[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX] = { 0 };
	uint8_t internal_context[
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX] = { 0 };
	struct route_range ranges[13];
	size_t range_count = 0;
	struct route_context context;
	uint64_t identity;
	uint64_t nonce;
	uint32_t expected = ROUTE_EMPTY;

	if (!object_valid(route, sizeof(*route), _Alignof(*route)) ||
	    !object_valid(descriptor, sizeof(*descriptor), _Alignof(*descriptor)) ||
	    !object_valid(receipt, sizeof(*receipt), _Alignof(*receipt)) ||
	    !object_valid(transaction_slot, sizeof(*transaction_slot),
		_Alignof(*transaction_slot)) ||
	    !object_valid(composition, sizeof(*composition), _Alignof(*composition)) ||
	    !object_valid(instance, sizeof(*instance), _Alignof(*instance)) ||
	    !object_valid(evidence, sizeof(*evidence), _Alignof(*evidence)) ||
	    !object_valid(topology, sizeof(*topology), _Alignof(*topology)) ||
	    !object_valid(ops, sizeof(*ops), _Alignof(*ops)) ||
	    !object_valid(internal, sizeof(*internal), _Alignof(*internal)) ||
	    !protected_storage || !predecessor_invocation_generation)
		return CB_ERR;
	frozen = *descriptor;
	ops_snapshot = *ops;
	internal_snapshot = *internal;
	if (!descriptor_valid(&frozen) ||
	    !internal_policy_valid(&internal_snapshot) ||
	    !ops_snapshot.match_apmc_write || !ops_snapshot.read_value ||
	    !ops_snapshot.write_value ||
	    ops_snapshot.context_size > SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX ||
	    (ops_snapshot.context_size && !object_valid(ops_snapshot.context,
		ops_snapshot.context_size, 1U)) ||
	    (internal_snapshot.context_size &&
	     !object_valid(internal_snapshot.context,
		internal_snapshot.context_size, 1U)))
		return CB_ERR;
	if (ops_snapshot.context_size)
		memcpy(ops_context, ops_snapshot.context, ops_snapshot.context_size);
	if (internal_snapshot.context_size)
		memcpy(internal_context, internal_snapshot.context,
			internal_snapshot.context_size);
	ranges[range_count++] = (struct route_range) { route, sizeof(*route) };
	ranges[range_count++] = (struct route_range) { descriptor,
		sizeof(*descriptor) };
	ranges[range_count++] = (struct route_range) { receipt, sizeof(*receipt) };
	ranges[range_count++] = (struct route_range) { transaction_slot,
		sizeof(*transaction_slot) };
	ranges[range_count++] = (struct route_range) { composition,
		sizeof(*composition) };
	ranges[range_count++] = (struct route_range) { instance, sizeof(*instance) };
	ranges[range_count++] = (struct route_range) { evidence, sizeof(*evidence) };
	ranges[range_count++] = (struct route_range) { topology, sizeof(*topology) };
	ranges[range_count++] = (struct route_range) { ops, sizeof(*ops) };
	ranges[range_count++] = (struct route_range) { internal,
		sizeof(*internal) };
	ranges[range_count++] = (struct route_range) {
		(void *)(uintptr_t)frozen.backing_base, frozen.backing_size };
	if (ops_snapshot.context_size)
		ranges[range_count++] = (struct route_range) { ops_snapshot.context,
			ops_snapshot.context_size };
	if (internal_snapshot.context_size)
		ranges[range_count++] = (struct route_range) { internal_snapshot.context,
			internal_snapshot.context_size };
	if (!ranges_disjoint(ranges, range_count) ||
	    nonzero((const uint8_t *)route + sizeof(route->state),
		sizeof(*route) - sizeof(route->state)) ||
	    memcmp(descriptor, &frozen, sizeof(frozen)) ||
	    memcmp(ops, &ops_snapshot, sizeof(ops_snapshot)) ||
	    memcmp(internal, &internal_snapshot, sizeof(internal_snapshot)) ||
	    (ops_snapshot.context_size && memcmp(ops_snapshot.context, ops_context,
		ops_snapshot.context_size)) ||
	    (internal_snapshot.context_size &&
	     memcmp(internal_snapshot.context, internal_context,
		internal_snapshot.context_size)))
		return CB_ERR;
	memset(receipt, 0, sizeof(*receipt));
	if (!__atomic_compare_exchange_n(&route->state, &expected,
		ROUTE_PROVISIONING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	if (
	    smm_invocation_loader_instance_read(instance, &instance_snapshot) !=
		CB_SUCCESS ||
	    smm_invocation_topology_read(topology, &topology_snapshot) != CB_SUCCESS ||
	    instance_snapshot.lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD ||
	    smm_invocation_loader_composition_evidence(composition, evidence) !=
		evidence ||
	    smm_invocation_evidence_phase(evidence) != SMM_INVOCATION_READY ||
	    evidence->closed_generation != predecessor_invocation_generation ||
	    !evidence->closed_eos_consumed ||
	    evidence->active_cpus != topology_snapshot.active_cpus ||
	    evidence->bsp_cpu != topology_snapshot.bsp_cpu ||
	    !payload_mm_authvar_presence_transaction_dispatch_enabled(
		transaction_slot, frozen.generation) ||
	    !protected_storage(protected_storage_context, route, sizeof(*route)) ||
	    !protected_storage(protected_storage_context, composition,
		sizeof(*composition)) ||
	    !protected_storage(protected_storage_context, instance,
		sizeof(*instance)) ||
	    !protected_storage(protected_storage_context, evidence,
		sizeof(*evidence)) ||
	    !protected_storage(protected_storage_context, topology,
		sizeof(*topology)) ||
	    !protected_storage(protected_storage_context, transaction_slot,
		sizeof(*transaction_slot)) ||
	    !protected_storage(protected_storage_context, ops, sizeof(*ops)) ||
	    !protected_storage(protected_storage_context, internal,
		sizeof(*internal)) ||
	    !callback_protected(protected_storage, protected_storage_context,
		(const void *)(uintptr_t)protected_storage) ||
	    !callback_protected(protected_storage, protected_storage_context,
		(const void *)(uintptr_t)ops_snapshot.match_apmc_write) ||
	    !callback_protected(protected_storage, protected_storage_context,
		(const void *)(uintptr_t)ops_snapshot.read_value) ||
	    !callback_protected(protected_storage, protected_storage_context,
		(const void *)(uintptr_t)ops_snapshot.write_value) ||
	    !callback_protected(protected_storage, protected_storage_context,
		(const void *)(uintptr_t)internal_snapshot.claim) ||
	    !callback_protected(protected_storage, protected_storage_context,
		(const void *)(uintptr_t)internal_snapshot.complete) ||
	    !callback_protected(protected_storage, protected_storage_context,
		(const void *)(uintptr_t)internal_snapshot.dma_protected) ||
	    (ops_snapshot.context_size &&
	     !protected_storage(protected_storage_context, ops_snapshot.context,
		ops_snapshot.context_size)) ||
	    (internal_snapshot.context_size &&
	     !protected_storage(protected_storage_context,
		internal_snapshot.context, internal_snapshot.context_size)) ||
	    !internal_snapshot.dma_protected(internal_snapshot.context,
		frozen.backing_base, frozen.backing_size) ||
	    memcmp(descriptor, &frozen, sizeof(frozen)) ||
	    memcmp(ops, &ops_snapshot, sizeof(ops_snapshot)) ||
	    memcmp(internal, &internal_snapshot, sizeof(internal_snapshot)) ||
	    (ops_snapshot.context_size && memcmp(ops_snapshot.context, ops_context,
		ops_snapshot.context_size)) ||
	    (internal_snapshot.context_size &&
	     memcmp(internal_snapshot.context, internal_context,
		internal_snapshot.context_size)))
		goto reject;
	route->generation = route->sealed_generation = frozen.generation;
	route->predecessor_invocation_generation =
		predecessor_invocation_generation;
	route->sealed_predecessor_invocation_generation =
		predecessor_invocation_generation;
	route->last_invocation_generation = predecessor_invocation_generation;
	route->last_invocation_generation_inverse =
		~predecessor_invocation_generation;
	route->backing_base = frozen.backing_base;
	route->backing_size = frozen.backing_size;
	route->sealed_backing_base = frozen.backing_base;
	route->sealed_backing_size = frozen.backing_size;
	route->evidence = route->sealed_evidence = evidence;
	route->composition = route->sealed_composition = composition;
	route->instance = route->sealed_instance = instance;
	route->topology = route->sealed_topology = topology;
	route->transaction_slot = route->sealed_transaction_slot = transaction_slot;
	route->instance_snapshot = instance_snapshot;
	route->sealed_instance_snapshot = instance_snapshot;
	route->topology_snapshot = topology_snapshot;
	route->sealed_topology_snapshot = topology_snapshot;
	route->ops = route->sealed_ops = ops_snapshot;
	if (ops_snapshot.context_size) {
		memcpy(route->ops_context, ops_context, ops_snapshot.context_size);
		memcpy(route->sealed_ops_context, ops_context,
			ops_snapshot.context_size);
	}
	route->ops.context = ops_snapshot.context_size ? route->ops_context : NULL;
	route->sealed_ops.context = ops_snapshot.context_size ?
		route->sealed_ops_context : NULL;
	route->internal = route->sealed_internal = internal_snapshot;
	if (internal_snapshot.context_size) {
		memcpy(route->internal_context, internal_context,
			internal_snapshot.context_size);
		memcpy(route->sealed_internal_context, internal_context,
			internal_snapshot.context_size);
	}
	route->internal.context = internal_snapshot.context_size ?
		route->internal_context : NULL;
	route->sealed_internal.context = internal_snapshot.context_size ?
		route->sealed_internal_context : NULL;
	route->protected_storage = route->sealed_protected_storage =
		protected_storage;
	route->protected_storage_context =
		route->sealed_protected_storage_context = protected_storage_context;
	context = (struct route_context) { .route = route,
		.identity = (uintptr_t)route };
	owner_policy =
		(struct payload_mm_authvar_presence_lifecycle_close_policy) {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_POLICY_REVISION,
			.size = sizeof(owner_policy),
			.generation = frozen.generation,
			.predecessor_invocation_generation =
				predecessor_invocation_generation,
			.initiator_cpu = topology_snapshot.bsp_cpu,
			.maximum_cpus = topology_snapshot.active_cpus,
			.transaction_slot = transaction_slot,
			.claim_invocation = route_claim,
			.complete_invocation = route_complete,
			.context = &context,
			.context_size = sizeof(context),
		};
	if (memcmp(descriptor, &frozen, sizeof(frozen)) ||
	    __atomic_load_n(&route->state, __ATOMIC_ACQUIRE) != ROUTE_PROVISIONING ||
	    route->active_source || nonzero(&route->token, sizeof(route->token)) ||
	    nonzero(&route->invocation, sizeof(route->invocation)) ||
	    nonzero(&route->active_ticket, sizeof(route->active_ticket)) ||
	    nonzero(&route->sealed_active_ticket,
		 sizeof(route->sealed_active_ticket)) || !immutable_valid(route) ||
	    !dma_protected(route))
		goto reject;
	if (payload_mm_authvar_presence_lifecycle_close_provision(&owner_policy,
		protected_storage, protected_storage_context) != CB_SUCCESS)
		fail_stop(route);
	if (memcmp(descriptor, &frozen, sizeof(frozen)) || !immutable_valid(route) ||
	    !dma_protected(route))
		fail_stop(route);
	identity = frozen.challenge[0] ^ frozen.challenge[2] ^ frozen.generation ^
		0x434c4f5345524f55ULL;
	nonce = frozen.challenge[1] ^ frozen.challenge[3] ^
		instance_snapshot.loader_instance_nonce.low ^
		instance_snapshot.loader_instance_nonce.high;
	if (!identity)
		identity = 1U;
	if (!nonce)
		nonce = 1U;
	*receipt =
		(struct payload_mm_authvar_presence_lifecycle_close_install_receipt) {
			.descriptor = frozen,
			.protected_route_identity = identity,
			.route_nonce = nonce,
			.installed = 1U,
		};
	__atomic_store_n(&route->state, ROUTE_IDLE, __ATOMIC_RELEASE);
	scrub(&owner_policy, sizeof(owner_policy));
	scrub(&context, sizeof(context));
	return CB_SUCCESS;
reject:
	scrub(receipt, sizeof(*receipt));
	/* Every reject is before the owner-live boundary: restore exact EMPTY. */
	scrub(route, sizeof(*route));
	scrub(&owner_policy, sizeof(owner_policy));
	scrub(&context, sizeof(context));
	return CB_ERR;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_route_arrive(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	uint32_t cpu, uint32_t initial_apic_id,
	struct smm_invocation_entry_ticket *ticket)
{
	struct smm_invocation_entry_cause cause_snapshot;
	struct smm_invocation_entry_policy policy_snapshot;
	enum cb_err status;

	if (!route || !cause || !policy || !ticket || !immutable_valid(route) ||
	    __atomic_load_n(&route->state, __ATOMIC_ACQUIRE) !=
		ROUTE_IDLE ||
	    !runtime_object_disjoint(route, cause, sizeof(*cause)) ||
	    !runtime_object_disjoint(route, policy, sizeof(*policy)) ||
	    !runtime_object_disjoint(route, ticket, sizeof(*ticket)) ||
	    overlap(cause, sizeof(*cause), policy, sizeof(*policy)) ||
	    overlap(cause, sizeof(*cause), ticket, sizeof(*ticket)) ||
	    overlap(policy, sizeof(*policy), ticket, sizeof(*ticket)))
		return CB_ERR;
	cause_snapshot = *cause;
	policy_snapshot = *policy;
	status = smm_invocation_entry_arrive(route->evidence, cause, policy,
		route->instance_snapshot.loader_instance_nonce,
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE, cpu, initial_apic_id,
		ticket);
	if (memcmp(cause, &cause_snapshot, sizeof(cause_snapshot)) ||
	    memcmp(policy, &policy_snapshot, sizeof(policy_snapshot)) ||
	    !immutable_valid(route))
		fail_stop(route);
	if (status == CB_SUCCESS && !ticket_valid(route, ticket))
		fail_stop(route);
	return status;
}

static bool ticket_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_ticket *ticket)
{
	return ticket && ticket->generation &&
		ticket->command ==
			SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE &&
		ticket->max_polls &&
		ticket->max_polls <= SMM_INVOCATION_ENTRY_MAX_POLLS &&
		ticket->cpu < route->topology_snapshot.active_cpus &&
		ticket->lifecycle == route->instance_snapshot.lifecycle &&
		smm_invocation_loader_instance_nonce_equal(
			ticket->loader_instance_nonce,
			route->instance_snapshot.loader_instance_nonce) &&
		!ticket->reserved[0] && !ticket->reserved[1] &&
		!ticket->reserved[2];
}

static bool ticket_fresh(
	const struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_ticket *ticket)
{
	return ticket_valid(route, ticket) &&
		ticket->generation > route->last_invocation_generation;
}

static bool active_ticket_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_ticket *ticket)
{
	return ticket_valid(route, ticket) &&
		ticket->generation == route->last_invocation_generation &&
		!memcmp(ticket, &route->active_ticket, sizeof(*ticket)) &&
		!memcmp(ticket, &route->sealed_active_ticket, sizeof(*ticket));
}

static bool runtime_object_disjoint(
	const struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const void *object, size_t size)
{
	return object_valid(object, size, 1U) &&
		!overlap(object, size, route, sizeof(*route)) &&
		!overlap(object, size, (void *)(uintptr_t)route->backing_base,
			route->backing_size) &&
		!overlap(object, size, route->evidence, sizeof(*route->evidence)) &&
		!overlap(object, size, route->composition,
			sizeof(*route->composition)) &&
		!overlap(object, size, route->instance, sizeof(*route->instance)) &&
		!overlap(object, size, route->topology, sizeof(*route->topology)) &&
		!overlap(object, size, route->transaction_slot,
			sizeof(*route->transaction_slot));
}

static bool selection_valid(const struct smm_apmc_selection_receipt *selection)
{
	return selection &&
		selection->revision == SMM_APMC_SELECTION_RECEIPT_REVISION &&
		selection->size == sizeof(*selection) && selection->identity &&
		selection->generation && !selection->reserved &&
		selection->descriptor.command ==
			SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE &&
		selection->descriptor.owner ==
			SMM_APMC_OWNER_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE &&
		selection->descriptor.role == SMM_APMC_EXCLUSIVE &&
		selection->descriptor.binding_count == 1U &&
		!selection->descriptor.observer_count &&
		selection->descriptor.reserved && selection->descriptor.enabled;
}

static enum cb_err dispatch_source(uint32_t source)
{
	switch (source) {
	case LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PRE_EXTERNAL_IMAGE:
		return payload_mm_authvar_presence_pre_external_image_close();
	case LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PAYLOAD_FAILURE_OR_RETURN:
		return payload_mm_authvar_presence_payload_failure_or_return_close();
	case LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_REPROOF:
		return payload_mm_authvar_presence_closed_reproof();
	default:
		return CB_ERR;
	}
}

enum smm_apmc_dispatch_result
payload_mm_authvar_presence_lifecycle_close_route_dispatch_locked(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_ticket *ticket,
	struct smm_apmc_selection_receipt *selection)
{
	struct payload_mm_authvar_presence_lifecycle_close_message request;
	struct payload_mm_authvar_presence_lifecycle_close_message observed;
	struct payload_mm_authvar_presence_lifecycle_close_message response;
	struct smm_invocation_entry_ticket ticket_snapshot;
	struct smm_apmc_selection_receipt selection_snapshot;
	struct payload_mm_authvar_presence_lifecycle_close_message *message;
	struct lb_authvar_presence_lifecycle_close_endpoint endpoint = {
		.tag = LB_TAG_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT,
		.size = sizeof(endpoint),
		.revision = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT_REVISION,
		.header_size = sizeof(endpoint),
		.flags = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_REQUIRED_FLAGS,
		.transport = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT_APM_IO8,
		.trigger_width = 1U,
		.trigger_address = 0xb2U,
		.trigger_value = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		.source_mask = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_SOURCE_MASK,
	};
	uint32_t expected = ROUTE_IDLE;

	if (!route || !ticket || !selection || !immutable_valid(route) ||
	    !dma_protected(route) ||
	    !runtime_object_disjoint(route, ticket, sizeof(*ticket)) ||
	    !runtime_object_disjoint(route, selection, sizeof(*selection)) ||
	    overlap(ticket, sizeof(*ticket), selection, sizeof(*selection)))
		return SMM_APMC_CONSUMED_REJECT;
	ticket_snapshot = *ticket;
	selection_snapshot = *selection;
	if (!ticket_fresh(route, &ticket_snapshot) ||
	    ticket_snapshot.cpu != route->topology_snapshot.bsp_cpu ||
	    !selection_valid(&selection_snapshot) ||
	    selection_snapshot.identity != (uint64_t)(uintptr_t)selection ||
	    memcmp(ticket, &ticket_snapshot, sizeof(ticket_snapshot)) ||
	    memcmp(selection, &selection_snapshot, sizeof(selection_snapshot)) ||
	    !__atomic_compare_exchange_n(&route->state, &expected,
		ROUTE_DISPATCHING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return SMM_APMC_CONSUMED_REJECT;
	if (memcmp(ticket, &ticket_snapshot, sizeof(ticket_snapshot)) ||
	    memcmp(selection, &selection_snapshot, sizeof(selection_snapshot)))
		fail_stop(route);
	if (smm_apmc_command_consume(
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		SMM_APMC_OWNER_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE, selection) !=
		SMM_APMC_CONSUMED_SUCCESS)
		fail_stop(route);
	message = (void *)(uintptr_t)route->backing_base;
	endpoint.generation = route->generation;
	endpoint.communication_base = route->backing_base;
	endpoint.communication_size =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE;
	endpoint.message_size =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE;
	if (__atomic_load_n(&message->completion, __ATOMIC_ACQUIRE) !=
	    PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING)
		fail_stop(route);
	request = *message;
	observed = *message;
	if (__atomic_load_n(&message->completion, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING ||
	    memcmp(&request, &observed, sizeof(request)) ||
	    payload_mm_authvar_presence_lifecycle_close_request_validate(&endpoint,
		&request, sizeof(request)) != CB_SUCCESS)
		fail_stop(route);
	route->active_source = request.source;
	route->active_ticket = route->sealed_active_ticket = ticket_snapshot;
	if (!dma_protected(route))
		fail_stop(route);
	if (dispatch_source(request.source) != CB_SUCCESS ||
	    __atomic_load_n(&route->state, __ATOMIC_ACQUIRE) != ROUTE_CLOSING ||
	    memcmp(message, &request, sizeof(request)) || !dma_protected(route))
		fail_stop(route);
	response = request;
	response.status =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SUCCESS;
	response.completion = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING;
	*message = response;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	if (memcmp(message, &response, sizeof(response)) ||
	    !immutable_valid(route) || !dma_protected(route))
		fail_stop(route);
	__atomic_store_n(&message->completion,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPLETE,
		__ATOMIC_RELEASE);
	__atomic_store_n(&route->state, ROUTE_RESPONDED, __ATOMIC_RELEASE);
	return SMM_APMC_CONSUMED_SUCCESS;
}

void payload_mm_authvar_presence_lifecycle_close_route_prepare_lock_release(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_ticket *ticket)
{
	struct smm_invocation_entry_ticket ticket_snapshot;
	uint32_t expected = ROUTE_RESPONDED;

	if (!route || !ticket || !immutable_valid(route) ||
	    !runtime_object_disjoint(route, ticket, sizeof(*ticket)))
		fail_stop(route);
	ticket_snapshot = *ticket;
	if (!active_ticket_valid(route, &ticket_snapshot) ||
	    ticket_snapshot.cpu != route->topology_snapshot.bsp_cpu ||
	    memcmp(ticket, &ticket_snapshot, sizeof(ticket_snapshot)) ||
	    !__atomic_compare_exchange_n(&route->state, &expected,
		ROUTE_DEPARTING, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		fail_stop(route);
}

enum payload_mm_authvar_presence_lifecycle_close_route_departure
payload_mm_authvar_presence_lifecycle_close_route_depart(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_ticket *ticket)
{
	struct smm_invocation_entry_ticket ticket_snapshot;
	bool bsp;

	if (!route || !ticket || !immutable_valid(route) ||
	    !runtime_object_disjoint(route, ticket, sizeof(*ticket)))
		fail_stop(route);
	ticket_snapshot = *ticket;
	bsp = ticket_snapshot.cpu == route->topology_snapshot.bsp_cpu;
	if (!ticket_valid(route, &ticket_snapshot) ||
	    ticket_snapshot.generation != route->last_invocation_generation ||
	    (bsp && !active_ticket_valid(route, &ticket_snapshot)) ||
	    memcmp(ticket, &ticket_snapshot, sizeof(ticket_snapshot)) ||
	    __atomic_load_n(&route->state, __ATOMIC_ACQUIRE) != ROUTE_DEPARTING ||
	    smm_invocation_entry_depart(route->evidence, &ticket_snapshot) !=
		CB_SUCCESS)
		fail_stop(route);
	if (!bsp)
		return
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_PARTICIPANT_DEPARTED;
	for (uint32_t poll = 0; poll < ticket_snapshot.max_polls; poll++) {
		if (smm_invocation_evidence_phase(route->evidence) ==
			SMM_INVOCATION_READY)
			break;
		if (poll + 1U == ticket_snapshot.max_polls)
			fail_stop(route);
		__asm__ __volatile__("pause");
	}
	if (!smm_invocation_entry_eos_ready(route->evidence, &ticket_snapshot))
		fail_stop(route);
	route->active_source = 0;
	scrub(&route->token, sizeof(route->token));
	scrub(&route->sealed_token, sizeof(route->sealed_token));
	scrub(&route->invocation, sizeof(route->invocation));
	scrub(&route->sealed_invocation, sizeof(route->sealed_invocation));
	scrub(&route->active_ticket, sizeof(route->active_ticket));
	scrub(&route->sealed_active_ticket, sizeof(route->sealed_active_ticket));
	__atomic_store_n(&route->state, ROUTE_IDLE, __ATOMIC_RELEASE);
	return PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_BSP_EOS_CONSUMED;
}
