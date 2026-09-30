/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_authority.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_s3_route.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Authenticated-variable presence lifecycle-close S3 route is SMM-only"
#endif

enum s3_route_state {
	S3_ROUTE_EMPTY,
	S3_ROUTE_PROVISIONING,
	S3_ROUTE_IDLE,
	S3_ROUTE_DISPATCHING,
	S3_ROUTE_RESPONDED,
	S3_ROUTE_DEPARTING,
	S3_ROUTE_POISONED,
};

struct route_range { const void *base; size_t size; };

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool nonzero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value != 0;
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

static __noreturn void fail_stop(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route)
{
	if (object_valid(route, sizeof(*route), _Alignof(*route)))
		__atomic_store_n(&route->state, S3_ROUTE_POISONED, __ATOMIC_RELEASE);
	smm_invocation_platform_fail_stop();
	__builtin_trap();
}

static bool protected_exact(
	const struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const void *object, size_t size)
{
	uint8_t before[sizeof(*route) - offsetof(
		struct payload_mm_authvar_presence_lifecycle_close_s3_route, reserved)];
	const uint8_t *tail = (const void *)&route->reserved;
	uint32_t state;
	bool protected;

	if (!route->sealed_protected_storage)
		return false;
	state = __atomic_load_n(&route->state, __ATOMIC_ACQUIRE);
	memcpy(before, tail, sizeof(before));
	protected = route->sealed_protected_storage(
		route->sealed_protected_storage_context, object, size);
	if (state != __atomic_load_n(&route->state, __ATOMIC_ACQUIRE) ||
	    memcmp(before, tail, sizeof(before)))
		protected = false;
	scrub(before, sizeof(before));
	return protected;
}

static bool immutable_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_s3_route *route)
{
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_topology topology;
	bool valid;

	if (!route || route->reserved ||
	    route->dma_context_size > sizeof(route->dma_context) ||
	    route->sealed_dma_context_size != route->dma_context_size)
		return false;
	valid =
		!memcmp(&route->endpoint, &route->sealed_endpoint,
			sizeof(route->endpoint)) &&
		route->backing_base == route->sealed_backing_base &&
		route->backing_size == route->sealed_backing_size &&
		route->backing_tag == route->sealed_backing_tag &&
		route->composition == route->sealed_composition &&
		route->instance == route->sealed_instance &&
		route->evidence == route->sealed_evidence &&
		route->topology == route->sealed_topology &&
		!memcmp(&route->instance_snapshot,
			&route->sealed_instance_snapshot,
			sizeof(route->instance_snapshot)) &&
		!memcmp(&route->topology_snapshot,
			&route->sealed_topology_snapshot,
			sizeof(route->topology_snapshot)) &&
		!memcmp(&route->ops, &route->sealed_ops, sizeof(route->ops)) &&
		route->ops.context == (route->ops.context_size ?
			(void *)route->ops_context : NULL) &&
		route->sealed_ops.context == (route->sealed_ops.context_size ?
			(void *)route->sealed_ops_context : NULL) &&
		route->ops.context_size == route->sealed_ops.context_size &&
		route->ops.context_size <= sizeof(route->ops_context) &&
		!memcmp(route->ops_context, route->sealed_ops_context,
			route->ops.context_size) &&
		route->dma_protected == route->sealed_dma_protected &&
		route->dma_protected &&
		!memcmp(route->dma_context, route->sealed_dma_context,
			route->dma_context_size) &&
		route->protected_storage == route->sealed_protected_storage &&
		route->protected_storage_context ==
			route->sealed_protected_storage_context &&
		route->last_invocation_generation != UINT64_MAX &&
		route->last_invocation_generation_inverse ==
			~route->last_invocation_generation &&
		payload_mm_authvar_presence_lifecycle_close_endpoint_validate(
			&route->endpoint) == CB_SUCCESS &&
		route->endpoint.communication_base == route->backing_base &&
		route->endpoint.communication_size ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE &&
		route->backing_size ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE &&
		route->backing_tag != 0;
	if (!valid || !protected_exact(route, route, sizeof(*route)) ||
	    !protected_exact(route, route->composition,
		sizeof(*route->composition)) ||
	    !protected_exact(route, route->instance, sizeof(*route->instance)) ||
	    !protected_exact(route, route->evidence, sizeof(*route->evidence)) ||
	    !protected_exact(route, route->topology, sizeof(*route->topology)) ||
	    smm_invocation_loader_instance_read(route->instance, &instance) !=
		CB_SUCCESS ||
	    smm_invocation_topology_read(route->topology, &topology) != CB_SUCCESS)
		return false;
	valid = !memcmp(&instance, &route->instance_snapshot, sizeof(instance)) &&
		!memcmp(&topology, &route->topology_snapshot, sizeof(topology)) &&
		instance.lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD &&
		smm_invocation_loader_composition_evidence(route->composition,
			route->evidence) == route->evidence &&
		route->evidence->active_cpus == topology.active_cpus &&
		route->evidence->bsp_cpu == topology.bsp_cpu &&
		route->evidence->closed_generation &&
		route->evidence->closed_generation <=
			route->last_invocation_generation &&
		route->evidence->closed_eos_consumed &&
		smm_invocation_loader_instance_nonce_equal(
			route->evidence->loader_instance_nonce,
			instance.loader_instance_nonce) &&
		smm_invocation_loader_instance_nonce_equal(
			route->evidence->closed_loader_instance_nonce,
			instance.loader_instance_nonce) &&
		route->evidence->loader_lifecycle == instance.lifecycle &&
		route->evidence->closed_lifecycle == instance.lifecycle;
	scrub(&instance, sizeof(instance));
	scrub(&topology, sizeof(topology));
	return valid;
}

static bool dma_valid(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route)
{
	return immutable_valid(route) && route->sealed_dma_protected(
		route->dma_context_size ? route->sealed_dma_context : NULL,
		route->backing_base, route->backing_size) && immutable_valid(route);
}

static bool policy_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_s3_policy *policy)
{
	return policy && policy->revision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_POLICY_REVISION &&
		policy->size == sizeof(*policy) && policy->backing_base &&
		policy->backing_size ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE &&
		!(policy->backing_base &
		  (PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_ALIGNMENT - 1U)) &&
		policy->backing_base <= UINTPTR_MAX - policy->backing_size &&
		policy->backing_base + policy->backing_size <= (1ULL << 32) &&
		policy->backing_tag && !policy->reserved &&
		policy->dram_provenance && policy->dma_protected &&
		policy->dram_provenance_context_size <=
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_CONTEXT_MAX &&
		policy->dma_context_size <=
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_CONTEXT_MAX &&
		!!policy->dram_provenance_context ==
			!!policy->dram_provenance_context_size &&
		!!policy->dma_context == !!policy->dma_context_size;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_s3_route_provision(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_save_state_ops *ops,
	const struct payload_mm_authvar_presence_lifecycle_close_s3_policy *policy,
	payload_mm_authvar_protected_storage protected_storage,
	void *protected_storage_context)
{
	struct lb_authvar_presence_lifecycle_close_endpoint endpoint_snapshot;
	struct smm_invocation_loader_instance instance_snapshot;
	struct smm_invocation_topology topology_snapshot;
	struct smm_invocation_save_state_ops ops_snapshot;
	struct payload_mm_authvar_presence_lifecycle_close_s3_policy proof;
	uint8_t provenance_context[
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_CONTEXT_MAX] = { 0 };
	uint8_t dma_context[
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_CONTEXT_MAX] = { 0 };
	uint8_t ops_context[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX] = { 0 };
	struct route_range ranges[12];
	size_t range_count = 0;
	uint32_t expected = S3_ROUTE_EMPTY;
	enum cb_err status = CB_ERR;

	if (!object_valid(route, sizeof(*route), _Alignof(*route)) ||
	    !object_valid(endpoint, sizeof(*endpoint), 1U) ||
	    !object_valid(composition, sizeof(*composition), _Alignof(*composition)) ||
	    !object_valid(instance, sizeof(*instance), _Alignof(*instance)) ||
	    !object_valid(evidence, sizeof(*evidence), _Alignof(*evidence)) ||
	    !object_valid(topology, sizeof(*topology), _Alignof(*topology)) ||
	    !object_valid(ops, sizeof(*ops), _Alignof(*ops)) ||
	    !object_valid(policy, sizeof(*policy), _Alignof(*policy)) ||
	    !protected_storage)
		return CB_ERR;
	endpoint_snapshot = *endpoint;
	ops_snapshot = *ops;
	proof = *policy;
	if (payload_mm_authvar_presence_lifecycle_close_endpoint_validate(
		&endpoint_snapshot) != CB_SUCCESS || !policy_valid(&proof) ||
	    endpoint_snapshot.communication_base != proof.backing_base ||
	    !ops_snapshot.match_apmc_write || !ops_snapshot.read_value ||
	    !ops_snapshot.write_value ||
	    ops_snapshot.context_size > sizeof(ops_context) ||
	    !!ops_snapshot.context != !!ops_snapshot.context_size)
		goto out;
	if (proof.dram_provenance_context_size)
		memcpy(provenance_context, proof.dram_provenance_context,
			proof.dram_provenance_context_size);
	if (proof.dma_context_size)
		memcpy(dma_context, proof.dma_context, proof.dma_context_size);
	if (ops_snapshot.context_size)
		memcpy(ops_context, ops_snapshot.context, ops_snapshot.context_size);
	ranges[range_count++] = (struct route_range) { route, sizeof(*route) };
	ranges[range_count++] = (struct route_range) { endpoint, sizeof(*endpoint) };
	ranges[range_count++] = (struct route_range) { composition,
		sizeof(*composition) };
	ranges[range_count++] = (struct route_range) { instance, sizeof(*instance) };
	ranges[range_count++] = (struct route_range) { evidence, sizeof(*evidence) };
	ranges[range_count++] = (struct route_range) { topology, sizeof(*topology) };
	ranges[range_count++] = (struct route_range) { ops, sizeof(*ops) };
	ranges[range_count++] = (struct route_range) { policy, sizeof(*policy) };
	ranges[range_count++] = (struct route_range) {
		(void *)(uintptr_t)proof.backing_base, (size_t)proof.backing_size };
	if (ops_snapshot.context_size)
		ranges[range_count++] = (struct route_range) { ops_snapshot.context,
			ops_snapshot.context_size };
	if (proof.dram_provenance_context_size)
		ranges[range_count++] = (struct route_range) {
			proof.dram_provenance_context,
			proof.dram_provenance_context_size };
	if (proof.dma_context_size)
		ranges[range_count++] = (struct route_range) { proof.dma_context,
			proof.dma_context_size };
	if (!ranges_disjoint(ranges, range_count) ||
	    memcmp(endpoint, &endpoint_snapshot, sizeof(endpoint_snapshot)) ||
	    memcmp(ops, &ops_snapshot, sizeof(ops_snapshot)) ||
	    memcmp(policy, &proof, sizeof(proof)) ||
	    !__atomic_compare_exchange_n(&route->state, &expected,
		S3_ROUTE_PROVISIONING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto out;
	if (nonzero((const uint8_t *)route + sizeof(route->state),
		sizeof(*route) - sizeof(route->state)) ||
	    smm_invocation_loader_instance_read(instance, &instance_snapshot) !=
		CB_SUCCESS ||
	    smm_invocation_topology_read(topology, &topology_snapshot) != CB_SUCCESS ||
	    instance_snapshot.lifecycle != SMM_INVOCATION_LOADER_S3_RELOAD ||
	    smm_invocation_loader_composition_evidence(composition, evidence) !=
		evidence ||
	    smm_invocation_evidence_phase(evidence) != SMM_INVOCATION_READY ||
	    !evidence->closed_generation ||
	    evidence->closed_generation == UINT64_MAX ||
	    !evidence->closed_eos_consumed ||
	    evidence->active_cpus != topology_snapshot.active_cpus ||
	    evidence->bsp_cpu != topology_snapshot.bsp_cpu ||
	    !smm_invocation_loader_instance_nonce_equal(
		evidence->loader_instance_nonce,
		instance_snapshot.loader_instance_nonce) ||
	    !smm_invocation_loader_instance_nonce_equal(
		evidence->closed_loader_instance_nonce,
		instance_snapshot.loader_instance_nonce) ||
	    evidence->loader_lifecycle != instance_snapshot.lifecycle ||
	    evidence->closed_lifecycle != instance_snapshot.lifecycle ||
	    !protected_storage(protected_storage_context, route, sizeof(*route)) ||
	    !protected_storage(protected_storage_context, endpoint,
		sizeof(*endpoint)) ||
	    !protected_storage(protected_storage_context, composition,
		sizeof(*composition)) ||
	    !protected_storage(protected_storage_context, instance,
		sizeof(*instance)) ||
	    !protected_storage(protected_storage_context, evidence,
		sizeof(*evidence)) ||
	    !protected_storage(protected_storage_context, topology,
		sizeof(*topology)) ||
	    !protected_storage(protected_storage_context, ops, sizeof(*ops)) ||
	    !protected_storage(protected_storage_context, policy, sizeof(*policy)) ||
	    protected_storage(protected_storage_context,
		(const void *)(uintptr_t)proof.backing_base,
		(size_t)proof.backing_size) ||
	    !callback_protected(protected_storage, protected_storage_context,
		(const void *)(uintptr_t)protected_storage) ||
	    !callback_protected(protected_storage, protected_storage_context,
		(const void *)(uintptr_t)ops_snapshot.match_apmc_write) ||
	    !callback_protected(protected_storage, protected_storage_context,
		(const void *)(uintptr_t)ops_snapshot.read_value) ||
	    !callback_protected(protected_storage, protected_storage_context,
		(const void *)(uintptr_t)ops_snapshot.write_value) ||
	    !callback_protected(protected_storage, protected_storage_context,
		(const void *)(uintptr_t)proof.dram_provenance) ||
	    !callback_protected(protected_storage, protected_storage_context,
		(const void *)(uintptr_t)proof.dma_protected) ||
	    (ops_snapshot.context_size &&
	     !protected_storage(protected_storage_context, ops_snapshot.context,
		ops_snapshot.context_size)) ||
	    (proof.dram_provenance_context_size &&
	     !protected_storage(protected_storage_context,
		proof.dram_provenance_context,
		proof.dram_provenance_context_size)) ||
	    (proof.dma_context_size &&
	     !protected_storage(protected_storage_context, proof.dma_context,
		proof.dma_context_size)) ||
	    !proof.dram_provenance(proof.dram_provenance_context_size ?
		provenance_context : NULL, proof.backing_base, proof.backing_size) ||
	    !proof.dma_protected(proof.dma_context_size ? dma_context : NULL,
		proof.backing_base, proof.backing_size) ||
	    memcmp(endpoint, &endpoint_snapshot, sizeof(endpoint_snapshot)) ||
	    memcmp(ops, &ops_snapshot, sizeof(ops_snapshot)) ||
	    memcmp(policy, &proof, sizeof(proof)) ||
	    (ops_snapshot.context_size &&
	     memcmp(ops_snapshot.context, ops_context,
		ops_snapshot.context_size)) ||
	    (proof.dram_provenance_context_size &&
	     memcmp(proof.dram_provenance_context, provenance_context,
		proof.dram_provenance_context_size)) ||
	    (proof.dma_context_size &&
	     memcmp(proof.dma_context, dma_context, proof.dma_context_size)))
		goto reject;

	route->endpoint = route->sealed_endpoint = endpoint_snapshot;
	route->backing_base = route->sealed_backing_base = proof.backing_base;
	route->backing_size = route->sealed_backing_size = proof.backing_size;
	route->backing_tag = route->sealed_backing_tag = proof.backing_tag;
	route->composition = route->sealed_composition = composition;
	route->instance = route->sealed_instance = instance;
	route->evidence = route->sealed_evidence = evidence;
	route->topology = route->sealed_topology = topology;
	route->instance_snapshot = route->sealed_instance_snapshot =
		instance_snapshot;
	route->topology_snapshot = route->sealed_topology_snapshot =
		topology_snapshot;
	route->ops = route->sealed_ops = ops_snapshot;
	if (ops_snapshot.context_size) {
		memcpy(route->ops_context, ops_context, ops_snapshot.context_size);
		memcpy(route->sealed_ops_context, ops_context,
			ops_snapshot.context_size);
	}
	route->ops.context = ops_snapshot.context_size ? route->ops_context : NULL;
	route->sealed_ops.context = ops_snapshot.context_size ?
		route->sealed_ops_context : NULL;
	route->dma_protected = route->sealed_dma_protected = proof.dma_protected;
	route->dma_context_size = route->sealed_dma_context_size =
		proof.dma_context_size;
	if (proof.dma_context_size) {
		memcpy(route->dma_context, dma_context, proof.dma_context_size);
		memcpy(route->sealed_dma_context, dma_context,
			proof.dma_context_size);
	}
	route->protected_storage = route->sealed_protected_storage =
		protected_storage;
	route->protected_storage_context =
		route->sealed_protected_storage_context = protected_storage_context;
	route->last_invocation_generation = evidence->closed_generation;
	route->last_invocation_generation_inverse = ~evidence->closed_generation;
	if (!dma_valid(route))
		goto reject;
	scrub((void *)(uintptr_t)route->backing_base, (size_t)route->backing_size);
	if (!dma_valid(route))
		goto reject;
	__atomic_store_n(&route->state, S3_ROUTE_IDLE, __ATOMIC_RELEASE);
	status = CB_SUCCESS;
	goto out;
reject:
	scrub(route, sizeof(*route));
out:
	scrub(provenance_context, sizeof(provenance_context));
	scrub(dma_context, sizeof(dma_context));
	scrub(ops_context, sizeof(ops_context));
	scrub(&proof, sizeof(proof));
	scrub(&ops_snapshot, sizeof(ops_snapshot));
	scrub(&instance_snapshot, sizeof(instance_snapshot));
	scrub(&topology_snapshot, sizeof(topology_snapshot));
	scrub(&endpoint_snapshot, sizeof(endpoint_snapshot));
	return status;
}

static bool ticket_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_ticket *ticket)
{
	return ticket && ticket->generation &&
		ticket->command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE &&
		ticket->max_polls && ticket->max_polls <= SMM_INVOCATION_ENTRY_MAX_POLLS &&
		ticket->cpu < route->topology_snapshot.active_cpus &&
		ticket->lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD &&
		smm_invocation_loader_instance_nonce_equal(
			ticket->loader_instance_nonce,
			route->instance_snapshot.loader_instance_nonce) &&
		!ticket->reserved[0] && !ticket->reserved[1] && !ticket->reserved[2];
}

static bool runtime_disjoint(
	const struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const void *object, size_t size)
{
	return object_valid(object, size, 1U) &&
		!overlap(object, size, route, sizeof(*route)) &&
		!overlap(object, size, (void *)(uintptr_t)route->backing_base,
			(size_t)route->backing_size) &&
		!overlap(object, size, route->evidence, sizeof(*route->evidence)) &&
		!overlap(object, size, route->composition,
			sizeof(*route->composition)) &&
		!overlap(object, size, route->instance, sizeof(*route->instance)) &&
		!overlap(object, size, route->topology, sizeof(*route->topology));
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_s3_route_arrive(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	uint32_t cpu, uint32_t initial_apic_id,
	struct smm_invocation_entry_ticket *ticket)
{
	struct smm_invocation_entry_cause cause_snapshot;
	struct smm_invocation_entry_policy policy_snapshot;
	enum cb_err status;

	if (!route || !cause || !policy || !ticket ||
	    __atomic_load_n(&route->state, __ATOMIC_ACQUIRE) != S3_ROUTE_IDLE ||
	    !immutable_valid(route) || !runtime_disjoint(route, cause, sizeof(*cause)) ||
	    !runtime_disjoint(route, policy, sizeof(*policy)) ||
	    !runtime_disjoint(route, ticket, sizeof(*ticket)) ||
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

static bool selection_valid(const struct smm_apmc_selection_receipt *selection)
{
	return selection && selection->revision == SMM_APMC_SELECTION_RECEIPT_REVISION &&
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

enum smm_apmc_dispatch_result
payload_mm_authvar_presence_lifecycle_close_s3_route_dispatch_locked(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_ticket *ticket,
	struct smm_apmc_selection_receipt *selection)
{
	struct payload_mm_authvar_presence_lifecycle_close_message request;
	struct payload_mm_authvar_presence_lifecycle_close_message observed;
	struct payload_mm_authvar_presence_lifecycle_close_message response;
	struct smm_invocation_entry_ticket ticket_snapshot;
	struct smm_apmc_selection_receipt selection_snapshot;
	struct payload_mm_authvar_presence_lifecycle_close_message *message;
	struct smm_invocation_token token = { 0 };
	uint32_t expected = S3_ROUTE_IDLE;

	if (!object_valid(route, sizeof(*route), _Alignof(*route)) ||
	    !object_valid(ticket, sizeof(*ticket), _Alignof(*ticket)) ||
	    !object_valid(selection, sizeof(*selection), _Alignof(*selection)) ||
	    !__atomic_compare_exchange_n(&route->state, &expected,
		S3_ROUTE_DISPATCHING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return SMM_APMC_CONSUMED_REJECT;
	if (!immutable_valid(route) || !dma_valid(route))
		fail_stop(route);
	if (!runtime_disjoint(route, ticket, sizeof(*ticket)) ||
	    !runtime_disjoint(route, selection, sizeof(*selection)) ||
	    overlap(ticket, sizeof(*ticket), selection, sizeof(*selection)))
		goto reject;
	ticket_snapshot = *ticket;
	selection_snapshot = *selection;
	if (!ticket_valid(route, &ticket_snapshot) ||
	    ticket_snapshot.generation <= route->last_invocation_generation ||
	    ticket_snapshot.cpu != route->topology_snapshot.bsp_cpu ||
	    !selection_valid(&selection_snapshot) ||
	    selection_snapshot.identity != (uint64_t)(uintptr_t)selection ||
	    memcmp(ticket, &ticket_snapshot, sizeof(ticket_snapshot)) ||
	    memcmp(selection, &selection_snapshot, sizeof(selection_snapshot)))
		goto reject;
	if (!dma_valid(route) ||
	    smm_apmc_command_consume(SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		SMM_APMC_OWNER_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE, selection) !=
		SMM_APMC_CONSUMED_SUCCESS || !dma_valid(route))
		fail_stop(route);
	message = (void *)(uintptr_t)route->backing_base;
	if (!dma_valid(route) ||
	    __atomic_load_n(&message->completion, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING ||
	    !dma_valid(route))
		fail_stop(route);
	if (!dma_valid(route))
		fail_stop(route);
	request = *message;
	if (!dma_valid(route))
		fail_stop(route);
	if (!dma_valid(route))
		fail_stop(route);
	observed = *message;
	if (!dma_valid(route) || memcmp(&request, &observed, sizeof(request)) ||
	    payload_mm_authvar_presence_lifecycle_close_request_validate(
		&route->endpoint, &request, sizeof(request)) != CB_SUCCESS ||
	    request.source !=
		LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_REPROOF)
		fail_stop(route);
	if (!dma_valid(route) || smm_invocation_evidence_claim(route->evidence,
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SENTINEL,
		&route->ops, &token) != CB_SUCCESS || !dma_valid(route) ||
	    token.revision != SMM_INVOCATION_TOKEN_REVISION ||
	    token.size != sizeof(token) ||
	    token.initiator_cpu != route->topology_snapshot.bsp_cpu ||
	    token.active_cpus != route->topology_snapshot.active_cpus ||
	    token.smi_generation != ticket_snapshot.generation ||
	    token.rendezvous_generation != token.smi_generation ||
	    !nonzero(token.rendezvous_digest, sizeof(token.rendezvous_digest)) ||
	    token.bsp != 1U || token.reserved)
		fail_stop(route);
	route->token = route->sealed_token = token;
	route->last_invocation_generation = token.smi_generation;
	route->last_invocation_generation_inverse = ~token.smi_generation;
	route->active_ticket = route->sealed_active_ticket = ticket_snapshot;
	if (!dma_valid(route) || payload_mm_authvar_presence_authority_restrict(
		route->endpoint.generation) != CB_SUCCESS || !dma_valid(route) ||
	    payload_mm_authvar_presence_authority_restrict(
		route->endpoint.generation) != CB_SUCCESS || !dma_valid(route) ||
	    !dma_valid(route) ||
	    smm_invocation_evidence_publish_and_request_close(route->evidence,
		&route->token,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SUCCESS,
		&route->ops) != CB_SUCCESS || !dma_valid(route))
		fail_stop(route);
	if (!dma_valid(route) || memcmp(message, &request, sizeof(request)) ||
	    !dma_valid(route))
		fail_stop(route);
	response = request;
	response.status = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SUCCESS;
	response.completion = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING;
	if (!dma_valid(route))
		fail_stop(route);
	*message = response;
	if (!dma_valid(route) || memcmp(message, &response, sizeof(response)) ||
	    !dma_valid(route))
		fail_stop(route);
	__atomic_thread_fence(__ATOMIC_RELEASE);
	if (!dma_valid(route))
		fail_stop(route);
	__atomic_store_n(&message->completion,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPLETE,
		__ATOMIC_RELEASE);
	if (!dma_valid(route))
		fail_stop(route);
	__atomic_store_n(&route->state, S3_ROUTE_RESPONDED, __ATOMIC_RELEASE);
	return SMM_APMC_CONSUMED_SUCCESS;
reject:
	__atomic_store_n(&route->state, S3_ROUTE_IDLE, __ATOMIC_RELEASE);
	return SMM_APMC_CONSUMED_REJECT;
}

static bool active_ticket_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_ticket *ticket)
{
	return ticket_valid(route, ticket) &&
		ticket->generation == route->last_invocation_generation &&
		!memcmp(ticket, &route->active_ticket, sizeof(*ticket)) &&
		!memcmp(ticket, &route->sealed_active_ticket, sizeof(*ticket));
}

void payload_mm_authvar_presence_lifecycle_close_s3_route_prepare_lock_release(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_ticket *ticket)
{
	struct smm_invocation_entry_ticket snapshot;
	uint32_t expected = S3_ROUTE_RESPONDED;

	if (!route || !ticket || !immutable_valid(route) ||
	    !runtime_disjoint(route, ticket, sizeof(*ticket)))
		fail_stop(route);
	snapshot = *ticket;
	if (!active_ticket_valid(route, &snapshot) ||
	    snapshot.cpu != route->topology_snapshot.bsp_cpu ||
	    memcmp(ticket, &snapshot, sizeof(snapshot)) ||
	    !__atomic_compare_exchange_n(&route->state, &expected,
		S3_ROUTE_DEPARTING, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		fail_stop(route);
}

enum payload_mm_authvar_presence_lifecycle_close_s3_route_departure
payload_mm_authvar_presence_lifecycle_close_s3_route_depart(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_ticket *ticket)
{
	struct smm_invocation_entry_ticket snapshot;
	bool bsp;

	if (!route || !ticket || !immutable_valid(route) ||
	    !runtime_disjoint(route, ticket, sizeof(*ticket)))
		fail_stop(route);
	snapshot = *ticket;
	bsp = snapshot.cpu == route->topology_snapshot.bsp_cpu;
	if (!ticket_valid(route, &snapshot) ||
	    snapshot.generation != route->last_invocation_generation ||
	    (bsp && !active_ticket_valid(route, &snapshot)) ||
	    memcmp(ticket, &snapshot, sizeof(snapshot)) ||
	    __atomic_load_n(&route->state, __ATOMIC_ACQUIRE) !=
		S3_ROUTE_DEPARTING ||
	    smm_invocation_entry_depart(route->evidence, &snapshot) != CB_SUCCESS)
		fail_stop(route);
	if (!bsp)
		return
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_PARTICIPANT_DEPARTED;
	for (uint32_t poll = 0; poll < snapshot.max_polls; poll++) {
		if (smm_invocation_evidence_phase(route->evidence) ==
			SMM_INVOCATION_READY)
			break;
		if (poll + 1U == snapshot.max_polls)
			fail_stop(route);
		__asm__ __volatile__("pause");
	}
	if (!smm_invocation_entry_eos_ready(route->evidence, &snapshot))
		fail_stop(route);
	scrub(&route->token, sizeof(route->token));
	scrub(&route->sealed_token, sizeof(route->sealed_token));
	scrub(&route->active_ticket, sizeof(route->active_ticket));
	scrub(&route->sealed_active_ticket, sizeof(route->sealed_active_ticket));
	__atomic_store_n(&route->state, S3_ROUTE_IDLE, __ATOMIC_RELEASE);
	return PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_BSP_EOS_CONSUMED;
}
