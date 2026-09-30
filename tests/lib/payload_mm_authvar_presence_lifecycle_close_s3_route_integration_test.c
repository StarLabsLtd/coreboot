/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_s3_route.h>
#include <bootmem.h>
#include <cpu/x86/smm.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../src/lib/payload_mm_crypto/crypto.h"
#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_smm_authority.h"
#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_provision.h"

#undef assert
static void __noreturn assertion_failed(unsigned int line)
{
	char message[16] = "line 00000\n";
	ssize_t written;

	for (size_t index = 0; index < 5U; index++) {
		message[9U - index] = (char)('0' + line % 10U);
		line /= 10U;
	}
	written = write(STDERR_FILENO, message, 11U);
	(void)written;
	abort();
}
#define assert(condition) do { if (!(condition)) assertion_failed(__LINE__); } while (0)

#define AUTHORITY_GENERATION 7ULL
#define PROOF_MAGIC 0x73632d726f757465ULL

struct save_state_context {
	uint64_t *value;
	uint64_t magic;
};
struct range_context { uint64_t magic; };

struct integration_fixture {
	struct payload_mm_authvar_presence_lifecycle_close_s3_route route;
	uint8_t backing[
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE]
		__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_ALIGNMENT);
	uint8_t presence_backing[4096] __aligned(4096);
	uint8_t dma_memory[5 * 4096] __aligned(4096);
	uint8_t mirror_memory[4096] __aligned(4096);
	struct starbook_mtl_dma_receipt_frame dma_frame __aligned(8);
	struct smm_dma_receipt_memory dma_layout;
	struct smm_invocation_loader_composition composition;
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_evidence evidence;
	struct smm_invocation_topology topology;
	struct save_state_context save_state;
	uint64_t save_state_value;
	struct range_context provenance_context;
	struct range_context dma_context;
	struct range_context storage_context;
	struct smm_invocation_save_state_ops ops;
	struct lb_authvar_presence_lifecycle_close_endpoint endpoint;
	struct payload_mm_authvar_presence_lifecycle_close_s3_policy policy;
	unsigned int restrict_calls;
	uint64_t dma_wire;
	unsigned int epoch_collecting_calls;
	unsigned int epoch_late_calls;
	unsigned int epoch_terminal_calls;
};

struct arrival_call {
	struct integration_fixture *fixture;
	uint32_t cpu;
	uint32_t apic_id;
	bool through_route;
	struct smm_invocation_entry_ticket ticket;
};

struct departure_call {
	struct integration_fixture *fixture;
	const struct smm_invocation_entry_ticket *ticket;
	pthread_barrier_t *barrier;
	enum payload_mm_authvar_presence_lifecycle_close_s3_route_departure result;
};

static struct integration_fixture *active_fixture;
static struct smm_invocation_runtime_view *runtime_view = (void *)0x1234;

enum cb_err smm_invocation_runtime_view_get(
	const struct smm_invocation_runtime_view **view)
{
	*view = runtime_view;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_binding_get(
	struct smm_invocation_runtime_binding *binding)
{
	*binding = (struct smm_invocation_runtime_binding) {
		.composition = &active_fixture->composition,
		.instance = &active_fixture->instance,
		.evidence = &active_fixture->evidence,
		.topology = &active_fixture->topology,
	};
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base, size_t size)
{
	if (view != runtime_view || !base || !size)
		return CB_ERR;
	if (base == &active_fixture->dma_frame &&
	    size == sizeof(active_fixture->dma_frame))
		return CB_ERR;
	return CB_SUCCESS;
}

bool smm_get_dma_receipt_frame(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)&active_fixture->dma_frame;
	*size = sizeof(active_fixture->dma_frame);
	return true;
}

bool smm_get_dma_receipt_memory(const struct smm_dma_receipt_memory **memory)
{
	*memory = &active_fixture->dma_layout;
	return true;
}

void smm_region(uintptr_t *base, size_t *size)
{
	*base = 0x90000000U;
	*size = 0x100000U;
}

enum payload_mm_verify_status payload_mm_sha256(const void *message,
	size_t message_size, uint8_t digest[32])
{
	const uint8_t *bytes = message;
	uint8_t value = 0;

	for (size_t index = 0; index < message_size; index++)
		value ^= bytes[index];
	memset(digest, value, 32);
	return PAYLOAD_MM_VERIFY_OK;
}

enum cb_err starbook_mtl_dma_smm_authority_verify(void *context,
	const struct starbook_mtl_dma_smm_receipt *receipt)
{
	return context && receipt ? CB_SUCCESS : CB_ERR;
}

enum cb_err starbook_mtl_dma_smm_authority_verify_epoch(void *context,
	const struct starbook_mtl_dma_smm_receipt *receipt,
	const struct starbook_mtl_dma_requester_binding *binding)
{
	return context && receipt && binding &&
		binding->invocation_generation == receipt->invocation_generation + 1U ?
		CB_SUCCESS : CB_ERR;
}

enum cb_err starbook_mtl_dma_smm_authority_verify_live_policy(void *context,
	const struct starbook_mtl_dma_smm_receipt *receipt,
	const struct starbook_mtl_dma_requester_binding *binding)
{
	return context && receipt && binding &&
		binding->invocation_generation > receipt->invocation_generation ?
		CB_SUCCESS : CB_ERR;
}

enum cb_err starbook_mtl_dma_smm_verify(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	uint64_t lifecycle_base, size_t lifecycle_size,
	const struct starbook_mtl_dma_smm_observer *observer,
	const struct smm_invocation_runtime_view *view,
	struct starbook_mtl_dma_smm_workspace *workspace)
{
	(void)lifecycle_base;
	(void)lifecycle_size;
	(void)workspace;
	if (!receipt || !observer || view != runtime_view)
		return CB_ERR;
	return observer->verify_translation(observer->context, receipt);
}

bool starbook_mtl_dma_smm_receipt_geometry_valid(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	uint64_t forbidden_base, size_t forbidden_size)
{
	(void)forbidden_base;
	(void)forbidden_size;
	return receipt && receipt->tables.base && receipt->tables.size;
}

enum cb_err smm_invocation_platform_loader_instance_take(
	struct smm_invocation_loader_instance_seed *seed)
{
	*seed = (struct smm_invocation_loader_instance_seed) {
		.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION,
		.size = sizeof(*seed),
		.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD,
		.loader_instance_nonce = {
			.low = 0x1122334455667788ULL,
			.high = 0x8877665544332211ULL,
		},
	};
	return CB_SUCCESS;
}

void __noreturn smm_invocation_platform_fail_stop(void)
{
	abort();
}

enum cb_err payload_mm_authvar_presence_authority_restrict(uint64_t generation)
{
	assert(generation == AUTHORITY_GENERATION);
	active_fixture->restrict_calls++;
	return CB_SUCCESS;
}

static bool protected_storage(void *context, const void *object, size_t size)
{
	const uintptr_t first = (uintptr_t)object;
	const uintptr_t public_first = (uintptr_t)active_fixture->backing;

	assert(context == &active_fixture->storage_context);
	assert(active_fixture->storage_context.magic == PROOF_MAGIC);
	if (!object || !size || first > UINTPTR_MAX - (size - 1U))
		return false;
	return first + size <= public_first ||
		first >= public_first + sizeof(active_fixture->backing);
}

static bool range_proof(void *context, uint64_t base, uint64_t size)
{
	const struct range_context *proof = context;

	return proof && proof->magic == PROOF_MAGIC &&
		base == (uintptr_t)active_fixture->backing &&
		size == sizeof(active_fixture->backing);
}

static bool epoch_range_proof(void *context, uint64_t base, uint64_t size)
{
	const uint32_t phase = smm_invocation_evidence_phase(
		&active_fixture->evidence);

	assert(context && ((struct range_context *)context)->magic == PROOF_MAGIC);
	if (phase == SMM_INVOCATION_COLLECTING)
		active_fixture->epoch_collecting_calls++;
	else if (phase >= SMM_INVOCATION_CLAIMED &&
		 phase <= SMM_INVOCATION_CLOSING)
		active_fixture->epoch_late_calls++;
	else if (phase == SMM_INVOCATION_READY)
		active_fixture->epoch_terminal_calls++;
	return starbook_mtl_dma_smm_epoch_range_protected(NULL, base, size);
}

static enum smm_invocation_match dma_match(void *context, uint32_t cpu,
	uint8_t command)
{
	(void)context;
	return !cpu && command == SMM_APMC_STARBOOK_MTL_DMA_RECEIPT ?
		SMM_INVOCATION_MATCHED : SMM_INVOCATION_NOT_MATCHED;
}

static enum cb_err dma_read(void *context, uint32_t cpu, uint64_t *value)
{
	struct integration_fixture *fixture = context;

	if (fixture != active_fixture || cpu || !value)
		return CB_ERR;
	*value = fixture->dma_wire;
	return CB_SUCCESS;
}

static enum cb_err dma_write(void *context, uint32_t cpu, uint64_t value)
{
	struct integration_fixture *fixture = context;

	if (fixture != active_fixture || cpu)
		return CB_ERR;
	fixture->dma_wire = value;
	return CB_SUCCESS;
}

static enum smm_invocation_match match(void *context, uint32_t cpu,
	uint8_t command)
{
	assert(context);
	return !cpu && command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE ?
		SMM_INVOCATION_MATCHED : SMM_INVOCATION_NOT_MATCHED;
}

static enum cb_err read_value(void *context, uint32_t cpu, uint64_t *value)
{
	struct save_state_context *save_state = context;

	if (cpu || !value)
		return CB_ERR;
	assert(save_state->magic == PROOF_MAGIC && save_state->value);
	*value = *save_state->value;
	return CB_SUCCESS;
}

static enum cb_err write_value(void *context, uint32_t cpu, uint64_t value)
{
	struct save_state_context *save_state = context;

	if (cpu)
		return CB_ERR;
	assert(save_state->magic == PROOF_MAGIC && save_state->value);
	*save_state->value = value;
	return CB_SUCCESS;
}

static void *arrival_thread(void *argument)
{
	struct arrival_call *call = argument;
	struct integration_fixture *fixture = call->fixture;
	const struct smm_invocation_entry_cause cause = {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
		.size = sizeof(cause),
		.loader_instance_nonce = fixture->instance.loader_instance_nonce,
		.lifecycle = fixture->instance.lifecycle,
		.command = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		.recognized = 1U,
	};
	const struct smm_invocation_entry_policy entry_policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(entry_policy),
		.max_polls = SMM_INVOCATION_ENTRY_MAX_POLLS,
	};

	if (call->through_route)
		assert(payload_mm_authvar_presence_lifecycle_close_s3_route_arrive(
			&fixture->route, &cause, &entry_policy, call->cpu,
			call->apic_id, &call->ticket) == CB_SUCCESS);
	else
		assert(smm_invocation_entry_arrive(&fixture->evidence, &cause,
			&entry_policy, fixture->instance.loader_instance_nonce,
			SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE, call->cpu,
			call->apic_id, &call->ticket) == CB_SUCCESS);
	return NULL;
}

static void *departure_thread(void *argument)
{
	struct departure_call *call = argument;
	const int barrier = pthread_barrier_wait(call->barrier);

	assert(barrier == 0 || barrier == PTHREAD_BARRIER_SERIAL_THREAD);
	call->result = payload_mm_authvar_presence_lifecycle_close_s3_route_depart(
		&call->fixture->route, call->ticket);
	return NULL;
}

static void staged_arrivals(struct integration_fixture *fixture,
	bool through_route, struct arrival_call calls[2])
{
	pthread_t threads[2];
	unsigned int polls;

	calls[0] = (struct arrival_call) {
		.fixture = fixture, .cpu = 0U, .apic_id = 0x10U,
		.through_route = through_route,
	};
	calls[1] = (struct arrival_call) {
		.fixture = fixture, .cpu = 1U, .apic_id = 0x20U,
		.through_route = through_route,
	};
	assert(!pthread_create(&threads[0], NULL, arrival_thread, &calls[0]));
	for (polls = 0; polls < 1000000U; polls++) {
		if (smm_invocation_evidence_phase(&fixture->evidence) ==
			SMM_INVOCATION_COLLECTING)
			break;
		sched_yield();
	}
	assert(polls < 1000000U);
	assert(!fixture->evidence.closed_generation);
	assert(smm_invocation_loader_instance_nonce_is_zero(
		fixture->evidence.closed_loader_instance_nonce));
	assert(!fixture->evidence.closed_lifecycle);
	assert(!fixture->evidence.closed_eos_consumed);
	assert(!pthread_create(&threads[1], NULL, arrival_thread, &calls[1]));
	assert(!pthread_join(threads[0], NULL));
	assert(!pthread_join(threads[1], NULL));
	assert(calls[0].ticket.generation == calls[1].ticket.generation);
}

static void assert_terminal_evidence(struct integration_fixture *fixture,
	uint64_t generation)
{
	assert(smm_invocation_evidence_phase(&fixture->evidence) ==
		SMM_INVOCATION_READY);
	assert(fixture->evidence.generation == generation);
	assert(fixture->evidence.closed_generation == generation);
	assert(smm_invocation_loader_instance_nonce_equal(
		fixture->evidence.loader_instance_nonce,
		fixture->instance.loader_instance_nonce));
	assert(smm_invocation_loader_instance_nonce_equal(
		fixture->evidence.closed_loader_instance_nonce,
		fixture->instance.loader_instance_nonce));
	assert(fixture->evidence.loader_lifecycle ==
		SMM_INVOCATION_LOADER_S3_RELOAD);
	assert(fixture->evidence.closed_lifecycle ==
		SMM_INVOCATION_LOADER_S3_RELOAD);
	assert(fixture->evidence.active_cpus == fixture->topology.active_cpus);
	assert(fixture->evidence.bsp_cpu == fixture->topology.bsp_cpu);
	assert(fixture->evidence.closed_eos_consumed == 1U);
	assert(smm_invocation_evidence_phase(&fixture->evidence) ==
		SMM_INVOCATION_READY);
}

static uint64_t seed_predecessor(struct integration_fixture *fixture)
{
	struct arrival_call calls[2];
	struct smm_invocation_token token;

	fixture->save_state_value =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SENTINEL;
	staged_arrivals(fixture, false, calls);
	assert(smm_invocation_evidence_claim(&fixture->evidence,
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SENTINEL,
		&fixture->ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_publish_and_request_close(&fixture->evidence,
		&token, PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SUCCESS,
		&fixture->ops) == CB_SUCCESS);
	assert(smm_invocation_entry_depart(&fixture->evidence, &calls[1].ticket) ==
		CB_SUCCESS);
	assert(smm_invocation_entry_depart(&fixture->evidence, &calls[0].ticket) ==
		CB_SUCCESS);
	assert(smm_invocation_entry_eos_ready(&fixture->evidence,
		&calls[0].ticket));
	assert_terminal_evidence(fixture, calls[0].ticket.generation);
	return calls[0].ticket.generation;
}

static uint64_t run_route_round(struct integration_fixture *fixture)
{
	struct payload_mm_authvar_presence_lifecycle_close_message *message =
		(void *)fixture->backing;
	struct smm_apmc_selection_receipt selection;
	struct payload_mm_authvar_presence_lifecycle_close_message expected_response;
	struct arrival_call calls[2];
	struct departure_call departure_calls[2];
	pthread_barrier_t departure_barrier;
	pthread_t departure_threads[2];

	*message = (struct payload_mm_authvar_presence_lifecycle_close_message) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_REVISION,
		.size = sizeof(*message),
		.source = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_REPROOF,
		.generation = AUTHORITY_GENERATION,
		.status = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_PENDING,
		.completion = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING,
	};
	fixture->save_state_value =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SENTINEL;
	staged_arrivals(fixture, true, calls);
	message->request_id = calls[0].ticket.generation;
	expected_response = *message;
	expected_response.status =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SUCCESS;
	expected_response.completion =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPLETE;
	assert(smm_apmc_command_select(
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE, &selection) ==
		SMM_APMC_SELECT_ENABLED);
	assert(payload_mm_authvar_presence_lifecycle_close_s3_route_dispatch_locked(
		&fixture->route, &calls[0].ticket, &selection) ==
		SMM_APMC_CONSUMED_SUCCESS);
	assert(message->completion ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPLETE);
	assert(message->status ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SUCCESS);
	assert(!memcmp(message, &expected_response, sizeof(expected_response)));
	payload_mm_authvar_presence_lifecycle_close_s3_route_prepare_lock_release(
		&fixture->route, &calls[0].ticket);
	assert(!pthread_barrier_init(&departure_barrier, NULL, 2U));
	departure_calls[0] = (struct departure_call) {
		.fixture = fixture, .ticket = &calls[1].ticket,
		.barrier = &departure_barrier,
	};
	departure_calls[1] = (struct departure_call) {
		.fixture = fixture, .ticket = &calls[0].ticket,
		.barrier = &departure_barrier,
	};
	assert(!pthread_create(&departure_threads[0], NULL, departure_thread,
		&departure_calls[0]));
	assert(!pthread_create(&departure_threads[1], NULL, departure_thread,
		&departure_calls[1]));
	assert(!pthread_join(departure_threads[0], NULL));
	assert(!pthread_join(departure_threads[1], NULL));
	assert(!pthread_barrier_destroy(&departure_barrier));
	assert(departure_calls[0].result ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_PARTICIPANT_DEPARTED);
	assert(departure_calls[1].result ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_BSP_EOS_CONSUMED);
	assert_terminal_evidence(fixture, calls[0].ticket.generation);
	return calls[0].ticket.generation;
}

static void setup(struct integration_fixture *fixture)
{
	struct smm_invocation_topology_builder builder;
	uint32_t installed[2] = { 0x10U, 0x20U };

	memset(fixture, 0, sizeof(*fixture));
	active_fixture = fixture;
	fixture->dma_layout = (struct smm_dma_receipt_memory) {
		.revision = SMM_DMA_RECEIPT_MEMORY_REVISION,
		.size = sizeof(fixture->dma_layout),
		.frame = { (uintptr_t)&fixture->dma_frame,
			sizeof(fixture->dma_frame) },
		.dma = { (uintptr_t)fixture->dma_memory,
			sizeof(fixture->dma_memory) },
		.mirror = { (uintptr_t)fixture->mirror_memory,
			sizeof(fixture->mirror_memory) },
	};
	fixture->dma_frame = (struct starbook_mtl_dma_receipt_frame) {
		.revision = STARBOOK_MTL_DMA_RECEIPT_FRAME_REVISION,
		.size = sizeof(fixture->dma_frame),
		.state = STARBOOK_MTL_DMA_RECEIPT_FRAME_REQUEST,
		.candidate = {
			.revision = STARBOOK_MTL_DMA_SMM_RECEIPT_REVISION,
			.size = sizeof(fixture->dma_frame.candidate),
			.handoff = { (uintptr_t)fixture->dma_memory, 4096 },
			.tables = { (uintptr_t)fixture->dma_memory + 4096, 4096 },
			.table_mirror = {
				(uintptr_t)fixture->mirror_memory, 4096 },
			.arenas = {
				{ (uintptr_t)fixture->dma_memory + 8192, 4096 },
				{ (uintptr_t)fixture->dma_memory + 12288, 4096 },
				{ (uintptr_t)fixture->dma_memory + 16384, 4096 },
			},
		},
	};
	fixture->dma_wire = (uint64_t)(uint32_t)(uintptr_t)&fixture->dma_frame << 32 |
		STARBOOK_MTL_DMA_RECEIPT_WIRE_REQUEST;
	fixture->provenance_context.magic = PROOF_MAGIC;
	fixture->dma_context.magic = PROOF_MAGIC;
	fixture->storage_context.magic = PROOF_MAGIC;
	fixture->save_state.value = &fixture->save_state_value;
	fixture->save_state.magic = PROOF_MAGIC;
	assert(smm_invocation_topology_begin(&builder, &fixture->topology, 2U,
		installed[0]) == CB_SUCCESS);
	for (uint32_t cpu = 0; cpu < 2U; cpu++)
		assert(smm_invocation_topology_append(&builder, &installed[cpu],
			installed[cpu]) == CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, installed, 2U, 2U) ==
		CB_SUCCESS);
	assert(smm_invocation_loader_compose(&fixture->composition,
		&fixture->topology, &fixture->instance, &fixture->evidence) ==
		CB_SUCCESS);
	fixture->ops = (struct smm_invocation_save_state_ops) {
		.match_apmc_write = match,
		.read_value = read_value,
		.write_value = write_value,
		.context = &fixture->save_state,
		.context_size = sizeof(fixture->save_state),
	};
	fixture->endpoint =
		(struct lb_authvar_presence_lifecycle_close_endpoint) {
			.tag = LB_TAG_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT,
			.size = sizeof(fixture->endpoint),
			.revision =
				LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT_REVISION,
			.header_size = sizeof(fixture->endpoint),
			.flags = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_REQUIRED_FLAGS,
			.generation = AUTHORITY_GENERATION,
			.communication_base = (uintptr_t)fixture->backing,
			.communication_size =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
			.message_size =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
			.transport =
				LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT_APM_IO8,
			.trigger_width = 1U, .trigger_address = 0xb2U,
			.trigger_value =
				SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
			.source_mask =
				LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_SOURCE_MASK,
		};
	fixture->policy =
		(struct payload_mm_authvar_presence_lifecycle_close_s3_policy) {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_POLICY_REVISION,
			.size = sizeof(fixture->policy),
			.backing_base = (uintptr_t)fixture->backing,
			.backing_size = sizeof(fixture->backing),
			.backing_tag = BM_MEM_RESERVED,
			.dram_provenance = range_proof,
			.dram_provenance_context = &fixture->provenance_context,
			.dram_provenance_context_size =
				sizeof(fixture->provenance_context),
			.dma_protected = epoch_range_proof,
			.dma_context = &fixture->dma_context,
			.dma_context_size = sizeof(fixture->dma_context),
		};
}

int main(void)
{
	static struct integration_fixture fixture;
	const struct smm_invocation_save_state_ops dma_ops = {
		.match_apmc_write = dma_match,
		.read_value = dma_read,
		.write_value = dma_write,
		.context = &fixture,
		.context_size = sizeof(fixture),
	};
	const struct starbook_mtl_dma_smm_epoch_range ranges[2] = {
		{ (uintptr_t)fixture.presence_backing,
			sizeof(fixture.presence_backing) },
		{ (uintptr_t)fixture.backing, sizeof(fixture.backing) },
	};
	struct smm_invocation_entry_ticket activation_ticket;
	uint64_t predecessor, first, second;
	unsigned int terminal_before_retained_checks;

	setup(&fixture);
	predecessor = seed_predecessor(&fixture);
	assert(starbook_mtl_dma_receipt_provision_receive(&dma_ops) == CB_SUCCESS);
	assert(starbook_mtl_dma_smm_epoch_prepare(&fixture.instance,
		&fixture.evidence, &fixture.topology, ranges) == CB_SUCCESS);
	predecessor = seed_predecessor(&fixture);
	activation_ticket = (struct smm_invocation_entry_ticket) {
		.generation = predecessor,
		.loader_instance_nonce = fixture.instance.loader_instance_nonce,
		.cpu = fixture.topology.bsp_cpu,
		.lifecycle = fixture.instance.lifecycle,
	};
	assert(starbook_mtl_dma_smm_epoch_activate(&fixture.instance,
		&fixture.evidence, &fixture.topology, &activation_ticket,
		ranges) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_close_s3_route_provision(
		&fixture.route, &fixture.endpoint, &fixture.composition,
		&fixture.instance, &fixture.evidence, &fixture.topology,
		&fixture.ops, &fixture.policy, protected_storage,
		&fixture.storage_context, sizeof(fixture.storage_context)) == CB_SUCCESS);
	assert(starbook_mtl_dma_smm_epoch_retain() == CB_SUCCESS);
	terminal_before_retained_checks = fixture.epoch_terminal_calls;
	assert(fixture.route.predecessor_invocation_generation == predecessor);
	assert(fixture.route.ops.context != fixture.route.sealed_ops.context);
	assert(payload_mm_authvar_presence_lifecycle_close_s3_route_idle_exact(
		&fixture.route, &fixture.endpoint, (uintptr_t)fixture.backing,
		sizeof(fixture.backing)));
	first = run_route_round(&fixture);
	assert(payload_mm_authvar_presence_lifecycle_close_s3_route_idle_exact(
		&fixture.route, &fixture.endpoint, (uintptr_t)fixture.backing,
		sizeof(fixture.backing)));
	second = run_route_round(&fixture);
	assert(payload_mm_authvar_presence_lifecycle_close_s3_route_idle_exact(
		&fixture.route, &fixture.endpoint, (uintptr_t)fixture.backing,
		sizeof(fixture.backing)));
	assert(predecessor < first && first < second);
	assert(fixture.epoch_collecting_calls);
	assert(fixture.epoch_late_calls);
	assert(fixture.epoch_terminal_calls - terminal_before_retained_checks == 6U);
	assert(fixture.restrict_calls == 4U);
	return 0;
}
