/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_route.h>
#include <cpu/x86/smm_command.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/lib/payload_mm_authvar_presence_lifecycle_close_claims.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define TEST_GENERATION 0x123456789abcdef0ULL
#define PREDECESSOR_GENERATION 10ULL

static struct payload_mm_authvar_presence_lifecycle_close_route route;
static uint8_t backing[
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE]
	__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_ALIGNMENT);
static struct payload_mm_authvar_presence_transaction_slot transaction_slot;
static struct smm_invocation_loader_composition composition;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_evidence evidence;
static struct smm_invocation_topology topology;
static struct payload_mm_authvar_presence_lifecycle_close_policy owner_policy;
static uint8_t owner_context[
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX];
static uint64_t next_invocation_generation;
static uint64_t last_wire_value;
static uint64_t last_completed_value;
static uint64_t active_owner_claim;
static unsigned int consume_calls;
static unsigned int arrival_count;
static unsigned int departure_count;
static unsigned int dma_calls;
static unsigned int dma_fail_at;
static bool corrupt_ticket_before_release;
static jmp_buf failure;

void __noreturn smm_invocation_platform_fail_stop(void)
{
	longjmp(failure, 1);
}

static bool protected_storage(void *context, const void *base, size_t size)
{
	(void)context;
	return base && size;
}

static bool dma_protected(void *context, uint64_t base, uint64_t size)
{
	(void)context;
	dma_calls++;
	return base == (uintptr_t)backing && size == sizeof(backing) &&
		(!dma_fail_at || dma_calls != dma_fail_at);
}

static enum smm_invocation_match match_apmc(void *context, uint32_t cpu,
	uint8_t command)
{
	(void)context;
	return cpu == 0 &&
		command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE ?
		SMM_INVOCATION_MATCHED : SMM_INVOCATION_NOT_MATCHED;
}

static enum cb_err read_value(void *context, uint32_t cpu, uint64_t *value)
{
	(void)context;
	if (cpu)
		return CB_ERR;
	*value = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SENTINEL;
	return CB_SUCCESS;
}

static enum cb_err write_value(void *context, uint32_t cpu, uint64_t value)
{
	(void)context;
	if (cpu)
		return CB_ERR;
	last_wire_value = value;
	return CB_SUCCESS;
}

static enum cb_err internal_claim(void *context, uint64_t claim,
	struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	(void)context;
	assert(claim == PAYLOAD_MM_AUTHVAR_PRESENCE_WARM_RESET_CLAIM ||
		claim == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_RESUME_CLAIM);
	*invocation =
		(struct payload_mm_authvar_presence_transaction_invocation) {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_INVOCATION_REVISION,
			.size = sizeof(*invocation),
			.initiator_cpu = 0,
			.active_cpus = 2,
			.smi_generation = ++next_invocation_generation,
			.rendezvous_generation = next_invocation_generation,
			.rendezvous_proof = { 1, 2, 3 },
			.bsp = 1,
		};
	return CB_SUCCESS;
}

static enum cb_err internal_complete(void *context,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	uint64_t value)
{
	(void)context;
	assert(invocation->smi_generation == next_invocation_generation);
	assert(value);
	last_completed_value = value;
	return CB_SUCCESS;
}

const struct smm_invocation_evidence *
smm_invocation_loader_composition_evidence(
	const struct smm_invocation_loader_composition *candidate,
	const struct smm_invocation_evidence *candidate_evidence)
{
	return candidate == &composition &&
		candidate->state == SMM_INVOCATION_LOADER_COMPOSITION_READY &&
		candidate->evidence_identity == (uintptr_t)candidate_evidence ?
		candidate_evidence : NULL;
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *candidate,
	struct smm_invocation_loader_instance *snapshot)
{
	if (candidate != &instance ||
	    candidate->state != SMM_INVOCATION_LOADER_INSTANCE_READY)
		return CB_ERR;
	*snapshot = *candidate;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_topology_read(
	const struct smm_invocation_topology *candidate,
	struct smm_invocation_topology *snapshot)
{
	if (candidate != &topology ||
	    candidate->state != SMM_INVOCATION_TOPOLOGY_READY)
		return CB_ERR;
	*snapshot = *candidate;
	return CB_SUCCESS;
}

bool payload_mm_authvar_presence_transaction_dispatch_enabled(
	const struct payload_mm_authvar_presence_transaction_slot *slot,
	uint64_t generation)
{
	return slot == &transaction_slot && generation == TEST_GENERATION;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_provision(
	const struct payload_mm_authvar_presence_lifecycle_close_policy *policy,
	payload_mm_authvar_protected_storage storage_is_protected,
	void *storage_context)
{
	assert(storage_is_protected == protected_storage);
	assert(!storage_context);
	owner_policy = *policy;
	assert(policy->context_size <= sizeof(owner_context));
	memcpy(owner_context, policy->context, policy->context_size);
	owner_policy.context = owner_context;
	return CB_SUCCESS;
}

static enum cb_err close_owner_source(uint64_t claim)
{
	struct payload_mm_authvar_presence_transaction_invocation invocation;
	enum cb_err status;

	active_owner_claim = claim;
	status = owner_policy.claim_invocation(owner_policy.context, claim,
		&invocation);
	if (status != CB_SUCCESS)
		return status;
	last_completed_value = 0x1000U + claim;
	return owner_policy.complete_invocation(owner_policy.context, &invocation,
		last_completed_value);
}

enum cb_err payload_mm_authvar_presence_pre_external_image_close(void)
{
	return close_owner_source(
		PAYLOAD_MM_AUTHVAR_PRESENCE_PRE_EXTERNAL_CLAIM);
}

enum cb_err payload_mm_authvar_presence_payload_failure_or_return_close(void)
{
	return close_owner_source(
		PAYLOAD_MM_AUTHVAR_PRESENCE_PAYLOAD_FAILURE_CLAIM);
}

enum cb_err payload_mm_authvar_presence_warm_reset_close(void)
{
	return close_owner_source(PAYLOAD_MM_AUTHVAR_PRESENCE_WARM_RESET_CLAIM);
}

enum cb_err payload_mm_authvar_presence_s3_resume_close(void)
{
	return close_owner_source(PAYLOAD_MM_AUTHVAR_PRESENCE_S3_RESUME_CLAIM);
}

enum cb_err payload_mm_authvar_presence_closed_reproof(void)
{
	return close_owner_source(PAYLOAD_MM_AUTHVAR_PRESENCE_CLOSED_REPROOF_CLAIM);
}

enum cb_err smm_invocation_evidence_claim(
	struct smm_invocation_evidence *candidate, uint8_t command,
	uint64_t sentinel, const struct smm_invocation_save_state_ops *ops,
	struct smm_invocation_token *token)
{
	assert(candidate == &evidence);
	assert(command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);
	assert(sentinel ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SENTINEL);
	assert(ops->match_apmc_write == match_apmc);
	*token = (struct smm_invocation_token) {
		.revision = SMM_INVOCATION_TOKEN_REVISION,
		.size = sizeof(*token),
		.initiator_cpu = 0,
		.active_cpus = 2,
		.smi_generation = next_invocation_generation,
		.rendezvous_generation = next_invocation_generation,
		.rendezvous_digest = { 1, 2, 3 },
		.bsp = 1,
	};
	candidate->state = SMM_INVOCATION_CLAIMED;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_evidence_publish_and_request_close(
	struct smm_invocation_evidence *candidate,
	const struct smm_invocation_token *token, uint64_t value,
	const struct smm_invocation_save_state_ops *ops)
{
	assert(candidate == &evidence);
	assert(token->smi_generation == next_invocation_generation);
	assert(value == PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SUCCESS);
	assert(ops->write_value == write_value);
	last_wire_value = value;
	candidate->state = SMM_INVOCATION_CLOSING;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_entry_arrive(
	struct smm_invocation_evidence *candidate,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	struct smm_invocation_loader_instance_nonce expected_nonce,
	uint8_t expected_command, uint32_t cpu, uint32_t initial_apic_id,
	struct smm_invocation_entry_ticket *ticket)
{
	(void)initial_apic_id;
	assert(candidate == &evidence && cpu < 2);
	assert(cause->command == expected_command && cause->recognized);
	assert(smm_invocation_loader_instance_nonce_equal(
		cause->loader_instance_nonce, expected_nonce));
	if (!arrival_count++)
		next_invocation_generation++;
	*ticket = (struct smm_invocation_entry_ticket) {
		.generation = next_invocation_generation,
		.loader_instance_nonce = expected_nonce,
		.cpu = cpu,
		.lifecycle = cause->lifecycle,
		.max_polls = policy->max_polls,
		.command = expected_command,
	};
	candidate->state = SMM_INVOCATION_COLLECTING;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_entry_depart(
	struct smm_invocation_evidence *candidate,
	const struct smm_invocation_entry_ticket *ticket)
{
	assert(candidate == &evidence);
	assert(ticket->generation == next_invocation_generation);
	if (++departure_count == 2) {
		candidate->state = SMM_INVOCATION_READY;
		arrival_count = 0;
		departure_count = 0;
	}
	return CB_SUCCESS;
}

bool smm_invocation_entry_eos_ready(
	struct smm_invocation_evidence *candidate,
	const struct smm_invocation_entry_ticket *ticket)
{
	return candidate == &evidence && ticket->cpu == 0 &&
		candidate->state == SMM_INVOCATION_READY;
}

uint32_t smm_invocation_evidence_phase(
	const struct smm_invocation_evidence *candidate)
{
	return candidate->state;
}

enum smm_apmc_dispatch_result smm_apmc_command_consume(
	uint8_t command, enum smm_apmc_owner owner,
	struct smm_apmc_selection_receipt *selection)
{
	assert(command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);
	assert(owner == SMM_APMC_OWNER_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);
	assert(selection->identity == (uintptr_t)selection);
	consume_calls++;
	return SMM_APMC_CONSUMED_SUCCESS;
}

static void reset(void)
{
	memset(&route, 0, sizeof(route));
	memset(backing, 0, sizeof(backing));
	memset(&transaction_slot, 0, sizeof(transaction_slot));
	memset(&composition, 0, sizeof(composition));
	memset(&instance, 0, sizeof(instance));
	memset(&evidence, 0, sizeof(evidence));
	memset(&topology, 0, sizeof(topology));
	memset(&owner_policy, 0, sizeof(owner_policy));
	composition.state = SMM_INVOCATION_LOADER_COMPOSITION_READY;
	composition.evidence_identity = (uintptr_t)&evidence;
	instance.state = SMM_INVOCATION_LOADER_INSTANCE_READY;
	instance.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION;
	instance.size = sizeof(instance);
	instance.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	instance.loader_instance_nonce.low = 0x11;
	instance.loader_instance_nonce.high = 0x22;
	topology.state = SMM_INVOCATION_TOPOLOGY_READY;
	topology.revision = SMM_INVOCATION_TOPOLOGY_REVISION;
	topology.size = sizeof(topology);
	topology.active_cpus = 2;
	topology.bsp_cpu = 0;
	evidence.state = SMM_INVOCATION_READY;
	evidence.active_cpus = 2;
	evidence.bsp_cpu = 0;
	evidence.loader_instance_nonce = instance.loader_instance_nonce;
	evidence.loader_lifecycle = instance.lifecycle;
	evidence.closed_generation = PREDECESSOR_GENERATION;
	evidence.closed_eos_consumed = 1;
	next_invocation_generation = PREDECESSOR_GENERATION;
	last_wire_value = 0;
	last_completed_value = 0;
	active_owner_claim = 0;
	consume_calls = 0;
	arrival_count = 0;
	departure_count = 0;
	dma_calls = 0;
	dma_fail_at = 0;
	corrupt_ticket_before_release = false;
}

static void provision(void)
{
	const struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		descriptor = {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_REVISION,
			.size = sizeof(descriptor),
			.generation = TEST_GENERATION,
			.backing_base = (uintptr_t)backing,
			.backing_size = sizeof(backing),
			.challenge = { 1, 2, 3, 4 },
			.message_size =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
			.trigger_address = 0xb2,
			.trigger_value =
				SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
			.source_mask =
				LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_SOURCE_MASK,
		};
	const struct smm_invocation_save_state_ops ops = {
		.match_apmc_write = match_apmc,
		.read_value = read_value,
		.write_value = write_value,
	};
	const struct payload_mm_authvar_presence_lifecycle_close_internal_policy
		internal = {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_POLICY_REVISION,
			.size = sizeof(internal),
			.claim = internal_claim,
			.complete = internal_complete,
			.dma_protected = dma_protected,
		};
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt receipt;

	assert(payload_mm_authvar_presence_lifecycle_close_route_provision(&route,
		&descriptor, &transaction_slot, &composition, &instance, &evidence,
		&topology, &ops, &internal, PREDECESSOR_GENERATION,
		protected_storage, NULL, &receipt) == CB_SUCCESS);
	assert(receipt.installed == 1 && receipt.descriptor.generation ==
		TEST_GENERATION);
}

static void public_round(uint32_t source)
{
	struct smm_invocation_entry_cause cause = {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
		.size = sizeof(cause),
		.loader_instance_nonce = instance.loader_instance_nonce,
		.lifecycle = instance.lifecycle,
		.command = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		.recognized = 1,
	};
	struct smm_invocation_entry_policy policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(policy),
		.max_polls = 10,
	};
	struct smm_invocation_entry_ticket ticket;
	struct smm_invocation_entry_ticket participant_ticket;
	struct smm_apmc_selection_receipt selection;
	struct payload_mm_authvar_presence_lifecycle_close_message *message =
		(void *)backing;

	assert(payload_mm_authvar_presence_lifecycle_close_route_arrive(&route,
		&cause, &policy, 0, 0, &ticket) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_close_route_arrive(&route,
		&cause, &policy, 1, 1, &participant_ticket) == CB_SUCCESS);
	assert(participant_ticket.generation == ticket.generation);
	selection = (struct smm_apmc_selection_receipt) {
		.revision = SMM_APMC_SELECTION_RECEIPT_REVISION,
		.size = sizeof(selection),
		.identity = (uintptr_t)&selection,
		.generation = ticket.generation,
		.descriptor = {
			.command = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
			.owner =
				SMM_APMC_OWNER_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
			.role = SMM_APMC_EXCLUSIVE,
			.binding_count = 1,
			.reserved = true,
			.enabled = true,
		},
	};
	*message =
		(struct payload_mm_authvar_presence_lifecycle_close_message) {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_REVISION,
			.size = sizeof(*message),
			.source = source,
			.generation = TEST_GENERATION,
			.request_id = ticket.generation,
			.status =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_PENDING,
			.completion =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING,
		};
	assert(payload_mm_authvar_presence_lifecycle_close_route_dispatch_locked(
		&route, &ticket, &selection) == SMM_APMC_CONSUMED_SUCCESS);
	assert(message->status ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SUCCESS);
	assert(message->completion ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPLETE);
	assert(last_wire_value ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SUCCESS);
	if (corrupt_ticket_before_release)
		route.sealed_active_ticket.generation++;
	payload_mm_authvar_presence_lifecycle_close_route_prepare_lock_release(
		&route, &ticket);
	assert(payload_mm_authvar_presence_lifecycle_close_route_depart(&route,
		&participant_ticket) ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_PARTICIPANT_DEPARTED);
	assert(payload_mm_authvar_presence_lifecycle_close_route_depart(&route,
		&ticket) ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_BSP_EOS_CONSUMED);
}

int main(void)
{
	reset();
	provision();
	public_round(LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PRE_EXTERNAL_IMAGE);
	assert(active_owner_claim ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_PRE_EXTERNAL_CLAIM);
	public_round(LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_REPROOF);
	assert(active_owner_claim ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_CLOSED_REPROOF_CLAIM);
	public_round(
		LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PAYLOAD_FAILURE_OR_RETURN);
	assert(active_owner_claim ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_PAYLOAD_FAILURE_CLAIM);
	assert(consume_calls == 3);
	assert(payload_mm_authvar_presence_warm_reset_close() == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_resume_close() == CB_SUCCESS);

	reset();
	provision();
	/* The pre-admission check succeeds; the first post-consume recheck fails. */
	dma_fail_at = dma_calls + 2;
	if (!setjmp(failure)) {
		public_round(
			LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PRE_EXTERNAL_IMAGE);
		abort();
	}

	reset();
	provision();
	corrupt_ticket_before_release = true;
	if (!setjmp(failure)) {
		public_round(
			LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PRE_EXTERNAL_IMAGE);
		abort();
	}
	return 0;
}
