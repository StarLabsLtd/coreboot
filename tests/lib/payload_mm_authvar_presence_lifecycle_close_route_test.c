/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_route.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_backing.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/lib/payload_mm_authvar_presence_lifecycle_close_claims.h"
#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install.h"
#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_provision.h"

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
static uint64_t internal_context_seed[2];
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
static struct smm_invocation_runtime_binding policy_runtime;
static struct starbook_mtl_dma_smm_receipt policy_dma_receipt;
static struct smm_dma_receipt_memory policy_dma_memory;
static struct bootmem_reservation_receipt_authority policy_verifier;
static struct payload_mm_authvar_presence_lifecycle_close_install_frame
	policy_frame;

enum cb_err smm_invocation_runtime_view_get(
	const struct smm_invocation_runtime_view **view)
{
	*view = (const void *)(uintptr_t)0x1234;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base, size_t size)
{
	return view && base && size ? CB_SUCCESS : CB_ERR;
}

enum cb_err smm_invocation_runtime_binding_get(
	struct smm_invocation_runtime_binding *binding)
{
	*binding = policy_runtime;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_dma_smm_binding_get(
	struct starbook_mtl_dma_smm_binding *binding)
{
	binding->receipt = &policy_dma_receipt;
	return CB_SUCCESS;
}

bool smm_get_dma_receipt_memory(const struct smm_dma_receipt_memory **memory)
{
	*memory = &policy_dma_memory;
	return true;
}

struct bootmem_reservation_receipt_authority *
smm_get_payload_mm_authvar_presence_lifecycle_close_backing_verifier(void)
{
	return &policy_verifier;
}

enum cb_err bootmem_reservation_receipt_verify_consume_exact_tag(
	struct bootmem_reservation_receipt_authority *authority,
	struct bootmem_reservation_receipt *receipt, enum bootmem_type expected_tag)
{
	memset(authority, 0, sizeof(*authority));
	memset(receipt, 0, sizeof(*receipt));
	return expected_tag == BM_MEM_RESERVED ? CB_SUCCESS : CB_ERR;
}

void __noreturn
platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop(void)
{
	longjmp(failure, 1);
}

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
	assert(context == internal_context_seed || context == route.internal_context ||
		context == route.sealed_internal_context);
	assert(((const uint64_t *)context)[0] == 0x1122334455667788ULL);
	assert(((const uint64_t *)context)[1] == 0x8877665544332211ULL);
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
	assert(context == route.internal_context);
	assert(context != internal_context_seed);
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
	assert(context == route.internal_context);
	assert(context != internal_context_seed);
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
	assert(storage_is_protected);
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
	memset(&policy_runtime, 0, sizeof(policy_runtime));
	memset(&policy_dma_receipt, 0, sizeof(policy_dma_receipt));
	memset(&policy_dma_memory, 0, sizeof(policy_dma_memory));
	memset(&policy_verifier, 0xa5, sizeof(policy_verifier));
	memset(&policy_frame, 0, sizeof(policy_frame));
	internal_context_seed[0] = 0x1122334455667788ULL;
	internal_context_seed[1] = 0x8877665544332211ULL;
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
	policy_runtime = (struct smm_invocation_runtime_binding) {
		.composition = &composition,
		.instance = &instance,
		.evidence = &evidence,
		.topology = &topology,
	};
	policy_dma_receipt.loader_instance_nonce = instance.loader_instance_nonce;
	policy_dma_receipt.loader_lifecycle = instance.lifecycle;
	policy_dma_receipt.handoff = (struct starbook_mtl_dma_smm_range) {
		.base = 0x100000, .size = 0x1000,
	};
	policy_dma_receipt.tables = (struct starbook_mtl_dma_smm_range) {
		.base = 0x200000, .size = 0x1000,
	};
	policy_dma_receipt.table_mirror = (struct starbook_mtl_dma_smm_range) {
		.base = 0x300000, .size = 0x1000,
	};
	for (size_t index = 0; index < ARRAY_SIZE(policy_dma_receipt.arenas);
	     index++) {
		policy_dma_receipt.arenas[index].base = 0x500000 + index * 0x10000;
		policy_dma_receipt.arenas[index].size = 0x1000;
	}
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

static void provision_with(
	const struct payload_mm_authvar_presence_lifecycle_close_internal_policy
		*internal,
	const struct smm_invocation_save_state_ops *ops,
	payload_mm_authvar_protected_storage storage, void *storage_context)
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
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt receipt;

	assert(payload_mm_authvar_presence_lifecycle_close_route_provision(&route,
		&descriptor, &transaction_slot, &composition, &instance, &evidence,
		&topology, ops, internal, PREDECESSOR_GENERATION,
		storage, storage_context, &receipt) == CB_SUCCESS);
	assert(receipt.installed == 1 && receipt.descriptor.generation ==
		TEST_GENERATION);
	assert(route.internal.context == route.internal_context);
	assert(route.sealed_internal.context == route.sealed_internal_context);
}

static void provision(void)
{
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
			.context = internal_context_seed,
			.context_size = sizeof(internal_context_seed),
		};

	provision_with(&internal, &ops, protected_storage, NULL);
	assert(route.internal.context != internal_context_seed);
	assert(!memcmp(route.internal_context, internal_context_seed,
		sizeof(internal_context_seed)));
	/* The caller-owned source is not retained as callback authority. */
	internal_context_seed[0] = 0;
	internal_context_seed[1] = 0;
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

static void mtl_policy_setup(void)
{
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		*dependencies = NULL;
	const struct smm_invocation_save_state_ops ops = {
		.match_apmc_write = match_apmc,
		.read_value = read_value,
		.write_value = write_value,
	};

	starbook_mtl_authvar_presence_lifecycle_close_install_policy_reset_test();
	reset();
	policy_frame.request.backing_base = (uintptr_t)backing;
	policy_frame.request.backing_size = sizeof(backing);
	policy_frame.request.backing_receipt =
		(struct bootmem_reservation_receipt) {
			.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
			.generation = 73,
			.base = (uintptr_t)backing,
			.bytes = sizeof(backing),
			.tag = BM_MEM_RESERVED,
			.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE,
		};
	policy_dma_memory.frame.base = (uintptr_t)&policy_frame;
	policy_dma_memory.frame.size = sizeof(policy_frame);
	assert(starbook_mtl_authvar_presence_lifecycle_close_install_policy(
		&dependencies, &ops) == CB_SUCCESS);
	assert(dependencies && dependencies->communication_range_valid(
		dependencies->communication_range_context,
		(uintptr_t)&policy_frame, sizeof(policy_frame)));
	provision_with(dependencies->internal, dependencies->active_ops,
		dependencies->protected_storage,
		dependencies->protected_storage_context);
	assert(!memcmp(route.internal_context, route.sealed_internal_context,
		route.internal.context_size));
}

static void mtl_policy_composition(void)
{
	struct payload_mm_authvar_presence_transaction_invocation invocation;

	mtl_policy_setup();
	assert(payload_mm_authvar_presence_warm_reset_close() == CB_SUCCESS);
	assert(!memcmp(route.internal_context, route.sealed_internal_context,
		route.internal.context_size));
	assert(payload_mm_authvar_presence_s3_resume_close() == CB_SUCCESS);
	assert(!memcmp(route.internal_context, route.sealed_internal_context,
		route.internal.context_size));
	if (!setjmp(failure)) {
		(void)payload_mm_authvar_presence_warm_reset_close();
		abort();
	}

	mtl_policy_setup();
	assert(owner_policy.claim_invocation(owner_policy.context,
		PAYLOAD_MM_AUTHVAR_PRESENCE_WARM_RESET_CLAIM,
		&invocation) == CB_SUCCESS);
	if (!setjmp(failure)) {
		(void)owner_policy.claim_invocation(owner_policy.context,
			PAYLOAD_MM_AUTHVAR_PRESENCE_S3_RESUME_CLAIM,
			&invocation);
		abort();
	}

	mtl_policy_setup();
	route.internal_context[0] ^= 1U;
	assert(payload_mm_authvar_presence_warm_reset_close() == CB_ERR);
}

int main(void)
{
	mtl_policy_composition();

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
