/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_route_session.h>
#include <bootmem_reservation_receipt_internal.h>
#include <cpu/x86/apm.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm_invocation_loader_composition.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#undef assert
static void assertion_failed(unsigned int line)
{
	char message[16] = "line 00000\n";
	ssize_t written;

	for (size_t index = 0; index < 5U; ++index) {
		message[9U - index] = (char)('0' + line % 10U);
		line /= 10U;
	}
	written = write(STDERR_FILENO, message, 11U);
	(void)written;
	abort();
}
#define assert(condition) do { if (!(condition)) assertion_failed(__LINE__); } while (0)

struct save_state_fixture {
	uint64_t rax;
};

struct callback_sentinel {
	uint64_t identity;
	uint8_t content[8];
};

#define AUTHORITY_CONTEXT_ID 0x617574686f726974ULL
#define PROOF_CONTEXT_ID 0x70726f6f662d6374ULL

struct integration_fixture {
	struct payload_mm_authvar_presence_route_session session;
	struct payload_mm_authvar_presence_arm arm;
	struct payload_mm_authvar_presence_transaction_slot slot;
	struct payload_mm_authvar_presence_transaction_page page
		__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE);
	struct smm_invocation_loader_composition composition;
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_evidence evidence;
	struct smm_invocation_topology topology;
	struct save_state_fixture save_state;
	struct payload_mm_authvar_presence_transaction_binding binding;
	struct payload_mm_authvar_presence_seed seed;
	struct bootmem_reservation_receipt_authority signer;
	struct bootmem_reservation_receipt_authority verifier;
	struct bootmem_reservation_receipt receipt;
	struct smm_invocation_save_state_ops ops;
	struct payload_mm_authvar_presence_route_authority_policy policy;
	struct callback_sentinel authority_context;
	struct callback_sentinel proof_context;
	pthread_barrier_t arrival_barrier;
	pthread_mutex_t handler_lock;
	unsigned int prepare_calls;
	unsigned int commit_calls;
	unsigned int abort_calls;
	unsigned int dma_calls;
};

struct arrival_call {
	struct integration_fixture *fixture;
	uint32_t cpu;
	uint32_t apic_id;
	struct smm_invocation_entry_ticket ticket;
};

static struct integration_fixture *active_fixture;

struct payload_mm_authvar_presence_arm *
platform_payload_mm_authvar_presence_arm(void)
{
	return &active_fixture->arm;
}

struct payload_mm_authvar_presence_transaction_slot *
smm_get_payload_mm_authvar_presence_transaction_slot(void)
{
	return &active_fixture->slot;
}

enum cb_err smm_invocation_platform_loader_instance_take(
	struct smm_invocation_loader_instance_seed *seed)
{
	*seed = (struct smm_invocation_loader_instance_seed) {
		.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION,
		.size = sizeof(*seed),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
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

static bool protected_storage(void *context, const void *object, size_t size)
{
	assert(context == &active_fixture->proof_context);
	assert(active_fixture->proof_context.identity == PROOF_CONTEXT_ID);
	assert(active_fixture->proof_context.content[0] == 0x3cU &&
		active_fixture->proof_context.content[1] == 0xc3U);
	return object && size;
}

static void assert_authority_context(void *context)
{
	assert(context == active_fixture->session.authority_context);
	assert(!memcmp(context, &active_fixture->authority_context,
		sizeof(active_fixture->authority_context)));
}

static void assert_authority_context_copy(void *context)
{
	assert(context);
	assert(!memcmp(context, &active_fixture->authority_context,
		sizeof(active_fixture->authority_context)));
}

static enum smm_invocation_match match(void *context, uint32_t cpu,
	uint8_t command)
{
	(void)context;
	if (command != SMM_APMC_AUTHVAR_PRESENCE)
		return SMM_INVOCATION_MATCH_ERROR;
	return cpu == 0U ? SMM_INVOCATION_MATCHED : SMM_INVOCATION_NOT_MATCHED;
}

static enum cb_err read_rax(void *context, uint32_t cpu, uint64_t *value)
{
	struct save_state_fixture *save_state = context;

	if (cpu != 0U || !value)
		return CB_ERR;
	*value = save_state->rax;
	return CB_SUCCESS;
}

static enum cb_err write_rax(void *context, uint32_t cpu, uint64_t value)
{
	struct save_state_fixture *save_state = context;

	if (cpu != 0U || !value)
		return CB_ERR;
	save_state->rax = value;
	return CB_SUCCESS;
}

static enum cb_err authority_prepare(void *context,
	const struct payload_mm_authvar_presence_seed *seed, uint64_t generation)
{
	assert_authority_context(context);
	active_fixture->prepare_calls++;
	return seed && seed->endpoint.generation == generation ? CB_SUCCESS : CB_ERR;
}

static enum cb_err authority_commit(void *context, uint64_t generation)
{
	assert_authority_context(context);
	active_fixture->commit_calls++;
	return generation ? CB_SUCCESS : CB_ERR;
}

static enum cb_err authority_abort(void *context, uint64_t generation)
{
	assert_authority_context(context);
	active_fixture->abort_calls++;
	return generation ? CB_SUCCESS : CB_ERR;
}

static bool authority_dma(void *context, uint64_t base, uint64_t size)
{
	assert_authority_context(context);
	active_fixture->dma_calls++;
	return base == (uintptr_t)&active_fixture->page &&
		size == sizeof(active_fixture->page);
}

static void __noreturn authority_fail_stop(void *context)
{
	assert_authority_context_copy(context);
	abort();
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
		.command = SMM_APMC_AUTHVAR_PRESENCE,
		.recognized = 1U,
	};
	const struct smm_invocation_entry_policy policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(policy),
		.max_polls = SMM_INVOCATION_ENTRY_MAX_POLLS,
	};
	const int barrier = pthread_barrier_wait(&fixture->arrival_barrier);

	assert(barrier == 0 || barrier == PTHREAD_BARRIER_SERIAL_THREAD);
	assert(payload_mm_authvar_presence_route_session_arrive(&fixture->session,
		&cause, &policy, call->cpu, call->apic_id, &call->ticket) ==
		CB_SUCCESS);
	return NULL;
}

static uint64_t run_round(struct integration_fixture *fixture,
	uint32_t decision)
{
	struct arrival_call calls[2] = {
		{ .fixture = fixture, .cpu = 0U, .apic_id = 0x10U },
		{ .fixture = fixture, .cpu = 1U, .apic_id = 0x20U },
	};
	struct smm_apmc_descriptor selection;
	pthread_t threads[2];

	memset(&fixture->page, 0, sizeof(fixture->page));
	fixture->page.request.binding = fixture->binding;
	fixture->page.request.decision = decision;
	if (decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE)
		fixture->page.request.seed = fixture->seed;
	fixture->save_state.rax = SMM_APMC_AUTHVAR_PRESENCE;
	assert(!pthread_barrier_init(&fixture->arrival_barrier, NULL, 2U));
	assert(!pthread_create(&threads[0], NULL, arrival_thread, &calls[0]));
	assert(!pthread_create(&threads[1], NULL, arrival_thread, &calls[1]));
	assert(!pthread_join(threads[0], NULL));
	assert(!pthread_join(threads[1], NULL));
	assert(!pthread_barrier_destroy(&fixture->arrival_barrier));
	assert(calls[0].ticket.generation == calls[1].ticket.generation);
	assert(calls[0].ticket.generation != 0U);
	assert(calls[0].ticket.cpu == 0U && calls[1].ticket.cpu == 1U);
	assert(!pthread_mutex_lock(&fixture->handler_lock));
	assert(smm_apmc_command_select(SMM_APMC_AUTHVAR_PRESENCE, &selection) ==
		SMM_APMC_SELECT_ENABLED);
	assert(payload_mm_authvar_presence_route_session_dispatch_locked(
		&fixture->session, &calls[0].ticket, &selection) ==
		SMM_APMC_CONSUMED_SUCCESS);
	assert(!memcmp(&selection, &(struct smm_apmc_descriptor) { 0 },
		sizeof(selection)));
	payload_mm_authvar_presence_route_session_prepare_lock_release(
		&fixture->session, &calls[0].ticket);
	assert(!pthread_mutex_unlock(&fixture->handler_lock));
	assert(payload_mm_authvar_presence_transaction_ack_valid(&fixture->binding,
		decision, &fixture->page.ack, fixture->save_state.rax));
	assert(payload_mm_authvar_presence_route_session_depart(&fixture->session,
		&calls[1].ticket) ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PARTICIPANT_DEPARTED);
	assert(payload_mm_authvar_presence_route_session_depart(&fixture->session,
		&calls[0].ticket) ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_BSP_EOS_CONSUMED);
	assert(smm_invocation_evidence_phase(&fixture->evidence) ==
		SMM_INVOCATION_READY);
	assert(fixture->evidence.closed_eos_consumed == 1U);
	return calls[0].ticket.generation;
}

static void setup(struct integration_fixture *fixture)
{
	struct smm_invocation_topology_builder builder;
	struct bootmem_aligned_reservation_handle handle = { .opaque = { 1U, 2U } };
	uint32_t installed[2] = { 0x10U, 0x20U };
	uint8_t secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE] = { 1U };

	memset(fixture, 0, sizeof(*fixture));
	active_fixture = fixture;
	fixture->authority_context = (struct callback_sentinel) {
		.identity = AUTHORITY_CONTEXT_ID, .content = { 0xa5U, 0x5aU },
	};
	fixture->proof_context = (struct callback_sentinel) {
		.identity = PROOF_CONTEXT_ID, .content = { 0x3cU, 0xc3U },
	};
	assert(!pthread_mutex_init(&fixture->handler_lock, NULL));
	assert(smm_invocation_topology_begin(&builder, &fixture->topology, 2U,
		installed[0]) == CB_SUCCESS);
	for (uint32_t cpu = 0; cpu < 2U; ++cpu)
		assert(smm_invocation_topology_append(&builder, &installed[cpu],
			installed[cpu]) == CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, installed, 2U, 2U) ==
		CB_SUCCESS);
	assert(smm_invocation_loader_compose(&fixture->composition,
		&fixture->topology, &fixture->instance, &fixture->evidence) ==
		CB_SUCCESS);
	fixture->binding =
		(struct payload_mm_authvar_presence_transaction_binding) {
			.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION,
			.size = sizeof(fixture->binding),
			.generation = 9U, .transaction_id = 10U, .nonce = 11U,
			.initiator_cpu = 0U, .maximum_cpus = 2U,
			.capability = { 1U },
		};
	fixture->seed = (struct payload_mm_authvar_presence_seed) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION,
		.size = sizeof(fixture->seed),
		.endpoint = {
			.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT,
			.size = sizeof(struct lb_authvar_presence_endpoint),
			.revision = LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION,
			.header_size = sizeof(struct lb_authvar_presence_endpoint),
			.flags = LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS,
			.generation = fixture->binding.generation,
			.communication_base = (uintptr_t)&fixture->page,
			.communication_size =
				PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
			.message_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
			.transport = LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8,
			.trigger_width = 1U, .trigger_address = APM_CNT,
			.trigger_value = SMM_APMC_AUTHVAR_PRESENCE,
			.action_scope = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
			.capability_size = LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE,
		},
		.backing = {
			.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION,
			.size = sizeof(struct payload_mm_authvar_presence_backing),
			.base = (uintptr_t)&fixture->page,
			.bytes = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE,
			.generation = fixture->binding.generation,
			.tag = BM_MEM_RESERVED,
		},
		.capability = { 1U },
	};
	assert(bootmem_reservation_receipt_provision(&fixture->signer,
		&fixture->verifier, secret, BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
		fixture->binding.generation, &handle) == CB_SUCCESS);
	fixture->receipt = (struct bootmem_reservation_receipt) {
		.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION,
		.size = sizeof(fixture->receipt),
		.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
		.generation = fixture->binding.generation,
		.sequence = 1U, .handle = handle,
		.base = (uintptr_t)&fixture->page, .bytes = sizeof(fixture->page),
		.tag = BM_MEM_RESERVED,
		.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE,
	};
	assert(bootmem_reservation_receipt_mac(fixture->signer.secret,
		&fixture->receipt, offsetof(struct bootmem_reservation_receipt, mac),
		fixture->receipt.mac) == CB_SUCCESS);
	fixture->ops = (struct smm_invocation_save_state_ops) {
		.match_apmc_write = match, .read_rax = read_rax, .write_rax = write_rax,
		.context = &fixture->save_state,
		.context_size = sizeof(fixture->save_state),
	};
	fixture->policy =
		(struct payload_mm_authvar_presence_route_authority_policy) {
			.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POLICY_REVISION,
			.size = sizeof(fixture->policy),
			.prepare = authority_prepare, .commit = authority_commit,
			.abort = authority_abort, .dma_protected = authority_dma,
			.fail_stop = authority_fail_stop,
			.context = &fixture->authority_context,
			.context_size = sizeof(fixture->authority_context),
		};
	assert(payload_mm_authvar_presence_arm_provision(&fixture->arm,
		&fixture->composition, &fixture->instance, &fixture->evidence,
		protected_storage, &fixture->proof_context) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_route_session_provision(&fixture->session,
		&fixture->arm, &fixture->slot, &fixture->composition,
		&fixture->instance, &fixture->evidence, &fixture->topology,
		&fixture->ops, &fixture->policy, &fixture->binding,
		&fixture->verifier, &fixture->receipt, protected_storage,
		&fixture->proof_context) ==
		CB_SUCCESS);
}

static void run_scenario(uint32_t decision)
{
	struct integration_fixture fixture;
	uint64_t prepare_generation, decision_generation;

	setup(&fixture);
	prepare_generation = run_round(&fixture,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	assert(fixture.session.state ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DECISION_IDLE);
	assert(fixture.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND);
	decision_generation = run_round(&fixture, decision);
	assert(decision_generation > prepare_generation);
	assert(fixture.session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_CLOSED);
	if (decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT)
		assert(fixture.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_READY);
	else
		assert(fixture.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_ABORTED);
	assert(fixture.prepare_calls == 1U);
	assert(fixture.commit_calls ==
		(decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT));
	assert(fixture.abort_calls ==
		(decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT));
	assert(fixture.dma_calls == 2U);
	assert(!pthread_mutex_destroy(&fixture.handler_lock));
}

int main(void)
{
	run_scenario(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
	run_scenario(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
	return 0;
}
