/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_route_session.h>
#include <bootmem_reservation_receipt_internal.h>
#include <cpu/x86/apm.h>
#include <pthread.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static struct payload_mm_authvar_presence_route_session session;
static struct payload_mm_authvar_presence_arm arm;
static struct payload_mm_authvar_presence_transaction_slot slot;
static struct payload_mm_authvar_presence_transaction_page page
	__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE);
static uint8_t save_state_context[16];
struct callback_sentinel {
	uint64_t identity;
	uint8_t content[8];
};

#define AUTHORITY_CONTEXT_ID 0x617574686f726974ULL
#define PROOF_CONTEXT_ID 0x70726f6f662d6374ULL

static struct callback_sentinel authority_context = {
	.identity = AUTHORITY_CONTEXT_ID, .content = { 0xa5U, 0x5aU },
};
static struct callback_sentinel proof_context = {
	.identity = PROOF_CONTEXT_ID, .content = { 0x3cU, 0xc3U },
};
static uint64_t next_generation;
static uint64_t expected_claim_generation;
static bool prepare_fails;
static bool expect_route_ready_at_publication;
static bool mutate_policy_during_proof;
static unsigned int mutate_lower_object_during_proof;
static bool deny_protected_storage;
static bool mutate_late_lower_ops_context;
static bool mutate_runtime_proof_ops_context;
static bool mutate_arm_during_delegate_bind;
static unsigned int mutate_authority_context_during_proof;
static struct payload_mm_authvar_presence_route_authority_policy
	*policy_to_mutate;
static bool dispatch_at_publication;
static unsigned int consume_calls, claim_calls, complete_calls, eos_calls;
static const struct smm_invocation_loader_instance *hook_instance;
static struct smm_invocation_evidence *hook_evidence;
static const struct payload_mm_authvar_presence_transaction_binding
	*hook_binding;
static const struct payload_mm_authvar_presence_seed *hook_seed;
static bool concurrent_arrive;
static pthread_barrier_t arrival_barrier;
static pthread_mutex_t arrival_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t arrival_condition = PTHREAD_COND_INITIALIZER;
static bool hold_ap_arrival, ap_arrival_waiting, release_ap_arrival;
static pthread_mutex_t departure_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t departure_condition = PTHREAD_COND_INITIALIZER;
static unsigned int departure_count;
static bool block_bsp_poll, bsp_polling;
static bool reject_eos, mutate_session_at_eos;
static unsigned int mutate_failure_closure_in_callback;

static void run_round(
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence, uint32_t decision,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	const struct payload_mm_authvar_presence_seed *seed);

static void init_selection_receipt(
	struct smm_apmc_selection_receipt *receipt)
{
	*receipt = (struct smm_apmc_selection_receipt) {
		.revision = SMM_APMC_SELECTION_RECEIPT_REVISION,
		.size = sizeof(*receipt),
		.identity = (uint64_t)(uintptr_t)receipt,
		.generation = 1U,
		.descriptor = {
			.command = SMM_APMC_AUTHVAR_PRESENCE,
			.owner = SMM_APMC_OWNER_AUTHVAR_PRESENCE,
			.role = SMM_APMC_EXCLUSIVE,
			.binding_count = 1U,
			.reserved = true,
			.enabled = true,
		},
	};
}

static void expect_pre_unlock_ticket_corruption_death(
	const struct smm_invocation_loader_instance *instance,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	const struct payload_mm_authvar_presence_seed *seed)
{
	struct smm_invocation_entry_cause cause = {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
		.size = sizeof(cause),
		.loader_instance_nonce = instance->loader_instance_nonce,
		.lifecycle = instance->lifecycle,
		.command = SMM_APMC_AUTHVAR_PRESENCE, .recognized = 1U,
	};
	struct smm_invocation_entry_policy entry_policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(entry_policy), .max_polls = 100U,
	};
	struct smm_invocation_entry_ticket ticket = { 0 };
	struct smm_apmc_selection_receipt selection;
	int status;
	pid_t child = fork();

	assert(child >= 0);
	if (!child) {
		init_selection_receipt(&selection);
		memset(&page, 0, sizeof(page));
		page.request.binding = *binding;
		page.request.decision =
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE;
		page.request.seed = *seed;
		assert(payload_mm_authvar_presence_route_session_arrive(&session,
			&cause, &entry_policy, 0U, 0U, &ticket) == CB_SUCCESS);
		assert(payload_mm_authvar_presence_route_session_dispatch_locked(
			&session, &ticket, &selection) ==
			SMM_APMC_CONSUMED_SUCCESS);
		session.active_ticket.max_polls++;
		payload_mm_authvar_presence_route_session_prepare_lock_release(
			&session, &ticket);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status));
}

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

struct payload_mm_authvar_presence_arm *
platform_payload_mm_authvar_presence_arm(void)
{
	return &arm;
}

struct payload_mm_authvar_presence_transaction_slot *
smm_get_payload_mm_authvar_presence_transaction_slot(void)
{
	return &slot;
}

void payload_mm_authvar_presence_transaction_test_after_provision_claim(
	const struct payload_mm_authvar_presence_transaction_policy *policy)
{
	(void)policy;
}

void payload_mm_authvar_presence_transaction_test_after_dispatch_owner(
	uint32_t state)
{
	(void)state;
}

void payload_mm_authvar_presence_transaction_test_after_provision_publish(
	struct payload_mm_authvar_presence_transaction_slot *published_slot)
{
	assert(published_slot == &slot);
	if (dispatch_at_publication)
		run_round(hook_instance, hook_evidence,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE,
			hook_binding, hook_seed);
}

void payload_mm_authvar_presence_arm_test_before_verifier_claim(
	struct bootmem_reservation_receipt_authority *verifier)
{
	(void)verifier;
	if (expect_route_ready_at_publication)
		assert(session.state ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PREPARE_IDLE);
}

const struct smm_invocation_evidence *
smm_invocation_loader_composition_evidence(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_evidence *evidence)
{
	return composition &&
		composition->state == SMM_INVOCATION_LOADER_COMPOSITION_READY &&
		composition->evidence_identity == (uintptr_t)evidence ? evidence : NULL;
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_loader_instance *snapshot)
{
	if (!instance || instance->state != SMM_INVOCATION_LOADER_INSTANCE_READY)
		return CB_ERR;
	*snapshot = *instance;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_topology_read(
	const struct smm_invocation_topology *topology,
	struct smm_invocation_topology *snapshot)
{
	if (!topology || topology->state != SMM_INVOCATION_TOPOLOGY_READY)
		return CB_ERR;
	*snapshot = *topology;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_endpoint_validate(
	const struct lb_authvar_presence_endpoint *endpoint)
{
	return endpoint && endpoint->tag == LB_TAG_AUTHVAR_PRESENCE_ENDPOINT &&
		endpoint->size == sizeof(*endpoint) && endpoint->generation &&
		endpoint->trigger_address == APM_CNT &&
		endpoint->trigger_value == SMM_APMC_AUTHVAR_PRESENCE ?
		CB_SUCCESS : CB_ERR;
}

static bool protected_storage(void *context, const void *object, size_t size)
{
	assert(context == &proof_context);
	assert(proof_context.identity == PROOF_CONTEXT_ID);
	assert(proof_context.content[0] == 0x3cU &&
		proof_context.content[1] == 0xc3U);
	if (deny_protected_storage)
		return false;
	if (mutate_authority_context_during_proof == 1U &&
	    session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PROVISIONING &&
	    object == &session) {
		authority_context.content[0]++;
		mutate_authority_context_during_proof = 0U;
	}
	if (mutate_authority_context_during_proof == 2U &&
	    session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PROVISIONING &&
	    object == &session.callback_context) {
		authority_context.content[0]++;
		mutate_authority_context_during_proof = 0U;
	}
	if (mutate_authority_context_during_proof == 3U &&
	    session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PREPARE_IDLE) {
		session.authority_context[0]++;
		mutate_authority_context_during_proof = 0U;
	}
	if (mutate_late_lower_ops_context &&
	    session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PREPARE_IDLE) {
		save_state_context[0]++;
		mutate_late_lower_ops_context = false;
	}
	if (mutate_runtime_proof_ops_context &&
	    session.state >= PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DISPATCHING) {
		save_state_context[0]++;
		mutate_runtime_proof_ops_context = false;
	}
	if (mutate_arm_during_delegate_bind &&
	    object == &session.callback_context) {
		arm.composition = NULL;
		arm.sealed_composition = NULL;
		mutate_arm_during_delegate_bind = false;
	}
	if (mutate_policy_during_proof && policy_to_mutate) {
		policy_to_mutate->commit = policy_to_mutate->abort;
		policy_to_mutate->size++;
		mutate_policy_during_proof = false;
	}
	if (mutate_lower_object_during_proof) {
		if (mutate_lower_object_during_proof & 1U)
			arm.composition = NULL;
		if (mutate_lower_object_during_proof & 2U)
			arm.sealed_composition = NULL;
		if (mutate_lower_object_during_proof & 4U)
			slot.state = 1U;
		if (mutate_lower_object_during_proof & 8U)
			save_state_context[0]++;
		mutate_lower_object_during_proof = 0U;
	}
	return object && size;
}

static void __noreturn redirected_fail_stop(void *context)
{
	(void)context;
	_exit(77);
}

static void assert_authority_context(void *context)
{
	assert(context == session.authority_context);
	assert(!memcmp(context, &authority_context, sizeof(authority_context)));
}

static void assert_authority_context_copy(void *context)
{
	assert(context);
	assert(!memcmp(context, &authority_context, sizeof(authority_context)));
}

static enum cb_err prepare(void *context,
	const struct payload_mm_authvar_presence_seed *seed, uint64_t generation)
{
	assert_authority_context(context);
	if (mutate_failure_closure_in_callback == 1U) {
		session.failure_callback = session.sealed_failure_callback =
			redirected_fail_stop;
		session.failure_context[0]++;
		session.sealed_failure_context[0]++;
		session.authority_context[0]++;
		session.sealed_authority_context[0]++;
		save_state_context[0]++;
	}
	return !prepare_fails && seed && seed->endpoint.generation == generation ?
		CB_SUCCESS : CB_ERR;
}

static enum cb_err decide(void *context, uint64_t generation)
{
	assert_authority_context(context);
	if (mutate_failure_closure_in_callback == 2U) {
		session.failure_callback = session.sealed_failure_callback =
			redirected_fail_stop;
		session.failure_context[0]++;
		session.sealed_failure_context[0]++;
		session.authority_context[0]++;
		session.sealed_authority_context[0]++;
		save_state_context[0]++;
	}
	return generation ? CB_SUCCESS : CB_ERR;
}

static enum cb_err commit(void *context, uint64_t generation)
{
	return decide(context, generation);
}

static enum cb_err abort_decision(void *context, uint64_t generation)
{
	return decide(context, generation);
}

static bool dma_protected(void *context, uint64_t base, uint64_t size)
{
	assert_authority_context(context);
	if (mutate_failure_closure_in_callback == 3U) {
		session.failure_callback = session.sealed_failure_callback =
			redirected_fail_stop;
		session.failure_context[0]++;
		session.sealed_failure_context[0]++;
		session.authority_context[0]++;
		session.sealed_authority_context[0]++;
		save_state_context[0]++;
	}
	return base == (uintptr_t)&page && size == sizeof(page);
}

static void __noreturn fail_stop(void *context)
{
	assert_authority_context_copy(context);
	__builtin_trap();
}

static enum smm_invocation_match match(void *context, uint32_t cpu,
	uint8_t command)
{
	(void)context;
	return cpu == 0U && command == SMM_APMC_AUTHVAR_PRESENCE ?
		SMM_INVOCATION_MATCHED : SMM_INVOCATION_NOT_MATCHED;
}

static enum cb_err read_value(void *context, uint32_t cpu, uint64_t *value)
{
	(void)context;
	(void)cpu;
	*value = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_SENTINEL;
	return CB_SUCCESS;
}

static enum cb_err write_value(void *context, uint32_t cpu, uint64_t value)
{
	(void)context;
	return cpu == 0U && value ? CB_SUCCESS : CB_ERR;
}

enum cb_err smm_invocation_evidence_claim(
	struct smm_invocation_evidence *evidence, uint8_t command,
	uint64_t sentinel, const struct smm_invocation_save_state_ops *ops,
	struct smm_invocation_token *token)
{
	(void)ops;
	claim_calls++;
	if (expected_claim_generation)
		assert(next_generation == expected_claim_generation);
	if (command != SMM_APMC_AUTHVAR_PRESENCE ||
	    sentinel != PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_SENTINEL)
		return CB_ERR;
	*token = (struct smm_invocation_token) {
		.revision = SMM_INVOCATION_TOKEN_REVISION,
		.size = sizeof(*token), .initiator_cpu = 0U,
		.active_cpus = evidence->active_cpus,
		.smi_generation = next_generation,
		.rendezvous_generation = next_generation,
		.rendezvous_digest = { 1U }, .bsp = 1U,
	};
	evidence->state = SMM_INVOCATION_CLAIMED;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_evidence_publish_and_request_close(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_token *token, uint64_t value,
	const struct smm_invocation_save_state_ops *ops)
{
	(void)ops;
	complete_calls++;
	if (token->smi_generation != next_generation || !value)
		return CB_ERR;
	evidence->state = SMM_INVOCATION_CLOSING;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_entry_arrive(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	struct smm_invocation_loader_instance_nonce expected_nonce,
	uint8_t expected_command, uint32_t cpu, uint32_t initial_apic_id,
	struct smm_invocation_entry_ticket *ticket)
{
	(void)initial_apic_id;
	if (!cause || cause->command != expected_command || !cause->recognized ||
	    !smm_invocation_loader_instance_nonce_equal(
		cause->loader_instance_nonce, expected_nonce))
		return CB_ERR;
	if (concurrent_arrive) {
		const int status = pthread_barrier_wait(&arrival_barrier);

		assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
		if (cpu == 1U && hold_ap_arrival) {
			assert(!pthread_mutex_lock(&arrival_lock));
			ap_arrival_waiting = true;
			assert(!pthread_cond_broadcast(&arrival_condition));
			while (!release_ap_arrival)
				assert(!pthread_cond_wait(&arrival_condition,
					&arrival_lock));
			assert(!pthread_mutex_unlock(&arrival_lock));
		}
	} else {
		++next_generation;
	}
	*ticket = (struct smm_invocation_entry_ticket) {
		.generation = next_generation, .loader_instance_nonce = expected_nonce,
		.cpu = cpu, .lifecycle = cause->lifecycle,
		.max_polls = policy->max_polls, .command = expected_command,
	};
	if (!concurrent_arrive)
		evidence->state = SMM_INVOCATION_COLLECTING;
	return CB_SUCCESS;
}

struct arrival_call {
	const struct smm_invocation_loader_instance *instance;
	struct smm_invocation_entry_ticket ticket;
	uint32_t cpu;
};

struct departure_call {
	struct smm_invocation_entry_ticket *ticket;
	enum payload_mm_authvar_presence_route_departure result;
};

static void *arrival_thread(void *argument)
{
	struct arrival_call *call = argument;
	struct smm_invocation_entry_cause cause = {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
		.size = sizeof(cause),
		.loader_instance_nonce = call->instance->loader_instance_nonce,
		.lifecycle = call->instance->lifecycle,
		.command = SMM_APMC_AUTHVAR_PRESENCE, .recognized = 1U,
	};
	struct smm_invocation_entry_policy policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(policy), .max_polls = 100U,
	};

	assert(payload_mm_authvar_presence_route_session_arrive(&session, &cause,
		&policy, call->cpu, call->cpu, &call->ticket) == CB_SUCCESS);
	return NULL;
}

static void *departure_thread(void *argument)
{
	struct departure_call *call = argument;

	call->result = payload_mm_authvar_presence_route_session_depart(&session,
		call->ticket);
	return NULL;
}

enum cb_err smm_invocation_entry_depart(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_entry_ticket *ticket)
{
	(void)ticket;
	if (block_bsp_poll) {
		assert(!pthread_mutex_lock(&departure_lock));
		departure_count++;
		if (departure_count == evidence->active_cpus) {
			evidence->state = SMM_INVOCATION_READY;
			assert(!pthread_cond_broadcast(&departure_condition));
		}
		assert(!pthread_mutex_unlock(&departure_lock));
	} else {
		evidence->state = SMM_INVOCATION_READY;
	}
	return CB_SUCCESS;
}

bool smm_invocation_entry_eos_ready(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_entry_ticket *ticket)
{
	(void)ticket;
	eos_calls++;
	if (mutate_session_at_eos)
		session.identity++;
	return !reject_eos && evidence->state == SMM_INVOCATION_READY;
}

uint32_t smm_invocation_evidence_phase(
	const struct smm_invocation_evidence *evidence)
{
	if (block_bsp_poll && evidence->state != SMM_INVOCATION_READY) {
		assert(!pthread_mutex_lock(&departure_lock));
		bsp_polling = true;
		assert(!pthread_cond_broadcast(&departure_condition));
		while (evidence->state != SMM_INVOCATION_READY)
			assert(!pthread_cond_wait(&departure_condition,
				&departure_lock));
		assert(!pthread_mutex_unlock(&departure_lock));
	}
	return evidence->state;
}

static void assert_terminal_scrubbed(void)
{
	struct payload_mm_authvar_presence_route_session clean = session;
	struct payload_mm_authvar_presence_route_session zero = { 0 };

	assert(clean.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_CLOSED);
	clean.state = PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_EMPTY;
	assert(!memcmp(&clean, &zero, sizeof(clean)));
}

static void expect_idle_corruption_death(unsigned int mutation,
	const struct smm_invocation_loader_instance *instance)
{
	struct smm_invocation_entry_ticket ticket = {
		.generation = 1U,
		.loader_instance_nonce = instance->loader_instance_nonce,
		.lifecycle = instance->lifecycle, .max_polls = 100U,
		.command = SMM_APMC_AUTHVAR_PRESENCE,
	};
	struct smm_apmc_selection_receipt selection;
	int status;
	pid_t child = fork();

	assert(child >= 0);
	if (!child) {
		init_selection_receipt(&selection);
		switch (mutation) {
		case 0:
			session.active_invocation_generation = 1U;
			break;
		case 1:
			session.decision = 1U;
			break;
		case 2:
			session.completion_value = 1U;
			break;
		case 3:
			session.token.revision = 1U;
			break;
		case 4:
			session.invocation.revision = 1U;
			break;
		case 5:
			session.active_ticket.generation = 1U;
			break;
		case 6:
			session.sealed_active_ticket.generation = 1U;
			break;
		case 7:
			session.round = UINT32_MAX;
			break;
		case 8:
			session.prepare_invocation_generation = 1U;
			break;
		case 9:
			session.prepare_invocation_generation = 0U;
			break;
		case 10:
			session.prepare_invocation_generation = UINT64_MAX;
			break;
		case 11:
			session.round = UINT32_MAX;
			break;
		default:
			__builtin_trap();
		}
		(void)payload_mm_authvar_presence_route_session_dispatch_locked(
			&session, &ticket, &selection);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status));
}

static void assert_ack_mutations_rejected(
	const struct payload_mm_authvar_presence_transaction_binding *binding)
{
	const struct payload_mm_authvar_presence_transaction_slot saved_slot = slot;
	const struct payload_mm_authvar_presence_transaction_page saved_page = page;

#define REJECT_ACK_MUTATION(statement) do { \
	slot = saved_slot; page = saved_page; statement; \
	assert(!payload_mm_authvar_presence_transaction_dispatch_ack_valid(&slot, \
		binding, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE, &page)); \
} while (0)
	REJECT_ACK_MUTATION(slot.dispatch_owner = 1U);
	REJECT_ACK_MUTATION(slot.state ^= 0x80000000U);
	REJECT_ACK_MUTATION(slot.binding.generation++);
	REJECT_ACK_MUTATION(slot.policy.context = NULL);
	REJECT_ACK_MUTATION(slot.failure.callback = NULL);
	REJECT_ACK_MUTATION(slot.page_base++);
	REJECT_ACK_MUTATION(page.ack.transport_status ^= 1U);
	REJECT_ACK_MUTATION(page.request.decision = 1U);
	REJECT_ACK_MUTATION(page.reserved[0] = 1U);
#undef REJECT_ACK_MUTATION
	slot = saved_slot;
	page = saved_page;
}

static void assert_terminal_ack_mutations_rejected(
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision)
{
	const struct payload_mm_authvar_presence_transaction_slot saved_slot = slot;
	const struct payload_mm_authvar_presence_transaction_page saved_page = page;

	assert(payload_mm_authvar_presence_transaction_dispatch_ack_valid(&slot,
		binding, decision, &page));
#define REJECT_TERMINAL_MUTATION(statement) do { \
	slot = saved_slot; page = saved_page; statement; \
	assert(!payload_mm_authvar_presence_transaction_dispatch_ack_valid(&slot, \
		binding, decision, &page)); \
} while (0)
	REJECT_TERMINAL_MUTATION(slot.state ^= 0x80000000U);
	REJECT_TERMINAL_MUTATION(slot.dispatch_owner = 1U);
	REJECT_TERMINAL_MUTATION(slot.ack_published = 0U);
	REJECT_TERMINAL_MUTATION(slot.reserved[0] = 1U);
	REJECT_TERMINAL_MUTATION(slot.policy.revision = 1U);
	REJECT_TERMINAL_MUTATION(slot.context[0] = 1U);
	REJECT_TERMINAL_MUTATION(slot.failure.generation = 1U);
	REJECT_TERMINAL_MUTATION(((uint8_t *)&slot.page_verifier)[0] = 1U);
	REJECT_TERMINAL_MUTATION(((uint8_t *)&slot.page_receipt)[0] = 1U);
	REJECT_TERMINAL_MUTATION(slot.page_base = 1U);
	REJECT_TERMINAL_MUTATION(slot.page_size = 1U);
	REJECT_TERMINAL_MUTATION(slot.binding.capability[0] ^= 1U);
	REJECT_TERMINAL_MUTATION(page.ack.decision ^= 1U);
	REJECT_TERMINAL_MUTATION(page.request.decision = 1U);
	REJECT_TERMINAL_MUTATION(page.reserved[0] = 1U);
#undef REJECT_TERMINAL_MUTATION
	slot = saved_slot;
	page = saved_page;
}

enum cb_err smm_invocation_evidence_ticket_fail(
	struct smm_invocation_evidence *evidence, uint64_t generation)
{
	(void)evidence;
	(void)generation;
	return CB_ERR;
}

enum smm_apmc_dispatch_result smm_apmc_command_consume(uint8_t command,
	enum smm_apmc_owner owner, struct smm_apmc_selection_receipt *selection)
{
	consume_calls++;
	if (command != SMM_APMC_AUTHVAR_PRESENCE ||
	    owner != SMM_APMC_OWNER_AUTHVAR_PRESENCE ||
	    selection->descriptor.command != command ||
	    selection->descriptor.owner != owner)
		return SMM_APMC_CONSUMED_REJECT;
	memset(selection, 0, sizeof(*selection));
	return SMM_APMC_CONSUMED_SUCCESS;
}

static void run_round(
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence,
	uint32_t decision,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	const struct payload_mm_authvar_presence_seed *seed)
{
	struct smm_invocation_entry_cause cause = {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
		.size = sizeof(cause),
		.loader_instance_nonce = instance->loader_instance_nonce,
		.lifecycle = instance->lifecycle,
		.command = SMM_APMC_AUTHVAR_PRESENCE, .recognized = 1U,
	};
	struct smm_invocation_entry_policy entry_policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(entry_policy), .max_polls = 100U,
	};
	struct smm_invocation_entry_ticket ticket = { 0 };
	struct smm_apmc_selection_receipt selection;

	(void)evidence;
	init_selection_receipt(&selection);
	memset(&page, 0, sizeof(page));
	page.request.binding = *binding;
	page.request.decision = decision;
	if (decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE)
		page.request.seed = *seed;
	assert(payload_mm_authvar_presence_route_session_arrive(&session, &cause,
		&entry_policy, 0U, 0U, &ticket) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_route_session_dispatch_locked(&session,
		&ticket, &selection) == SMM_APMC_CONSUMED_SUCCESS);
	assert(!selection.descriptor.command && !selection.descriptor.owner);
	payload_mm_authvar_presence_route_session_prepare_lock_release(&session,
		&ticket);
	assert(payload_mm_authvar_presence_route_session_depart(&session, &ticket) ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_BSP_EOS_CONSUMED);
}

int main(void)
{
	int child_status;
	pid_t child;
	struct smm_invocation_loader_composition composition = { 0 };
	struct smm_invocation_loader_instance instance = { 0 };
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_topology topology = { 0 };
	struct smm_invocation_save_state_ops ops = {
		.match_apmc_write = match, .read_value = read_value,
		.write_value = write_value, .context = save_state_context,
		.context_size = sizeof(save_state_context),
	};
	struct payload_mm_authvar_presence_route_authority_policy policy = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POLICY_REVISION,
		.size = sizeof(policy), .prepare = prepare, .commit = commit,
		.abort = abort_decision, .dma_protected = dma_protected,
		.fail_stop = fail_stop, .context = &authority_context,
		.context_size = sizeof(authority_context),
	};
	struct payload_mm_authvar_presence_route_authority_policy overlap_policy;
	struct smm_invocation_save_state_ops oversized_ops;
	struct payload_mm_authvar_presence_transaction_binding binding = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION,
		.size = sizeof(binding), .generation = 9U, .transaction_id = 10U,
		.nonce = 11U, .maximum_cpus = 1U, .capability = { 1U },
	};
	struct payload_mm_authvar_presence_seed seed = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION,
		.size = sizeof(seed),
		.endpoint = {
			.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT,
			.size = sizeof(struct lb_authvar_presence_endpoint),
			.revision = LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION,
			.header_size = sizeof(struct lb_authvar_presence_endpoint),
			.flags = LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS, .generation = 9U,
			.communication_base = 0x100000U,
			.communication_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
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
			.base = 0x100000U,
			.bytes = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE,
			.generation = 9U, .tag = BM_MEM_RESERVED,
		}, .capability = { 1U },
	};
	struct bootmem_reservation_receipt_authority signer = { 0 }, verifier = { 0 };
	struct bootmem_reservation_receipt receipt = { 0 };
	struct bootmem_aligned_reservation_handle handle = { .opaque = { 1U, 2U } };
	uint8_t secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE] = { 1U };
	uint8_t delegate_context = 0U;

	instance.state = SMM_INVOCATION_LOADER_INSTANCE_READY;
	instance.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION;
	instance.size = sizeof(instance);
	instance.lifecycle = 1U;
	instance.loader_instance_nonce.low = 1U;
	composition.state = SMM_INVOCATION_LOADER_COMPOSITION_READY;
	composition.owner_attempt = 1U;
	composition.evidence_identity = (uintptr_t)&evidence;
	evidence.state = SMM_INVOCATION_READY;
	evidence.active_cpus = 1U;
	evidence.loader_lifecycle = 1U;
	evidence.loader_instance_nonce = instance.loader_instance_nonce;
	topology.state = SMM_INVOCATION_TOPOLOGY_READY;
	topology.revision = SMM_INVOCATION_TOPOLOGY_REVISION;
	topology.size = sizeof(topology);
	topology.active_cpus = 1U;
	assert(bootmem_reservation_receipt_provision(&signer, &verifier, secret,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, binding.generation,
		&handle) == CB_SUCCESS);
	receipt = (struct bootmem_reservation_receipt) {
		.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION,
		.size = sizeof(receipt),
		.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
		.generation = binding.generation, .sequence = 1U, .handle = handle,
		.base = (uintptr_t)&page, .bytes = sizeof(page),
		.tag = BM_MEM_RESERVED,
		.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE,
	};
	assert(bootmem_reservation_receipt_mac(signer.secret, &receipt,
		offsetof(struct bootmem_reservation_receipt, mac), receipt.mac) ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_arm_provision(&arm, &composition,
		&instance, &evidence, protected_storage, &proof_context) == CB_SUCCESS);
	child = fork();
	assert(child >= 0);
	if (!child) {
		assert(payload_mm_authvar_presence_arm_protection_delegate_bind(&arm,
			protected_storage, &proof_context, protected_storage, &delegate_context,
			sizeof(delegate_context)) == CB_SUCCESS);
		assert(payload_mm_authvar_presence_arm_protection_delegate_bind(&arm,
			protected_storage, &proof_context, protected_storage, &delegate_context,
			sizeof(delegate_context)) == CB_ERR);
		assert(arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	overlap_policy = policy;
	overlap_policy.context = &session;
	overlap_policy.context_size = 1U;
	assert(payload_mm_authvar_presence_route_session_provision(&session, &arm,
		&slot, &composition, &instance, &evidence, &topology, &ops,
		&overlap_policy, &binding, &verifier, &receipt,
		protected_storage, &proof_context) == CB_ERR);
	assert(session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_EMPTY);
	oversized_ops = ops;
	oversized_ops.context_size = SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX + 1U;
	assert(payload_mm_authvar_presence_route_session_provision(&session, &arm,
		&slot, &composition, &instance, &evidence, &topology, &oversized_ops,
		&policy, &binding, &verifier, &receipt, protected_storage,
		&proof_context) ==
		CB_ERR);
	assert(session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_EMPTY);
	child = fork();
	assert(child >= 0);
	if (!child) {
		deny_protected_storage = true;
		assert(payload_mm_authvar_presence_route_session_provision(&session,
			&arm, &slot, &composition, &instance, &evidence, &topology,
			&ops, &policy, &binding, &verifier, &receipt,
			protected_storage, &proof_context) == CB_ERR);
		assert(session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POISONED);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	child = fork();
	assert(child >= 0);
	if (!child) {
		pthread_t threads[2];
		pthread_t bsp_departure;
		struct arrival_call calls[2] = {
			{ .instance = &instance, .cpu = 0U },
			{ .instance = &instance, .cpu = 1U },
		};
		struct departure_call bsp_call = { .ticket = &calls[0].ticket };
		struct smm_apmc_selection_receipt selection;

		memset(&session, 0, sizeof(session));
		memset(&arm, 0, sizeof(arm));
		memset(&slot, 0, sizeof(slot));
		evidence.state = SMM_INVOCATION_READY;
		evidence.active_cpus = 2U;
		topology.active_cpus = 2U;
		topology.initial_apic_ids[1] = 1U;
		binding.maximum_cpus = 2U;
		assert(payload_mm_authvar_presence_arm_provision(&arm, &composition,
			&instance, &evidence, protected_storage, &proof_context) ==
			CB_SUCCESS);
		assert(payload_mm_authvar_presence_route_session_provision(&session,
			&arm, &slot, &composition, &instance, &evidence, &topology,
			&ops, &policy, &binding, &verifier, &receipt,
			protected_storage, &proof_context) == CB_SUCCESS);
		next_generation = 1U;
		evidence.state = SMM_INVOCATION_COLLECTING;
		concurrent_arrive = true;
		hold_ap_arrival = true;
		ap_arrival_waiting = false;
		release_ap_arrival = false;
		assert(!pthread_barrier_init(&arrival_barrier, NULL, 2U));
		assert(!pthread_create(&threads[0], NULL, arrival_thread, &calls[0]));
		assert(!pthread_create(&threads[1], NULL, arrival_thread, &calls[1]));
		assert(!pthread_join(threads[0], NULL));
		assert(!pthread_mutex_lock(&arrival_lock));
		while (!ap_arrival_waiting)
			assert(!pthread_cond_wait(&arrival_condition, &arrival_lock));
		assert(!pthread_mutex_unlock(&arrival_lock));
		memset(&page, 0, sizeof(page));
		init_selection_receipt(&selection);
		page.request.binding = binding;
		page.request.decision =
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE;
		page.request.seed = seed;
		assert(payload_mm_authvar_presence_route_session_dispatch_locked(
			&session, &calls[0].ticket, &selection) ==
			SMM_APMC_CONSUMED_SUCCESS);
		assert(!pthread_mutex_lock(&arrival_lock));
		release_ap_arrival = true;
		assert(!pthread_cond_broadcast(&arrival_condition));
		assert(!pthread_mutex_unlock(&arrival_lock));
		assert(!pthread_join(threads[1], NULL));
		assert(!pthread_barrier_destroy(&arrival_barrier));
		assert(calls[0].ticket.generation == calls[1].ticket.generation);
		assert(calls[0].ticket.cpu != calls[1].ticket.cpu);
		payload_mm_authvar_presence_route_session_prepare_lock_release(
			&session, &calls[0].ticket);
		departure_count = 0U;
		bsp_polling = false;
		block_bsp_poll = true;
		assert(!pthread_create(&bsp_departure, NULL, departure_thread,
			&bsp_call));
		assert(!pthread_mutex_lock(&departure_lock));
		while (!bsp_polling)
			assert(!pthread_cond_wait(&departure_condition,
				&departure_lock));
		assert(!pthread_mutex_unlock(&departure_lock));
		assert(payload_mm_authvar_presence_route_session_depart(&session,
			&calls[1].ticket) ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PARTICIPANT_DEPARTED);
		assert(!pthread_join(bsp_departure, NULL));
		assert(bsp_call.result ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_BSP_EOS_CONSUMED);
		assert(session.state ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DECISION_IDLE);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	const unsigned int proof_mutations[] = { 1U, 2U, 3U, 4U, 8U };
	for (size_t index = 0; index < ARRAY_SIZE(proof_mutations); ++index) {
		child = fork();
		assert(child >= 0);
		if (!child) {
			mutate_lower_object_during_proof = proof_mutations[index];
			assert(payload_mm_authvar_presence_route_session_provision(
				&session, &arm, &slot, &composition, &instance,
				&evidence, &topology, &ops, &policy, &binding,
				&verifier, &receipt, protected_storage,
				&proof_context) == CB_ERR);
			assert(session.state ==
				PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POISONED);
			_exit(0);
		}
		assert(waitpid(child, &child_status, 0) == child);
		if (proof_mutations[index] == 8U) {
			assert(WIFSIGNALED(child_status));
		} else {
			assert(WIFEXITED(child_status) &&
				WEXITSTATUS(child_status) == 0);
		}
	}
	for (unsigned int proof_position = 1U; proof_position <= 3U;
	     ++proof_position) {
		child = fork();
		assert(child >= 0);
		if (!child) {
			mutate_authority_context_during_proof = proof_position;
			assert(payload_mm_authvar_presence_route_session_provision(
				&session, &arm, &slot, &composition, &instance,
				&evidence, &topology, &ops, &policy, &binding,
				&verifier, &receipt, protected_storage,
				&proof_context) == CB_ERR);
			assert(session.state ==
				PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POISONED);
			_exit(0);
		}
		assert(waitpid(child, &child_status, 0) == child);
		if (proof_position == 1U) {
			assert(WIFEXITED(child_status) &&
				WEXITSTATUS(child_status) == 0);
		} else {
			assert(WIFSIGNALED(child_status));
		}
	}
	child = fork();
	assert(child >= 0);
	if (!child) {
		mutate_arm_during_delegate_bind = true;
		(void)payload_mm_authvar_presence_route_session_provision(&session,
			&arm, &slot, &composition, &instance, &evidence, &topology,
			&ops, &policy, &binding, &verifier, &receipt,
			protected_storage, &proof_context);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFSIGNALED(child_status));
	child = fork();
	assert(child >= 0);
	if (!child) {
		mutate_late_lower_ops_context = true;
		(void)payload_mm_authvar_presence_route_session_provision(&session,
			&arm, &slot, &composition, &instance, &evidence, &topology,
			&ops, &policy, &binding, &verifier, &receipt,
			protected_storage, &proof_context);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFSIGNALED(child_status));
	child = fork();
	assert(child >= 0);
	if (!child) {
		hook_instance = &instance;
		hook_evidence = &evidence;
		hook_binding = &binding;
		hook_seed = &seed;
		expect_route_ready_at_publication = true;
		dispatch_at_publication = true;
		assert(payload_mm_authvar_presence_route_session_provision(&session,
			&arm, &slot, &composition, &instance, &evidence, &topology,
			&ops, &policy, &binding, &verifier, &receipt,
			protected_storage, &proof_context) == CB_SUCCESS);
		assert(session.state ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DECISION_IDLE);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	child = fork();
	assert(child >= 0);
	if (!child) {
		policy_to_mutate = &policy;
		mutate_policy_during_proof = true;
		assert(payload_mm_authvar_presence_route_session_provision(&session,
			&arm, &slot, &composition, &instance, &evidence, &topology,
			&ops, &policy, &binding, &verifier, &receipt,
			protected_storage, &proof_context) == CB_ERR);
		assert(session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POISONED);
		assert(!session.failure_callback && !session.binding.generation);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	expect_route_ready_at_publication = true;
	assert(payload_mm_authvar_presence_route_session_provision(&session, &arm,
		&slot, &composition, &instance, &evidence, &topology, &ops, &policy,
		&binding, &verifier, &receipt, protected_storage, &proof_context) ==
		CB_SUCCESS);
	expect_route_ready_at_publication = false;
	expect_pre_unlock_ticket_corruption_death(&instance, &binding, &seed);
	for (unsigned int mutation = 0; mutation <= 8U; ++mutation)
		expect_idle_corruption_death(mutation, &instance);
	for (unsigned int callback = 1U; callback <= 3U; callback += 2U) {
		child = fork();
		assert(child >= 0);
		if (!child) {
			mutate_failure_closure_in_callback = callback;
			run_round(&instance, &evidence,
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE,
				&binding, &seed);
			_exit(0);
		}
		assert(waitpid(child, &child_status, 0) == child);
		assert(WIFSIGNALED(child_status));
	}
	child = fork();
	assert(child >= 0);
	if (!child) {
		prepare_fails = true;
		mutate_failure_closure_in_callback = 2U;
		run_round(&instance, &evidence,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE,
			&binding, &seed);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFSIGNALED(child_status));
	child = fork();
	assert(child >= 0);
	if (!child) {
		mutate_runtime_proof_ops_context = true;
		run_round(&instance, &evidence,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE,
			&binding, &seed);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFSIGNALED(child_status));
	child = fork();
	assert(child >= 0);
	if (!child) {
		run_round(&instance, &evidence, UINT32_MAX, &binding, &seed);
		assert(session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_CLOSED);
		assert(arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_ABORTED);
		assert_terminal_scrubbed();
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	child = fork();
	assert(child >= 0);
	if (!child) {
		prepare_fails = true;
		run_round(&instance, &evidence,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE,
			&binding, &seed);
		assert(session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_CLOSED);
		assert(arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_ABORTED);
		assert_terminal_scrubbed();
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	next_generation = 10U;
	run_round(&instance, &evidence,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE, &binding, &seed);
	assert(session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DECISION_IDLE);
	for (unsigned int mutation = 9U; mutation <= 11U; ++mutation)
		expect_idle_corruption_death(mutation, &instance);
	child = fork();
	assert(child >= 0);
	if (!child) {
		mutate_failure_closure_in_callback = 2U;
		run_round(&instance, &evidence,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT, &binding, &seed);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFSIGNALED(child_status));
	for (unsigned int stale = 0U; stale < 2U; ++stale) {
		child = fork();
		assert(child >= 0);
		if (!child) {
			expected_claim_generation =
				session.prepare_invocation_generation - stale;
			next_generation = expected_claim_generation - 1U;
			run_round(&instance, &evidence,
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT,
				&binding, &seed);
			_exit(0);
		}
		assert(waitpid(child, &child_status, 0) == child);
		assert(WIFSIGNALED(child_status));
	}
	assert(payload_mm_authvar_presence_transaction_dispatch_ack_valid(&slot,
		&binding, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE, &page));
	assert_ack_mutations_rejected(&binding);
	page.reserved[0] = 1U;
	assert(!payload_mm_authvar_presence_transaction_dispatch_ack_valid(&slot,
		&binding, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE, &page));
	page.reserved[0] = 0U;
	slot.context[sizeof(slot.context) - 1U] = 1U;
	assert(!payload_mm_authvar_presence_transaction_dispatch_ack_valid(&slot,
		&binding, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE, &page));
	slot.context[sizeof(slot.context) - 1U] = 0U;
	slot.failure.context[sizeof(slot.failure.context) - 1U] = 1U;
	assert(!payload_mm_authvar_presence_transaction_dispatch_ack_valid(&slot,
		&binding, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE, &page));
	slot.failure.context[sizeof(slot.failure.context) - 1U] = 0U;
	slot.ack_published = 0U;
	assert(!payload_mm_authvar_presence_transaction_dispatch_ack_valid(&slot,
		&binding, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE, &page));
	slot.ack_published = 1U;
	child = fork();
	assert(child >= 0);
	if (!child) {
		reject_eos = true;
		run_round(&instance, &evidence,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT, &binding, &seed);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFSIGNALED(child_status));
	child = fork();
	assert(child >= 0);
	if (!child) {
		mutate_session_at_eos = true;
		run_round(&instance, &evidence,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT, &binding, &seed);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFSIGNALED(child_status));
	child = fork();
	assert(child >= 0);
	if (!child) {
		run_round(&instance, &evidence,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT, &binding, &seed);
		assert(session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_CLOSED);
		assert(arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_ABORTED);
		assert_terminal_ack_mutations_rejected(&binding,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
		assert_terminal_scrubbed();
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	run_round(&instance, &evidence,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT, &binding, &seed);
	assert(session.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_CLOSED);
	assert(arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_READY);
	assert_terminal_ack_mutations_rejected(&binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
	assert_terminal_scrubbed();
	assert(consume_calls == 2U);
	assert(claim_calls == 2U);
	assert(complete_calls == 2U);
	assert(eos_calls == 2U);
	return 0;
}
