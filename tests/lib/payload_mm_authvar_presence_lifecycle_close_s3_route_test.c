/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_s3_route.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define AUTHORITY_GENERATION 7ULL
#define CLOSED_INVOCATION_GENERATION 10ULL
#define CONTEXT_MAGIC 0x8347a221U
#define TEST_BACKING_TAG 0x10002U

struct proof_context {
	uint32_t magic;
	uint32_t reserved;
};

static struct payload_mm_authvar_presence_lifecycle_close_s3_route route;
static uint8_t backing[
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE]
	__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_ALIGNMENT);
static struct lb_authvar_presence_lifecycle_close_endpoint endpoint;
static struct smm_invocation_loader_composition composition;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_evidence evidence;
static struct smm_invocation_topology topology;
static struct smm_invocation_save_state_ops ops;
static struct payload_mm_authvar_presence_lifecycle_close_s3_policy policy;
static struct proof_context provenance_context;
static struct proof_context dma_context;
static uint64_t invocation_generation;
static unsigned int arrivals;
static unsigned int departures;
static unsigned int restrict_calls;
static unsigned int consume_calls;
static unsigned int provenance_calls;
static unsigned int dma_calls;
static unsigned int dma_fail_at;
static bool provenance_result;
static bool provenance_mutates;
static bool backing_protected;
static uint64_t wire_generation;
static bool reenter_dispatch;
static bool contend_dispatch;
static const struct smm_invocation_entry_ticket *reentry_ticket;
static bool claim_mutates_mailbox;
static bool claim_mutates_route;

struct dispatch_contention {
	struct smm_apmc_selection_receipt *selection;
	enum smm_apmc_dispatch_result result;
};

static void *contending_dispatch(void *argument)
{
	struct dispatch_contention *contention = argument;

	contention->result =
		payload_mm_authvar_presence_lifecycle_close_s3_route_dispatch_locked(
			&route, reentry_ticket, contention->selection);
	return NULL;
}

static enum smm_invocation_match match_apmc(void *context, uint32_t cpu,
	uint8_t command)
{
	(void)context;
	return !cpu && command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE ?
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
	return !cpu && value ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SUCCESS ?
		CB_SUCCESS : CB_ERR;
}

static bool range_proof(void *context, uint64_t base, uint64_t size)
{
	const struct proof_context *proof = context;
	unsigned int call = __atomic_add_fetch(&dma_calls, 1U, __ATOMIC_RELAXED);

	return proof && proof->magic == CONTEXT_MAGIC && !proof->reserved &&
		base == (uintptr_t)backing && size == sizeof(backing) &&
		(!dma_fail_at || call != dma_fail_at);
}

static bool provenance_proof(void *context, uint64_t base, uint64_t size)
{
	struct proof_context *proof = context;
	bool valid;

	__atomic_add_fetch(&provenance_calls, 1U, __ATOMIC_RELAXED);
	valid = provenance_result && proof && proof->magic == CONTEXT_MAGIC &&
		!proof->reserved && base == (uintptr_t)backing &&
		size == sizeof(backing);
	if (provenance_mutates && proof)
		proof->reserved = 1;
	return valid;
}

static bool protected_storage(void *context, const void *base, size_t size)
{
	uintptr_t first = (uintptr_t)base;
	uintptr_t public_first = (uintptr_t)backing;

	(void)context;
	if (!base || !size || first > UINTPTR_MAX - (size - 1U))
		return false;
	return backing_protected || first + size <= public_first ||
		first >= public_first + sizeof(backing);
}

void __noreturn smm_invocation_platform_fail_stop(void)
{
	_exit(73);
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

uint32_t smm_invocation_evidence_phase(
	const struct smm_invocation_evidence *candidate)
{
	return candidate->state;
}

enum cb_err smm_invocation_entry_arrive(
	struct smm_invocation_evidence *candidate,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *entry_policy,
	struct smm_invocation_loader_instance_nonce expected_nonce,
	uint8_t expected_command, uint32_t cpu, uint32_t initial_apic_id,
	struct smm_invocation_entry_ticket *ticket)
{
	(void)initial_apic_id;
	assert(candidate == &evidence && cpu < 2U);
	assert(cause->recognized && cause->command == expected_command);
	assert(smm_invocation_loader_instance_nonce_equal(
		cause->loader_instance_nonce, expected_nonce));
	if (!arrivals++)
		invocation_generation++;
	*ticket = (struct smm_invocation_entry_ticket) {
		.generation = invocation_generation,
		.loader_instance_nonce = expected_nonce,
		.cpu = cpu,
		.lifecycle = cause->lifecycle,
		.max_polls = entry_policy->max_polls,
		.command = expected_command,
	};
	candidate->state = SMM_INVOCATION_COLLECTING;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_evidence_claim(
	struct smm_invocation_evidence *candidate, uint8_t command,
	uint64_t sentinel, const struct smm_invocation_save_state_ops *candidate_ops,
	struct smm_invocation_token *token)
{
	assert(candidate == &evidence && candidate_ops->match_apmc_write == match_apmc);
	assert(command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);
	assert(sentinel == PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SENTINEL);
	*token = (struct smm_invocation_token) {
		.revision = SMM_INVOCATION_TOKEN_REVISION,
		.size = sizeof(*token),
		.initiator_cpu = 0,
		.active_cpus = 2,
		.smi_generation = invocation_generation,
		.rendezvous_generation = invocation_generation,
		.rendezvous_digest = { 1, 2, 3 },
		.bsp = 1,
	};
	if (claim_mutates_mailbox)
		((struct payload_mm_authvar_presence_lifecycle_close_message *)
		 backing)->request_id++;
	if (claim_mutates_route)
		route.sealed_endpoint.generation++;
	candidate->state = SMM_INVOCATION_CLAIMED;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_evidence_publish_and_request_close(
	struct smm_invocation_evidence *candidate,
	const struct smm_invocation_token *token, uint64_t value,
	const struct smm_invocation_save_state_ops *candidate_ops)
{
	assert(candidate == &evidence && token->smi_generation == invocation_generation);
	assert(value == PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SUCCESS);
	assert(candidate_ops->write_value == write_value);
	candidate->state = SMM_INVOCATION_CLOSING;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_entry_depart(
	struct smm_invocation_evidence *candidate,
	const struct smm_invocation_entry_ticket *ticket)
{
	assert(candidate == &evidence && ticket->generation == invocation_generation);
	if (ticket->cpu == topology.bsp_cpu && departures != 1U)
		return CB_ERR;
	departures++;
	if (departures == 2U) {
		candidate->state = SMM_INVOCATION_READY;
		arrivals = 0;
		departures = 0;
	}
	return CB_SUCCESS;
}

bool smm_invocation_entry_eos_ready(
	struct smm_invocation_evidence *candidate,
	const struct smm_invocation_entry_ticket *ticket)
{
	return candidate == &evidence && ticket->cpu == topology.bsp_cpu &&
		candidate->state == SMM_INVOCATION_READY;
}

enum smm_apmc_dispatch_result smm_apmc_command_consume(
	uint8_t command, enum smm_apmc_owner owner,
	struct smm_apmc_selection_receipt *selection)
{
	assert(command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);
	assert(owner == SMM_APMC_OWNER_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);
	assert(selection->identity == (uintptr_t)selection);
	consume_calls++;
	if (reenter_dispatch) {
		reenter_dispatch = false;
		assert(payload_mm_authvar_presence_lifecycle_close_s3_route_dispatch_locked(
			&route, reentry_ticket, selection) == SMM_APMC_CONSUMED_REJECT);
	}
	if (contend_dispatch) {
		struct dispatch_contention contention = { .selection = selection };
		pthread_t thread;

		contend_dispatch = false;
		assert(!pthread_create(&thread, NULL, contending_dispatch,
			&contention));
		assert(!pthread_join(thread, NULL));
		assert(contention.result == SMM_APMC_CONSUMED_REJECT);
	}
	return SMM_APMC_CONSUMED_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_authority_restrict(uint64_t generation)
{
	assert(generation == AUTHORITY_GENERATION);
	restrict_calls++;
	return CB_SUCCESS;
}

static void reset_fixture(void)
{
	memset(&route, 0, sizeof(route));
	memset(backing, 0xa5, sizeof(backing));
	memset(&composition, 0, sizeof(composition));
	memset(&instance, 0, sizeof(instance));
	memset(&evidence, 0, sizeof(evidence));
	memset(&topology, 0, sizeof(topology));
	provenance_context = (struct proof_context) { .magic = CONTEXT_MAGIC };
	dma_context = (struct proof_context) { .magic = CONTEXT_MAGIC };
	endpoint = (struct lb_authvar_presence_lifecycle_close_endpoint) {
		.tag = LB_TAG_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT,
		.size = sizeof(endpoint),
		.revision = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT_REVISION,
		.header_size = sizeof(endpoint),
		.flags = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_REQUIRED_FLAGS,
		.generation = AUTHORITY_GENERATION,
		.communication_base = (uintptr_t)backing,
		.communication_size =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
		.message_size = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
		.transport = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT_APM_IO8,
		.trigger_width = 1,
		.trigger_address = 0xb2,
		.trigger_value = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		.source_mask = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_SOURCE_MASK,
	};
	composition.state = SMM_INVOCATION_LOADER_COMPOSITION_READY;
	composition.evidence_identity = (uintptr_t)&evidence;
	instance.state = SMM_INVOCATION_LOADER_INSTANCE_READY;
	instance.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION;
	instance.size = sizeof(instance);
	instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
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
	evidence.closed_generation = CLOSED_INVOCATION_GENERATION;
	evidence.closed_loader_instance_nonce = instance.loader_instance_nonce;
	evidence.closed_lifecycle = instance.lifecycle;
	evidence.closed_eos_consumed = 1;
	ops = (struct smm_invocation_save_state_ops) {
		.match_apmc_write = match_apmc,
		.read_value = read_value,
		.write_value = write_value,
	};
	policy = (struct payload_mm_authvar_presence_lifecycle_close_s3_policy) {
		.revision =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_POLICY_REVISION,
		.size = sizeof(policy),
		.backing_base = (uintptr_t)backing,
		.backing_size = sizeof(backing),
		.backing_tag = TEST_BACKING_TAG,
		.dram_provenance = provenance_proof,
		.dram_provenance_context = &provenance_context,
		.dram_provenance_context_size = sizeof(provenance_context),
		.dma_protected = range_proof,
		.dma_context = &dma_context,
		.dma_context_size = sizeof(dma_context),
	};
	invocation_generation = CLOSED_INVOCATION_GENERATION;
	arrivals = departures = restrict_calls = consume_calls = 0;
	provenance_calls = dma_calls = dma_fail_at = 0;
	provenance_result = true;
	provenance_mutates = false;
	backing_protected = false;
	wire_generation = AUTHORITY_GENERATION;
	reenter_dispatch = false;
	contend_dispatch = false;
	reentry_ticket = NULL;
	claim_mutates_mailbox = false;
	claim_mutates_route = false;
}

static enum cb_err provision(void)
{
	return payload_mm_authvar_presence_lifecycle_close_s3_route_provision(
		&route, &endpoint, &composition, &instance, &evidence, &topology,
		&ops, &policy, protected_storage, NULL);
}

static void round_with_source(uint32_t source)
{
	struct smm_invocation_entry_cause cause = {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
		.size = sizeof(cause),
		.loader_instance_nonce = instance.loader_instance_nonce,
		.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD,
		.command = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		.recognized = 1,
	};
	struct smm_invocation_entry_policy entry_policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(entry_policy), .max_polls = 10,
	};
	struct smm_invocation_entry_ticket bsp, ap;
	struct smm_apmc_selection_receipt selection;
	struct payload_mm_authvar_presence_lifecycle_close_message *message =
		(void *)backing;

	assert(payload_mm_authvar_presence_lifecycle_close_s3_route_arrive(&route,
		&cause, &entry_policy, 0, 0, &bsp) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_close_s3_route_arrive(&route,
		&cause, &entry_policy, 1, 1, &ap) == CB_SUCCESS);
	selection = (struct smm_apmc_selection_receipt) {
		.revision = SMM_APMC_SELECTION_RECEIPT_REVISION,
		.size = sizeof(selection), .identity = (uintptr_t)&selection,
		.generation = bsp.generation,
		.descriptor = {
			.command = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
			.owner = SMM_APMC_OWNER_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
			.role = SMM_APMC_EXCLUSIVE, .binding_count = 1,
			.reserved = true, .enabled = true,
		},
	};
	*message = (struct payload_mm_authvar_presence_lifecycle_close_message) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_REVISION,
		.size = sizeof(*message), .source = source,
		.generation = wire_generation, .request_id = bsp.generation,
		.status = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_PENDING,
		.completion = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING,
	};
	reentry_ticket = &bsp;
	assert(payload_mm_authvar_presence_lifecycle_close_s3_route_dispatch_locked(
		&route, &bsp, &selection) == SMM_APMC_CONSUMED_SUCCESS);
	assert(message->status ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SUCCESS);
	assert(message->completion ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPLETE);
	payload_mm_authvar_presence_lifecycle_close_s3_route_prepare_lock_release(
		&route, &bsp);
	assert(payload_mm_authvar_presence_lifecycle_close_s3_route_depart(&route,
		&ap) ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_PARTICIPANT_DEPARTED);
	assert(payload_mm_authvar_presence_lifecycle_close_s3_route_depart(&route,
		&bsp) ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_BSP_EOS_CONSUMED);
}

static void expect_source_fail_stop(uint32_t source)
{
	pid_t child = fork();
	int status;

	assert(child >= 0);
	if (!child) {
		round_with_source(source);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);
}

static void expect_round_fail_stop(uint32_t source, uint64_t generation,
	unsigned int fail_after)
{
	pid_t child = fork();
	int status;

	assert(child >= 0);
	if (!child) {
		wire_generation = generation;
		if (fail_after)
			dma_fail_at = dma_calls + fail_after;
		round_with_source(source);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);
}

struct provision_result { enum cb_err status; };

static void *provision_thread(void *argument)
{
	struct provision_result *result = argument;

	result->status = provision();
	return NULL;
}

static void provision_race(void)
{
	pthread_t threads[2];
	struct provision_result results[2];

	reset_fixture();
	assert(!pthread_create(&threads[0], NULL, provision_thread, &results[0]));
	assert(!pthread_create(&threads[1], NULL, provision_thread, &results[1]));
	assert(!pthread_join(threads[0], NULL));
	assert(!pthread_join(threads[1], NULL));
	assert((results[0].status == CB_SUCCESS) +
		(results[1].status == CB_SUCCESS) == 1);
}

int main(void)
{
	reset_fixture();
	assert(provision() == CB_SUCCESS);
	assert(provenance_calls == 1U);
	assert(provision() == CB_ERR);
	round_with_source(LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_REPROOF);
	reenter_dispatch = true;
	contend_dispatch = true;
	round_with_source(LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_REPROOF);
	assert(restrict_calls == 4U && consume_calls == 2U);

	reset_fixture();
	assert(provision() == CB_SUCCESS);
	expect_source_fail_stop(
		LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PRE_EXTERNAL_IMAGE);
	expect_source_fail_stop(
		LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PAYLOAD_FAILURE_OR_RETURN);
	expect_round_fail_stop(
		LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_REPROOF,
		AUTHORITY_GENERATION + 1U, 0);
	expect_round_fail_stop(
		LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_REPROOF,
		AUTHORITY_GENERATION, 2);
	claim_mutates_mailbox = true;
	expect_round_fail_stop(
		LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_REPROOF,
		AUTHORITY_GENERATION, 0);
	claim_mutates_mailbox = false;
	claim_mutates_route = true;
	expect_round_fail_stop(
		LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_REPROOF,
		AUTHORITY_GENERATION, 0);

	reset_fixture();
	instance.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	evidence.loader_lifecycle = instance.lifecycle;
	assert(provision() == CB_ERR);

	reset_fixture();
	policy.backing_tag = 0;
	assert(provision() == CB_ERR);

	reset_fixture();
	provenance_result = false;
	assert(provision() == CB_ERR && provenance_calls == 1U);

	reset_fixture();
	provenance_mutates = true;
	assert(provision() == CB_ERR && provenance_calls == 1U);

	reset_fixture();
	backing_protected = true;
	assert(provision() == CB_ERR);

	reset_fixture();
	policy.dma_context = backing;
	policy.dma_context_size = sizeof(dma_context);
	assert(provision() == CB_ERR);

	reset_fixture();
	memcpy(backing, &endpoint, sizeof(endpoint));
	assert(payload_mm_authvar_presence_lifecycle_close_s3_route_provision(
		&route, (const void *)backing, &composition, &instance, &evidence,
		&topology, &ops, &policy, protected_storage, NULL) == CB_ERR);

	for (unsigned int trial = 0; trial < 64U; trial++)
		provision_race();
	return 0;
}
