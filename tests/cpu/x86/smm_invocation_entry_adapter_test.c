/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cpu/intel/em64t100_save_state.h>
#include <cpu/intel/em64t101_save_state.h>
#include <cpu/intel/smm_invocation_adapter.h>
#include <cpu/x86/smm_invocation_entry.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define REV100 0x30100U
#define REV101 0x30101U
#define EXACT_IO 0x00b20003U
#define TEST_ADMISSION_BUSY (1U << 7)
#define TEST_ADMISSION_CONSUMED (1U << 8)
#define TEST_SHUTDOWN_REQUESTED (1U << 9)
#define TEST_REENTRY_DETECTED (1U << 10)
#define NONCE(value) ((struct smm_invocation_loader_instance_nonce) { \
	.low = (value), .high = (value) ^ 0xa55aa55aa55aa55aULL })
#define TEST_CLOSE_REQUESTED (1U << 11)
#define TEST_ADMISSION_NONCE_ONE (1U << 12)

void smm_invocation_entry_test_ack_wait(
	struct smm_invocation_evidence *evidence, uint64_t generation,
	uint32_t cpu, const struct smm_invocation_entry_policy *policy);

static uint32_t evidence_hook_point;
static uint32_t evidence_hook_entered;
static uint32_t evidence_hook_release = 1;
static uint32_t evidence_hook_point_secondary;
static uint32_t evidence_hook_entered_secondary;
static uint32_t evidence_hook_release_secondary = 1;

void smm_invocation_evidence_test_hook(uint32_t point)
{
	uint32_t expected = point;

	if (!__atomic_compare_exchange_n(&evidence_hook_point, &expected, 0U,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		expected = point;
		if (!__atomic_compare_exchange_n(&evidence_hook_point_secondary,
			&expected, 0U, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			return;
		__atomic_store_n(&evidence_hook_entered_secondary, 1U,
			__ATOMIC_RELEASE);
		while (!__atomic_load_n(&evidence_hook_release_secondary,
			__ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
	} else {
		__atomic_store_n(&evidence_hook_entered, 1U, __ATOMIC_RELEASE);
		while (!__atomic_load_n(&evidence_hook_release, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
	}
}

static uint32_t adapter_hook_point;
static em64t101_smm_state_save_area_t *adapter_hook_state;
static struct intel_smm_invocation_adapter *adapter_hook_adapter;
static uintptr_t adapter_hook_redirect;
static uint32_t adapter_hook_revision;

void intel_smm_invocation_adapter_test_hook(uint32_t point)
{
	if (point != adapter_hook_point)
		return;
	if (adapter_hook_revision)
		adapter_hook_adapter->expected_revision = adapter_hook_revision;
	else if (adapter_hook_redirect)
		adapter_hook_adapter->nodes[0].save_state = adapter_hook_redirect;
	else
		adapter_hook_state->io_misc_info ^= 1U << 8;
}

static uint8_t fail_stop_exit_code;
static uint32_t fail_stop_thread_exit;
static uint32_t fail_stop_calls;

void __noreturn smm_invocation_platform_fail_stop(void)
{
	const uint32_t calls = __atomic_add_fetch(&fail_stop_calls, 1U,
		__ATOMIC_RELAXED);

	if (fail_stop_thread_exit)
		pthread_exit(NULL);
	if (fail_stop_exit_code)
		_exit(calls == 1U ? fail_stop_exit_code :
			(uint8_t)(fail_stop_exit_code + 1U));
	abort();
}

static uint32_t entry_hook_point;
static uint32_t entry_hook_cpu = UINT32_MAX;
static uint32_t entry_hook_entered;
static uint32_t entry_hook_release = 1;
static uint32_t entry_barrier_target;
static uint32_t entry_barrier_count;

void smm_invocation_entry_test_hook(uint32_t point, uint32_t cpu)
{
	if (point == 2 && entry_barrier_target) {
		__atomic_fetch_add(&entry_barrier_count, 1U, __ATOMIC_ACQ_REL);
		while (__atomic_load_n(&entry_barrier_count, __ATOMIC_ACQUIRE) <
		       entry_barrier_target)
			__asm__ volatile ("pause");
	}
	if (point != entry_hook_point || cpu != entry_hook_cpu)
		return;
	__atomic_store_n(&entry_hook_entered, 1U, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&entry_hook_release, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
}


static void expect_exit(pid_t child, int code)
{
	int status;

	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status));
	assert(WEXITSTATUS(status) == code);
}

static void expect_signal(pid_t child, int signal_number)
{
	int status;

	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status));
	assert(WTERMSIG(status) == signal_number);
}

static enum cb_err test_arm(struct smm_invocation_evidence *evidence,
	struct smm_invocation_loader_instance_nonce loader_instance_nonce, uint32_t lifecycle)
{
	struct smm_invocation_admission_token token;

	return smm_invocation_evidence_require_rendezvous_ack_try(evidence,
		loader_instance_nonce, lifecycle, &token) == SMM_INVOCATION_TRY_SUCCESS ?
		CB_SUCCESS : CB_ERR;
}

static enum cb_err test_arrive(struct smm_invocation_evidence *evidence,
	uint32_t cpu, uint32_t apic_id, uint64_t *generation)
{
	struct smm_invocation_admission_token token;

	return smm_invocation_evidence_arrive_try(evidence, cpu, apic_id,
		generation, &token) == SMM_INVOCATION_TRY_SUCCESS ?
		CB_SUCCESS : CB_ERR;
}

static enum cb_err test_ack(struct smm_invocation_evidence *evidence,
	uint64_t generation, uint32_t cpu)
{
	struct smm_invocation_admission_token token;

	return smm_invocation_evidence_rendezvous_ack_try(evidence, generation,
		cpu, &token) == SMM_INVOCATION_TRY_SUCCESS ? CB_SUCCESS : CB_ERR;
}

static enum cb_err test_depart(struct smm_invocation_evidence *evidence,
	uint32_t cpu, uint64_t generation)
{
	return smm_invocation_evidence_depart_try(evidence, cpu, generation) ==
		SMM_INVOCATION_TRY_SUCCESS ? CB_SUCCESS : CB_ERR;
}

struct arrival_arg {
	struct smm_invocation_evidence *evidence;
	const struct smm_invocation_entry_cause *cause;
	const struct smm_invocation_entry_policy *policy;
	uint32_t cpu;
	uint32_t apic_id;
	struct smm_invocation_entry_ticket ticket;
	enum cb_err result;
	uint32_t completed;
};

struct ticket_call_arg {
	struct smm_invocation_evidence *evidence;
	struct smm_invocation_entry_ticket *ticket;
	enum cb_err depart_result;
	bool eos_result;
};

struct claim_arg {
	struct smm_invocation_evidence *evidence;
	const struct smm_invocation_save_state_ops *ops;
	struct smm_invocation_token token;
	enum cb_err result;
};

struct ack_ready_arg {
	const struct smm_invocation_evidence *evidence;
	uint64_t generation;
	uint32_t release;
	bool result;
};

static void *arrival_thread(void *arg)
{
	struct arrival_arg *arrival = arg;

	arrival->result = smm_invocation_entry_arrive(arrival->evidence,
		arrival->cause, arrival->policy, NONCE(7), 0xa5, arrival->cpu,
		arrival->apic_id,
		&arrival->ticket);
	__atomic_store_n(&arrival->completed, 1U, __ATOMIC_RELEASE);
	return NULL;
}

static void *depart_thread(void *arg)
{
	struct ticket_call_arg *call = arg;

	call->depart_result = smm_invocation_entry_depart(call->evidence,
		call->ticket);
	return NULL;
}

static void *eos_thread(void *arg)
{
	struct ticket_call_arg *call = arg;

	call->eos_result = smm_invocation_entry_eos_ready(call->evidence,
		call->ticket);
	return NULL;
}

static void *claim_thread(void *arg)
{
	struct claim_arg *claim = arg;

	claim->result = smm_invocation_evidence_claim(claim->evidence, 0xa5,
		0x11223344556677a5ULL, claim->ops, &claim->token);
	return NULL;
}

static void *ack_ready_thread(void *arg)
{
	struct ack_ready_arg *ready = arg;

	while (!__atomic_load_n(&ready->release, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	ready->result = smm_invocation_evidence_rendezvous_ack_ready(
		ready->evidence, ready->generation);
	return NULL;
}

static void test_em64t101(void)
{
	em64t101_smm_state_save_area_t state = { 0 };
	struct intel_smm_invocation_adapter adapter;
	struct smm_invocation_save_state_ops ops;
	uintptr_t top = (uintptr_t)&state + sizeof(state);
	uint64_t value;

	state.smm_revision = REV101;
	state.io_misc_info = EXACT_IO;
	state.rax = 0xa5;
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(state), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter,
		(void *)((uintptr_t)&state +
			offsetof(em64t101_smm_state_save_area_t, rax))) == CB_ERR);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(ops.match_apmc_write(ops.context, 0, 0xa5) ==
		SMM_INVOCATION_MATCHED);
	assert(ops.read_rax(ops.context, 0, &value) == CB_SUCCESS);
	assert(value == 0xa5);
	assert(ops.write_rax(ops.context, 0, 0x123400a5) == CB_SUCCESS);
	assert(state.rax == 0x123400a5);

	state.io_misc_info = 0x00b20083U;
	assert(ops.read_rax(ops.context, 0, &value) == CB_ERR);
	assert(ops.match_apmc_write(ops.context, 0, 0xa5) ==
		SMM_INVOCATION_MATCH_ERROR);
	state.io_misc_info = 0x01b20003U;
	assert(ops.match_apmc_write(ops.context, 0, 0xa5) ==
		SMM_INVOCATION_MATCH_ERROR);
	state.io_misc_info = EXACT_IO | (1U << 8);
	assert(ops.match_apmc_write(ops.context, 0, 0xa5) ==
		SMM_INVOCATION_MATCH_ERROR);
	state.io_misc_info = EXACT_IO;
	state.smm_revision = REV100;
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(state), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(ops.match_apmc_write(ops.context, 0, 0xa5) ==
		SMM_INVOCATION_MATCH_ERROR);
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(state) - 1, REV101) == CB_ERR);
}

static void test_em64t100(void)
{
	em64t100_smm_state_save_area_t state = { 0 };
	struct intel_smm_invocation_adapter adapter;
	struct smm_invocation_save_state_ops ops;
	uintptr_t top = (uintptr_t)&state + sizeof(state);

	state.smm_revision = REV100;
	state.io_misc_info = EXACT_IO;
	state.rax = 0x5a;
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(state), REV100) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter,
		(void *)((uintptr_t)&state +
			offsetof(em64t100_smm_state_save_area_t, rax))) == CB_ERR);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(ops.match_apmc_write(ops.context, 0, 0x5a) ==
		SMM_INVOCATION_MATCHED);
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(state), 0x30000) == CB_ERR);
}

static void test_adapter_boundaries(void)
{
	em64t101_smm_state_save_area_t states[2] = { 0 };
	struct intel_smm_invocation_adapter adapter;
	struct intel_smm_invocation_adapter snapshot;
	struct smm_invocation_save_state_ops ops;
	uintptr_t top = (uintptr_t)&states[0] + sizeof(states[0]);
	uint64_t value;

	states[0].smm_revision = REV101;
	states[0].io_misc_info = EXACT_IO;
	states[0].rax = 0xa5;
	states[1] = states[0];
	adapter_hook_state = &states[0];
	adapter_hook_adapter = &adapter;
	assert(sizeof(adapter) <= SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX);
	for (uint32_t bit = 0; bit < 32; bit++) {
		states[0].io_misc_info = EXACT_IO ^ (1U << bit);
		assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
			sizeof(states[0]), REV101) == CB_SUCCESS);
		assert(intel_smm_invocation_adapter_ops(&adapter, &ops) ==
			CB_SUCCESS);
		assert(ops.match_apmc_write(ops.context, 0, 0xa5) !=
			SMM_INVOCATION_MATCHED);
	}
	states[0].io_misc_info = EXACT_IO;
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(states[0]), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	adapter_hook_point = 1;
	assert(ops.match_apmc_write(ops.context, 0, 0xa5) ==
		SMM_INVOCATION_MATCH_ERROR);
	adapter_hook_point = 0;
	states[0].io_misc_info = EXACT_IO;
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(ops.match_apmc_write(ops.context, 0, 0xa5) ==
		SMM_INVOCATION_MATCHED);
	adapter_hook_point = 2;
	assert(ops.read_rax(ops.context, 0, &value) == CB_ERR);
	adapter_hook_point = 0;
	states[0].io_misc_info = EXACT_IO;
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(ops.match_apmc_write(ops.context, 0, 0xa5) ==
		SMM_INVOCATION_MATCHED);
	adapter_hook_point = 3;
	assert(ops.write_rax(ops.context, 0, 0x123400a5) == CB_ERR);
	assert(states[0].rax == 0xa5);
	adapter_hook_point = 0;
	states[0].io_misc_info = EXACT_IO;
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(ops.match_apmc_write(ops.context, 0, 0xa5) ==
		SMM_INVOCATION_MATCHED);
	adapter_hook_redirect = (uintptr_t)&states[1];
	adapter_hook_point = 2;
	assert(ops.read_rax(ops.context, 0, &value) == CB_ERR);
	adapter_hook_point = 0;
	adapter_hook_redirect = 0;
	adapter.nodes[0].save_state = (uintptr_t)&states[0];
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(states[0]), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	adapter_hook_revision = REV100;
	adapter_hook_point = 1;
	assert(ops.match_apmc_write(ops.context, 0, 0xa5) ==
		SMM_INVOCATION_MATCH_ERROR);
	adapter_hook_point = 0;
	adapter_hook_revision = 0;

	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(states[0]), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(ops.match_apmc_write(ops.context, 0, 0xa5) ==
		SMM_INVOCATION_MATCHED);
	adapter_hook_revision = REV100;
	adapter_hook_point = 2;
	assert(ops.read_rax(ops.context, 0, &value) == CB_ERR);
	adapter_hook_point = 0;
	adapter_hook_revision = 0;

	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(states[0]), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(ops.match_apmc_write(ops.context, 0, 0xa5) ==
		SMM_INVOCATION_MATCHED);
	adapter_hook_revision = REV100;
	adapter_hook_point = 3;
	assert(ops.write_rax(ops.context, 0, 0x123400a5) == CB_ERR);
	assert(states[0].rax == 0xa5);
	adapter_hook_point = 0;
	adapter_hook_revision = 0;
	states[0].rax = 0xa5;

	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(states[0]), REV101) == CB_SUCCESS);
	adapter_hook_revision = REV100;
	adapter_hook_point = 5;
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_ERR);
	adapter_hook_point = 0;
	adapter_hook_revision = 0;
	assert(adapter.expected_revision == REV100);

	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(states[0]), REV101) == CB_SUCCESS);
	adapter.expected_revision = 0;
	memcpy(&snapshot, &adapter, sizeof(snapshot));
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_ERR);
	assert(!memcmp(&snapshot, &adapter, sizeof(snapshot)));

	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(states[0]), REV101) == CB_SUCCESS);
	adapter_hook_redirect = (uintptr_t)&states[1];
	adapter_hook_point = 5;
	adapter.active_cpus = SMM_INVOCATION_EVIDENCE_MAX_CPUS + 1U;
	memcpy(&snapshot, &adapter, sizeof(snapshot));
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_ERR);
	assert(!memcmp(&snapshot, &adapter, sizeof(snapshot)));
	adapter.active_cpus = UINT32_MAX;
	memcpy(&snapshot, &adapter, sizeof(snapshot));
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_ERR);
	assert(!memcmp(&snapshot, &adapter, sizeof(snapshot)));
	adapter_hook_point = 0;
	adapter_hook_redirect = 0;

	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(states[0]), REV101) == CB_SUCCESS);
	adapter_hook_redirect = (uintptr_t)&states[1];
	adapter_hook_point = 5;
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_ERR);
	adapter_hook_point = 0;
	adapter_hook_redirect = 0;

	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(states[0]), REV101) == CB_SUCCESS);
	adapter.invocation_nonce = UINT64_MAX;
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_ERR);
	assert(adapter.reserved == 1U);
	assert(adapter.invocation_nonce == UINT64_MAX);
	memcpy(&snapshot, &adapter, sizeof(snapshot));
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_ERR);
	assert(!memcmp(&snapshot, &adapter, sizeof(snapshot)));
}

static void test_cause_validation(void)
{
	struct smm_invocation_entry_cause cause = {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
		.size = sizeof(cause),
		.loader_instance_nonce = NONCE(7),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.command = 0xa5,
		.recognized = 0,
	};
	struct smm_invocation_entry_ticket ticket;
	struct smm_invocation_evidence evidence = { 0 };
	const struct smm_invocation_entry_policy policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(policy),
		.max_polls = 1000,
	};

	assert(smm_invocation_entry_arrive(&evidence, &cause, &policy, NONCE(7), 0xa5,
		0, 0, &ticket) == CB_ERR);
	cause.recognized = 1;
	cause.reserved[0] = 1;
	assert(smm_invocation_entry_arrive(&evidence, &cause, &policy, NONCE(7), 0xa5,
		0, 0, &ticket) == CB_ERR);
}

static void exercise_sparse_entry_nonce(
	struct smm_invocation_loader_instance_nonce nonce)
{
	em64t101_smm_state_save_area_t state = { 0 };
	uintptr_t top = (uintptr_t)&state + sizeof(state);
	struct intel_smm_invocation_adapter adapter;
	struct smm_invocation_save_state_ops ops;
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = nonce,
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_entry_cause cause = {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
		.size = sizeof(cause),
		.loader_instance_nonce = nonce,
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.command = 0xa5,
		.recognized = 1,
	};
	const struct smm_invocation_entry_policy policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(policy),
		.max_polls = 1000,
	};
	struct smm_invocation_entry_ticket ticket;
	struct smm_invocation_entry_ticket invalid;
	struct smm_invocation_token token;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	state.smm_revision = REV101;
	state.io_misc_info = EXACT_IO;
	state.rax = 0xa5;
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(state), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(smm_invocation_entry_arrive(&evidence, &cause, &policy, nonce,
		0xa5, 0, 3, &ticket) == CB_SUCCESS);
	assert(ticket.command == cause.command);
	assert(ticket.reserved[0] == 0U && ticket.reserved[1] == 0U &&
	       ticket.reserved[2] == 0U);
	assert(smm_invocation_evidence_claim(&evidence, 0xa5,
		0x11223344556677a5ULL, &ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(&evidence, &token, &ops) ==
		CB_SUCCESS);
	for (size_t byte = 0; byte < sizeof(ticket.reserved); byte++) {
		invalid = ticket;
		invalid.reserved[byte] = 1U;
		assert(smm_invocation_entry_depart(&evidence, &invalid) == CB_ERR);
	}
	assert(smm_invocation_entry_depart(&evidence, &ticket) == CB_SUCCESS);
	for (size_t byte = 0; byte < sizeof(ticket.reserved); byte++) {
		invalid = ticket;
		invalid.reserved[byte] = 1U;
		assert(!smm_invocation_entry_eos_ready(&evidence, &invalid));
	}
	assert(smm_invocation_entry_eos_ready(&evidence, &ticket));
}

static void test_sparse_entry_nonce(void)
{
	exercise_sparse_entry_nonce(
		(struct smm_invocation_loader_instance_nonce) { .low = 1 });
	exercise_sparse_entry_nonce(
		(struct smm_invocation_loader_instance_nonce) { .high = 1 });
}

static void test_identity_and_aliases(void)
{
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(7),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_entry_cause cause = {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
		.size = sizeof(cause),
		.loader_instance_nonce = NONCE(8),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.command = 0xa5,
		.recognized = 1,
	};
	struct smm_invocation_entry_policy policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(policy),
		.max_polls = 10,
	};
	struct smm_invocation_entry_ticket ticket;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(smm_invocation_entry_arrive(&evidence, &cause, &policy, NONCE(8), 0xa5,
		0, 3, &ticket) == CB_ERR);
	assert(!__atomic_load_n(&evidence.rendezvous_ack_required,
		__ATOMIC_ACQUIRE));
	cause.loader_instance_nonce = seed.loader_instance_nonce;
	cause.loader_instance_nonce.low++;
	assert(smm_invocation_entry_arrive(&evidence, &cause, &policy,
		seed.loader_instance_nonce, 0xa5, 0, 3, &ticket) == CB_ERR);
	cause.loader_instance_nonce = seed.loader_instance_nonce;
	cause.loader_instance_nonce.high++;
	assert(smm_invocation_entry_arrive(&evidence, &cause, &policy,
		seed.loader_instance_nonce, 0xa5, 0, 3, &ticket) == CB_ERR);
	cause.loader_instance_nonce = NONCE(7);
	cause.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	assert(smm_invocation_entry_arrive(&evidence, &cause, &policy, NONCE(7), 0xa5,
		0, 3, &ticket) == CB_ERR);
	assert(!__atomic_load_n(&evidence.rendezvous_ack_required,
		__ATOMIC_ACQUIRE));
	cause.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	assert(smm_invocation_entry_arrive(&evidence, &cause, &policy, NONCE(7), 0xa5,
		0, 3, (void *)&evidence) == CB_ERR);
	assert(smm_invocation_entry_arrive(&evidence,
		(const void *)(UINTPTR_MAX - 1U), &policy, NONCE(7), 0xa5, 0, 3,
		&ticket) == CB_ERR);
}

static void test_policy_snapshot_mutation(void)
{
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(7),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_entry_cause cause = {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
		.size = sizeof(cause),
		.loader_instance_nonce = NONCE(7),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.command = 0xa5,
		.recognized = 1,
	};
	struct smm_invocation_entry_policy policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(policy),
		.max_polls = 10,
	};
	struct arrival_arg arrival = {
		.evidence = &evidence,
		.cause = &cause,
		.policy = &policy,
		.cpu = 0,
		.apic_id = 3,
	};
	pthread_t thread;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	for (uint32_t mutation = 0; mutation < 4; mutation++) {
		const struct smm_invocation_entry_policy original = policy;

		arrival.result = CB_SUCCESS;
		arrival.completed = 0;
		entry_hook_point = 1;
		entry_hook_cpu = 0;
		entry_hook_entered = 0;
		entry_hook_release = 0;
		assert(!pthread_create(&thread, NULL, arrival_thread, &arrival));
		while (!__atomic_load_n(&entry_hook_entered, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		if (mutation == 0)
			policy.max_polls--;
		else if (mutation == 1)
			policy.reserved = 1;
		else if (mutation == 2)
			policy.revision++;
		else
			policy.size--;
		__atomic_store_n(&entry_hook_release, 1U, __ATOMIC_RELEASE);
		assert(!pthread_join(thread, NULL));
		assert(arrival.result == CB_ERR);
		assert(smm_invocation_evidence_phase(&evidence) ==
			SMM_INVOCATION_READY);
		policy = original;
	}
	entry_hook_point = 0;
	entry_hook_cpu = UINT32_MAX;
	entry_hook_release = 1;
}

struct ack_arm_arg {
	struct smm_invocation_evidence *evidence;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	uint32_t lifecycle;
	enum cb_err result;
	uint32_t completed;
};

struct rendezvous_call_arg {
	struct smm_invocation_evidence *evidence;
	uint64_t generation;
	uint32_t cpu;
	enum cb_err result;
};

struct departure_try_arg {
	struct smm_invocation_evidence *evidence;
	uint64_t generation;
	uint32_t cpu;
	enum smm_invocation_try_result result;
};

struct generation_call_arg {
	struct smm_invocation_evidence *evidence;
	uint64_t generation;
	enum cb_err result;
};

struct evidence_arrival_arg {
	struct smm_invocation_evidence *evidence;
	uint32_t cpu;
	uint32_t apic_id;
	uint64_t generation;
	enum cb_err result;
};

struct admission_try_arg {
	struct smm_invocation_evidence *evidence;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	uint64_t generation;
	uint32_t lifecycle;
	uint32_t cpu;
	uint32_t apic_id;
	struct smm_invocation_admission_token token;
	enum smm_invocation_try_result result;
};

struct admission_fail_arg {
	struct smm_invocation_evidence *evidence;
	struct smm_invocation_admission_token token;
	enum cb_err result;
};

struct shutdown_arg {
	struct smm_invocation_evidence *evidence;
	enum cb_err result;
	uint32_t completed;
};

struct provision_arg {
	struct smm_invocation_evidence *evidence;
	const struct smm_invocation_loader_seed *seed;
	enum cb_err result;
};

struct eos_consume_arg {
	struct smm_invocation_evidence *evidence;
	uint64_t generation;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	uint32_t lifecycle;
	uint32_t cpu;
	bool result;
};

static void *ack_arm_thread(void *arg)
{
	struct ack_arm_arg *arm = arg;

	arm->result = test_arm(
		arm->evidence, arm->loader_instance_nonce, arm->lifecycle);
	__atomic_store_n(&arm->completed, 1U, __ATOMIC_RELEASE);
	return NULL;
}

static void *rendezvous_ack_thread(void *arg)
{
	struct rendezvous_call_arg *call = arg;

	call->result = test_ack(call->evidence,
		call->generation, call->cpu);
	return NULL;
}

static void *rendezvous_fail_thread(void *arg)
{
	struct rendezvous_call_arg *call = arg;

	call->result = smm_invocation_evidence_rendezvous_fail(call->evidence,
		call->generation);
	return NULL;
}

static void *departure_try_thread(void *arg)
{
	struct departure_try_arg *call = arg;

	call->result = smm_invocation_evidence_depart_try(call->evidence,
		call->cpu, call->generation);
	return NULL;
}

static void *ticket_fail_thread(void *arg)
{
	struct generation_call_arg *call = arg;

	call->result = smm_invocation_evidence_ticket_fail(call->evidence,
		call->generation);
	return NULL;
}

static void *evidence_arrival_thread(void *arg)
{
	struct evidence_arrival_arg *arrival = arg;

	arrival->result = test_arrive(arrival->evidence,
		arrival->cpu, arrival->apic_id, &arrival->generation);
	return NULL;
}

static void *ack_arm_try_thread(void *arg)
{
	struct admission_try_arg *call = arg;

	call->result = smm_invocation_evidence_require_rendezvous_ack_try(
		call->evidence, call->loader_instance_nonce, call->lifecycle,
		&call->token);
	return NULL;
}

static void *arrival_try_thread(void *arg)
{
	struct admission_try_arg *call = arg;

	call->result = smm_invocation_evidence_arrive_try(call->evidence,
		call->cpu, call->apic_id, &call->generation, &call->token);
	return NULL;
}

static void *rendezvous_ack_try_thread(void *arg)
{
	struct admission_try_arg *call = arg;

	call->result = smm_invocation_evidence_rendezvous_ack_try(
		call->evidence, call->generation, call->cpu, &call->token);
	return NULL;
}

static void *admission_fail_thread(void *arg)
{
	struct admission_fail_arg *call = arg;

	call->result = smm_invocation_evidence_admission_fail(call->evidence,
		&call->token);
	return NULL;
}

static void *shutdown_thread(void *arg)
{
	struct shutdown_arg *shutdown = arg;

	shutdown->result = smm_invocation_evidence_shutdown(shutdown->evidence);
	__atomic_store_n(&shutdown->completed, 1U, __ATOMIC_RELEASE);
	return NULL;
}

static void *provision_thread(void *arg)
{
	struct provision_arg *provision = arg;

	provision->result = smm_invocation_evidence_provision(
		provision->evidence, provision->seed);
	return NULL;
}

static void *eos_consume_thread(void *arg)
{
	struct eos_consume_arg *consume = arg;

	consume->result = smm_invocation_evidence_eos_consume(
		consume->evidence, consume->generation, consume->loader_instance_nonce,
		consume->lifecycle, consume->cpu);
	return NULL;
}

static void test_ack_arm_race(void)
{
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(11),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct ack_arm_arg args[2] = {
		{ .evidence = &evidence, .loader_instance_nonce = NONCE(11),
		  .lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD },
		{ .evidence = &evidence, .loader_instance_nonce = NONCE(11),
		  .lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD },
	};
	pthread_t threads[2];

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(12),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) == CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_READY);
	assert(!__atomic_load_n(&evidence.rendezvous_ack_required,
		__ATOMIC_ACQUIRE));
	assert(!__atomic_load_n(&evidence.rendezvous_fail_requested,
		__ATOMIC_ACQUIRE));
	evidence_hook_point = 9;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&threads[0], NULL, ack_arm_thread, &args[0]));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&threads[1], NULL, ack_arm_thread, &args[1]));
	assert(!pthread_join(threads[1], NULL));
	assert(args[1].result == CB_ERR);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(threads[0], NULL));
	assert(args[0].result == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(11),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) == CB_SUCCESS);
	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_ack_arm_published_marker_is_retry(void)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(76),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct admission_try_arg owner = {
		.evidence = &evidence,
		.loader_instance_nonce = seed.loader_instance_nonce,
		.lifecycle = seed.lifecycle,
	};
	struct smm_invocation_admission_token retry;
	pthread_t thread;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	evidence_hook_point = 44;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, ack_arm_try_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(__atomic_load_n(&evidence.rendezvous_ack_required,
		__ATOMIC_ACQUIRE) == 1U);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
		seed.loader_instance_nonce, seed.lifecycle, &retry) ==
		SMM_INVOCATION_TRY_RETRY);
	assert(retry.evidence_identity == (uintptr_t)&evidence);
	assert(retry.kind == SMM_INVOCATION_ADMISSION_ARM);
	assert(retry.attempt_nonce != 0);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
		seed.loader_instance_nonce, seed.lifecycle, &retry) ==
		SMM_INVOCATION_TRY_SUCCESS);
	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_departure_published_marker_is_retry(void)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 2,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(77),
		.participant_apic_ids = { 3, 7 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct departure_try_arg owner = {
		.evidence = &evidence,
		.cpu = 0,
		.generation = 1,
	};
	pthread_t thread;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	__atomic_store_n(&evidence.generation, owner.generation,
		__ATOMIC_RELAXED);
	__atomic_store_n(&evidence.state,
		TEST_CLOSE_REQUESTED | SMM_INVOCATION_CLOSING,
		__ATOMIC_RELEASE);
	evidence_hook_point = 29;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, departure_try_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_depart_try(&evidence, 1,
		owner.generation) == SMM_INVOCATION_TRY_RETRY);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_depart_try(&evidence, 1,
		owner.generation) == SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_READY);
	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_admission_completion_retries_competing_state_change(void)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(70),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct ack_arm_arg arm = {
		.evidence = &evidence,
		.loader_instance_nonce = NONCE(70),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	pthread_t thread;
	uint32_t state;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	evidence_hook_point = 35;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, ack_arm_thread, &arm));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	state = __atomic_fetch_or(&evidence.state, 1U << 9,
		__ATOMIC_ACQ_REL);
	assert(state & (1U << 7));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(arm.result == CB_ERR);
	state = __atomic_load_n(&evidence.state, __ATOMIC_ACQUIRE);
	assert(!(state & (1U << 7)) && state & (1U << 9));
	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_ack_arm_shutdown_failure_handoff(void)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(53),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct admission_try_arg owner = {
		.evidence = &evidence, .loader_instance_nonce = NONCE(53),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct admission_try_arg waiter = owner;
	struct admission_fail_arg failure;
	struct shutdown_arg shutdown = { .evidence = &evidence };
	pthread_t owner_thread;
	pthread_t failure_thread;
	pthread_t shutdown_worker;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	evidence_hook_point = 9;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	evidence_hook_point_secondary = 24;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&owner_thread, NULL, ack_arm_try_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
		NONCE(53), SMM_INVOCATION_LOADER_NON_S3_LOAD, &waiter.token) ==
		SMM_INVOCATION_TRY_RETRY);
	assert(!pthread_create(&shutdown_worker, NULL, shutdown_thread,
		&shutdown));
	while (!smm_invocation_evidence_shutdown_requested(&evidence))
		__asm__ volatile ("pause");
	failure = (struct admission_fail_arg) {
		.evidence = &evidence, .token = waiter.token,
	};
	assert(!pthread_create(&failure_thread, NULL, admission_fail_thread,
		&failure));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_ADMISSION_FAILED);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_ERROR);
	assert(!(__atomic_load_n(&evidence.state,
		__ATOMIC_ACQUIRE) & (1U << 7)));
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(failure_thread, NULL));
	assert(failure.result == CB_ERR);
	assert(!pthread_join(shutdown_worker, NULL));
	assert(shutdown.result == CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	assert(__atomic_load_n(&evidence.state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_POISONED);
	assert(!evidence.admission_reserved);

	evidence_hook_point = 0;
	evidence_hook_release = 1;
	evidence_hook_point_secondary = 0;
	evidence_hook_release_secondary = 1;
}

static void test_admission_completion_before_observer(void)
{
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 2,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(11),
		.participant_apic_ids = { 3, 7 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct admission_try_arg owner;
	pthread_t owner_thread;
	uint64_t generation[2];

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	owner = (struct admission_try_arg) {
		.evidence = &evidence, .loader_instance_nonce = NONCE(11),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	evidence_hook_point = 16;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner_thread, NULL, ack_arm_try_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
		NONCE(11), SMM_INVOCATION_LOADER_NON_S3_LOAD, &owner.token) ==
		SMM_INVOCATION_TRY_RETRY);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
		NONCE(11), SMM_INVOCATION_LOADER_NON_S3_LOAD, &owner.token) ==
		SMM_INVOCATION_TRY_SUCCESS);

	owner = (struct admission_try_arg) {
		.evidence = &evidence, .cpu = 0, .apic_id = 3,
	};
	evidence_hook_point = 17;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner_thread, NULL, arrival_try_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	struct admission_try_arg contender = {
		.evidence = &evidence, .cpu = 1, .apic_id = 7,
	};
	assert(smm_invocation_evidence_arrive_try(&evidence, 1, 7,
		&contender.generation, &contender.token) ==
		SMM_INVOCATION_TRY_RETRY);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);
	generation[0] = owner.generation;
	assert(smm_invocation_evidence_arrive_try(&evidence, 1, 7,
		&generation[1], &contender.token) == SMM_INVOCATION_TRY_SUCCESS);
	assert(generation[0] == generation[1]);

	owner = (struct admission_try_arg) {
		.evidence = &evidence, .generation = generation[0], .cpu = 0,
	};
	contender = (struct admission_try_arg) {
		.evidence = &evidence, .generation = generation[1], .cpu = 1,
	};
	evidence_hook_point = 18;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner_thread, NULL, rendezvous_ack_try_thread,
		&owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_rendezvous_ack_try(&evidence,
		generation[1], 1, &contender.token) ==
		SMM_INVOCATION_TRY_RETRY);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_rendezvous_ack_try(&evidence,
		generation[1], 1, &contender.token) ==
		SMM_INVOCATION_TRY_SUCCESS);

	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_retry_token_resnapshot_gaps(void)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 2,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(59),
		.participant_apic_ids = { 3, 7 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct admission_try_arg contender = {
		.evidence = &evidence, .loader_instance_nonce = NONCE(59),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct admission_try_arg owner = contender;
	pthread_t contender_thread;
	pthread_t owner_thread;
	uint64_t generation[2];

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	evidence_hook_point = 25;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	evidence_hook_point_secondary = 9;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&contender_thread, NULL, ack_arm_try_thread,
		&contender));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&owner_thread, NULL, ack_arm_try_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(contender_thread, NULL));
	assert(contender.result == SMM_INVOCATION_TRY_RETRY);
	assert(contender.token.attempt_nonce ==
		(__atomic_load_n(&evidence.state,
			__ATOMIC_ACQUIRE) >> 12));
	assert(contender.token.kind == SMM_INVOCATION_ADMISSION_ARM);
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);

	contender = (struct admission_try_arg) {
		.evidence = &evidence, .cpu = 1, .apic_id = 7,
	};
	owner = (struct admission_try_arg) {
		.evidence = &evidence, .cpu = 0, .apic_id = 3,
	};
	evidence_hook_point = 25;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	evidence_hook_point_secondary = 1;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&contender_thread, NULL, arrival_try_thread,
		&contender));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&owner_thread, NULL, arrival_try_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(contender_thread, NULL));
	assert(contender.result == SMM_INVOCATION_TRY_RETRY);
	assert(contender.token.attempt_nonce ==
		(__atomic_load_n(&evidence.state,
			__ATOMIC_ACQUIRE) >> 12));
	assert(contender.token.kind == SMM_INVOCATION_ADMISSION_ARRIVE);
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);
	generation[0] = owner.generation;
	assert(smm_invocation_evidence_arrive_try(&evidence, 1, 7,
		&generation[1], &contender.token) == SMM_INVOCATION_TRY_SUCCESS);

	contender = (struct admission_try_arg) {
		.evidence = &evidence, .generation = generation[1], .cpu = 1,
	};
	owner = (struct admission_try_arg) {
		.evidence = &evidence, .generation = generation[0], .cpu = 0,
	};
	evidence_hook_point = 25;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	evidence_hook_point_secondary = 10;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&contender_thread, NULL,
		rendezvous_ack_try_thread, &contender));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&owner_thread, NULL, rendezvous_ack_try_thread,
		&owner));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(contender_thread, NULL));
	assert(contender.result == SMM_INVOCATION_TRY_RETRY);
	assert(contender.token.attempt_nonce ==
		(__atomic_load_n(&evidence.state,
			__ATOMIC_ACQUIRE) >> 12));
	assert(contender.token.kind == SMM_INVOCATION_ADMISSION_ACK);
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);

	evidence_hook_point = 0;
	evidence_hook_release = 1;
	evidence_hook_point_secondary = 0;
	evidence_hook_release_secondary = 1;
}

static void test_reservation_gap_failure_and_shutdown(void)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(47),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct admission_try_arg owner = {
		.evidence = &evidence, .loader_instance_nonce = NONCE(47),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct admission_try_arg waiter = owner;
	struct admission_fail_arg failure;
	struct smm_invocation_evidence terminal;
	pthread_t thread;
	pthread_t failure_thread;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	evidence_hook_point = 16;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, ack_arm_try_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
		NONCE(47), SMM_INVOCATION_LOADER_NON_S3_LOAD, &waiter.token) ==
		SMM_INVOCATION_TRY_RETRY);
	failure = (struct admission_fail_arg) {
		.evidence = &evidence, .token = waiter.token,
	};
	evidence_hook_point_secondary = 24;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&failure_thread, NULL, admission_fail_thread,
		&failure));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(__atomic_load_n(&evidence.state, __ATOMIC_ACQUIRE) &
		(1U << 8));
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(failure_thread, NULL));
	assert(failure.result == CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONING);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_ERROR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	assert(__atomic_load_n(&evidence.state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_POISONED);
	assert(!evidence.admission_reserved);

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(47),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) == CB_SUCCESS);
	assert(test_arrive(&evidence, 0, 3,
		&waiter.generation) == CB_SUCCESS);
	owner = (struct admission_try_arg) {
		.evidence = &evidence, .generation = waiter.generation, .cpu = 0,
	};
	waiter = owner;
	evidence_hook_point = 18;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, rendezvous_ack_try_thread,
		&owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_rendezvous_ack_try(&evidence,
		waiter.generation, 0, &waiter.token) ==
		SMM_INVOCATION_TRY_RETRY);
	assert(smm_invocation_evidence_admission_fail(&evidence,
		&waiter.token) == CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONING);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_ERROR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	assert(!(__atomic_load_n(&evidence.state, __ATOMIC_ACQUIRE) & ~0x1fU));

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(47),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) == CB_SUCCESS);
	owner = (struct admission_try_arg) {
		.evidence = &evidence, .cpu = 0, .apic_id = 3,
	};
	waiter = owner;
	evidence_hook_point = 17;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, arrival_try_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
		&waiter.generation, &waiter.token) == SMM_INVOCATION_TRY_RETRY);
	assert(smm_invocation_evidence_admission_fail(&evidence,
		&waiter.token) == CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONING);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_ERROR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	assert(!(__atomic_load_n(&evidence.state, __ATOMIC_ACQUIRE) & ~0x1fU));

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(47),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) == CB_SUCCESS);
	owner = (struct admission_try_arg) {
		.evidence = &evidence, .cpu = 0, .apic_id = 3,
	};
	evidence_hook_point = 27;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, arrival_try_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_shutdown(&evidence) == CB_SUCCESS);
	memcpy(&terminal, &evidence, sizeof(terminal));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_ERROR);
	assert(!memcmp(&terminal, &evidence, sizeof(terminal)));
	assert(__atomic_load_n(&evidence.state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_CLOSED);
	assert(!evidence.admission_reserved);

	evidence_hook_point = 0;
	evidence_hook_release = 1;
	evidence_hook_point_secondary = 0;
	evidence_hook_release_secondary = 1;
}

static void test_transient_completion_before_observer(void)
{
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 2,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(11),
		.participant_apic_ids = { 3, 7 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct admission_try_arg owner = {
		.evidence = &evidence, .loader_instance_nonce = NONCE(11),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct admission_try_arg observer = owner;
	pthread_t owner_thread;
	pthread_t observer_thread;
	uint64_t generation[2];

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	evidence_hook_point = 9;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	evidence_hook_point_secondary = 19;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&owner_thread, NULL, ack_arm_try_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&observer_thread, NULL, ack_arm_try_thread,
		&observer));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(observer_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);
	assert(observer.result == SMM_INVOCATION_TRY_SUCCESS);

	owner = (struct admission_try_arg) {
		.evidence = &evidence, .cpu = 0, .apic_id = 3,
	};
	observer = (struct admission_try_arg) {
		.evidence = &evidence, .cpu = 1, .apic_id = 7,
	};
	evidence_hook_point = 1;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	evidence_hook_point_secondary = 20;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&owner_thread, NULL, arrival_try_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&observer_thread, NULL, arrival_try_thread,
		&observer));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(observer_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);
	assert(observer.result == SMM_INVOCATION_TRY_SUCCESS);
	generation[0] = owner.generation;
	generation[1] = observer.generation;
	assert(generation[0] == generation[1]);

	owner = (struct admission_try_arg) {
		.evidence = &evidence, .generation = generation[0], .cpu = 0,
	};
	observer = (struct admission_try_arg) {
		.evidence = &evidence, .generation = generation[1], .cpu = 1,
	};
	evidence_hook_point = 10;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	evidence_hook_point_secondary = 21;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&owner_thread, NULL, rendezvous_ack_try_thread,
		&owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&observer_thread, NULL,
		rendezvous_ack_try_thread, &observer));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(observer_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);
	assert(observer.result == SMM_INVOCATION_TRY_SUCCESS);

	evidence_hook_point = 0;
	evidence_hook_release = 1;
	evidence_hook_point_secondary = 0;
	evidence_hook_release_secondary = 1;
}

static void test_admission_token_binding(void)
{
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(23),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_evidence other = { 0 };
	struct smm_invocation_admission_token exact;
	struct smm_invocation_admission_token altered;
	uint64_t generation;
	uint32_t phase;
	uint32_t failed;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(smm_invocation_evidence_provision(&other, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(23),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) == CB_SUCCESS);
	assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
		&generation, &exact) == SMM_INVOCATION_TRY_SUCCESS);
	exact.invocation_generation = generation;
	assert(exact.evidence_identity == (uintptr_t)&evidence);
	phase = smm_invocation_evidence_phase(&evidence);
	failed = __atomic_load_n(&evidence.rendezvous_fail_requested,
		__ATOMIC_ACQUIRE);

	altered = exact;
	altered.evidence_identity = (uintptr_t)&other;
	assert(smm_invocation_evidence_admission_fail(&evidence, &altered) ==
		CB_ERR);
	assert(smm_invocation_evidence_admission_fail(&other, &exact) == CB_ERR);
	altered = exact;
	altered.kind = 0;
	assert(smm_invocation_evidence_admission_fail(&evidence, &altered) ==
		CB_ERR);
	altered = exact;
	altered.kind = SMM_INVOCATION_ADMISSION_ACK;
	altered.invocation_generation++;
	assert(smm_invocation_evidence_admission_fail(&evidence, &altered) ==
		CB_ERR);
	altered = exact;
	altered.loader_instance_nonce.low++;
	assert(smm_invocation_evidence_admission_fail(&evidence, &altered) ==
		CB_ERR);
	altered = exact;
	altered.loader_instance_nonce.high++;
	assert(smm_invocation_evidence_admission_fail(&evidence, &altered) ==
		CB_ERR);
	altered = exact;
	altered.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	assert(smm_invocation_evidence_admission_fail(&evidence, &altered) ==
		CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) == phase);
	assert(__atomic_load_n(&evidence.rendezvous_fail_requested,
		__ATOMIC_ACQUIRE) == failed);
	assert(smm_invocation_evidence_phase(&other) ==
		SMM_INVOCATION_READY);

	exact.kind = SMM_INVOCATION_ADMISSION_ARRIVE;
	assert(smm_invocation_evidence_admission_fail(&evidence, &exact) ==
		CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	phase = smm_invocation_evidence_phase(&evidence);
	failed = __atomic_load_n(&evidence.rendezvous_fail_requested,
		__ATOMIC_ACQUIRE);
	assert(smm_invocation_evidence_admission_fail(&evidence, &exact) ==
		CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) == phase);
	assert(__atomic_load_n(&evidence.rendezvous_fail_requested,
		__ATOMIC_ACQUIRE) == failed);
	__atomic_store_n(&other.state,
		(UINT32_MAX & ~0xfffU) | SMM_INVOCATION_READY,
		__ATOMIC_RELEASE);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&other, NONCE(23),
		SMM_INVOCATION_LOADER_NON_S3_LOAD, &altered) ==
		SMM_INVOCATION_TRY_ERROR);
	assert(smm_invocation_evidence_phase(&other) ==
		SMM_INVOCATION_POISONED);
}

static void test_invalid_admission_state(void)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 2,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(83),
		.participant_apic_ids = { 3, 5 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	const uint32_t invalid[] = {
		TEST_ADMISSION_CONSUMED,
		0xfffffU << 12,
	};
	struct smm_invocation_evidence evidence;
	struct smm_invocation_admission_token token;
	struct admission_try_arg arm;
	pthread_t thread;
	uint64_t generation;

	for (size_t index = 0; index < ARRAY_SIZE(invalid); index++) {
		memset(&evidence, 0, sizeof(evidence));
		assert(smm_invocation_evidence_provision(&evidence, &seed) ==
			CB_SUCCESS);
		__atomic_store_n(&evidence.state,
			invalid[index] | SMM_INVOCATION_READY, __ATOMIC_RELEASE);
		assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
			NONCE(83), SMM_INVOCATION_LOADER_NON_S3_LOAD, &token) ==
			SMM_INVOCATION_TRY_ERROR);
		assert(smm_invocation_evidence_phase(&evidence) ==
			SMM_INVOCATION_POISONED);

		memset(&evidence, 0, sizeof(evidence));
		assert(smm_invocation_evidence_provision(&evidence, &seed) ==
			CB_SUCCESS);
		__atomic_store_n(&evidence.state,
			invalid[index] | SMM_INVOCATION_READY, __ATOMIC_RELEASE);
		assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
			&generation, &token) == SMM_INVOCATION_TRY_ERROR);
		assert(smm_invocation_evidence_phase(&evidence) ==
			SMM_INVOCATION_POISONED);

		memset(&evidence, 0, sizeof(evidence));
		assert(smm_invocation_evidence_provision(&evidence, &seed) ==
			CB_SUCCESS);
		__atomic_store_n(&evidence.state,
			invalid[index] | SMM_INVOCATION_COLLECTING,
			__ATOMIC_RELEASE);
		assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
			&generation, &token) == SMM_INVOCATION_TRY_ERROR);
		assert(smm_invocation_evidence_phase(&evidence) ==
			SMM_INVOCATION_POISONED);

		memset(&evidence, 0, sizeof(evidence));
		assert(smm_invocation_evidence_provision(&evidence, &seed) ==
			CB_SUCCESS);
		__atomic_store_n(&evidence.state,
			invalid[index] | SMM_INVOCATION_COLLECTING,
			__ATOMIC_RELEASE);
		assert(smm_invocation_evidence_rendezvous_ack_try(&evidence, 1,
			0, &token) == SMM_INVOCATION_TRY_ERROR);
		assert(smm_invocation_evidence_phase(&evidence) ==
			SMM_INVOCATION_POISONED);
	}

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	__atomic_store_n(&evidence.state,
		(0xffffeU << 12) | SMM_INVOCATION_READY, __ATOMIC_RELEASE);
	assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
		&generation, &token) == SMM_INVOCATION_TRY_SUCCESS);
	assert(__atomic_load_n(&evidence.state, __ATOMIC_ACQUIRE) >> 12 ==
		0xfffffU);
	assert(smm_invocation_evidence_arrive_try(&evidence, 1, 5,
		&generation, &token) == SMM_INVOCATION_TRY_ERROR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	__atomic_store_n(&evidence.state,
		(0xfffffU << 12) | SMM_INVOCATION_READY, __ATOMIC_RELEASE);
	arm = (struct admission_try_arg) {
		.evidence = &evidence,
		.loader_instance_nonce = NONCE(83),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	evidence_hook_point = 77;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, ack_arm_try_thread, &arm));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&evidence.state,
		TEST_ADMISSION_NONCE_ONE |
		(SMM_INVOCATION_ADMISSION_ARM << 5) | TEST_ADMISSION_BUSY |
		SMM_INVOCATION_ACK_ARMING, __ATOMIC_RELEASE);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(arm.result == SMM_INVOCATION_TRY_RETRY);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_ACK_ARMING);
	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_admission_failure_completion_race(void)
{
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(29),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct admission_try_arg owner;
	struct admission_fail_arg failure;
	pthread_t owner_thread;
	pthread_t failure_thread;
	uint64_t generation;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(29),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) == CB_SUCCESS);
	assert(test_arrive(&evidence, 0, 3,
		&generation) == CB_SUCCESS);
	owner = (struct admission_try_arg) {
		.evidence = &evidence, .generation = generation, .cpu = 0,
	};
	failure = (struct admission_fail_arg) {
		.evidence = &evidence,
		.token = {
			.evidence_identity = (uintptr_t)&evidence,
			.loader_instance_nonce = NONCE(29),
			.invocation_generation = generation,
			.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
			.kind = SMM_INVOCATION_ADMISSION_ACK,
		},
	};
	evidence_hook_point = 10;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	evidence_hook_point_secondary = 22;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&owner_thread, NULL, rendezvous_ack_try_thread,
		&owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	failure.token.attempt_nonce = __atomic_load_n(
		&evidence.state, __ATOMIC_ACQUIRE) >> 12;
	assert(!pthread_create(&failure_thread, NULL, admission_fail_thread,
		&failure));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_COLLECTING);
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(failure_thread, NULL));
	assert(failure.result == CB_ERR);
	assert(__atomic_load_n(&evidence.rendezvous_fail_requested,
		__ATOMIC_ACQUIRE));
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	assert(!smm_invocation_evidence_shutdown_requested(&evidence));
	assert(!smm_invocation_evidence_rendezvous_ready(&evidence, generation));

	evidence_hook_point = 0;
	evidence_hook_release = 1;
	evidence_hook_point_secondary = 0;
	evidence_hook_release_secondary = 1;
}

static void test_terminal_wins_stale_admission_failure(void)
{
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(37),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_evidence terminal;
	struct smm_invocation_admission_token token;
	struct admission_fail_arg failure;
	pthread_t thread;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence, NONCE(37),
		SMM_INVOCATION_LOADER_NON_S3_LOAD, &token) ==
		SMM_INVOCATION_TRY_SUCCESS);
	failure = (struct admission_fail_arg) {
		.evidence = &evidence, .token = token,
	};
	evidence_hook_point = 22;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, admission_fail_thread, &failure));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_shutdown(&evidence) == CB_SUCCESS);
	memcpy(&terminal, &evidence, sizeof(terminal));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(failure.result == CB_ERR);
	assert(!memcmp(&terminal, &evidence, sizeof(terminal)));

	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_stale_failure_cannot_claim_new_attempt(void)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 2,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(39),
		.participant_apic_ids = { 3, 7 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_evidence snapshot;
	struct smm_invocation_admission_token arm;
	struct admission_try_arg first = {
		.evidence = &evidence, .cpu = 0, .apic_id = 3,
	};
	struct admission_try_arg second = {
		.evidence = &evidence, .cpu = 1, .apic_id = 7,
	};
	struct admission_fail_arg stale = {
		.evidence = &evidence,
	};
	pthread_t failure_thread;
	pthread_t owner_thread;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence, NONCE(39),
		SMM_INVOCATION_LOADER_NON_S3_LOAD, &arm) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_arrive_try(&evidence, first.cpu,
		first.apic_id, &first.generation, &first.token) ==
		SMM_INVOCATION_TRY_SUCCESS);
	stale.token = first.token;
	evidence_hook_point = 22;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&failure_thread, NULL, admission_fail_thread,
		&stale));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	evidence_hook_point_secondary = 5;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&owner_thread, NULL, arrival_try_thread,
		&second));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(failure_thread, NULL));
	assert(stale.result == CB_ERR);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(second.result == SMM_INVOCATION_TRY_SUCCESS);
	assert(!smm_invocation_evidence_shutdown_requested(&evidence));
	assert(!__atomic_load_n(&evidence.rendezvous_fail_requested,
		__ATOMIC_ACQUIRE));
	evidence_hook_point = 0;
	evidence_hook_release = 1;
	evidence_hook_point_secondary = 0;
	evidence_hook_release_secondary = 1;
}

static void test_opening_completion_before_failure(void)
{
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(41),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct admission_try_arg owner = {
		.evidence = &evidence, .cpu = 0, .apic_id = 3,
	};
	struct admission_try_arg waiter = owner;
	struct admission_fail_arg failure;
	pthread_t owner_thread;
	pthread_t failure_thread;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(41),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) == CB_SUCCESS);
	evidence_hook_point = 1;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	evidence_hook_point_secondary = 22;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&owner_thread, NULL, arrival_try_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
		&waiter.generation, &waiter.token) == SMM_INVOCATION_TRY_RETRY);
	failure = (struct admission_fail_arg) {
		.evidence = &evidence, .token = waiter.token,
	};
	assert(!pthread_create(&failure_thread, NULL, admission_fail_thread,
		&failure));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(failure_thread, NULL));
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	assert(!__atomic_load_n(&evidence.arrival_failed, __ATOMIC_ACQUIRE));

	evidence_hook_point = 0;
	evidence_hook_release = 1;
	evidence_hook_point_secondary = 0;
	evidence_hook_release_secondary = 1;
}

static void test_ack_failure_races(void)
{
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(7),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct rendezvous_call_arg ack;
	struct rendezvous_call_arg fail[2];
	struct smm_invocation_admission_token admission;
	pthread_t ack_thread;
	pthread_t fail_threads[2];
	uint64_t generation;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(7),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) ==
		CB_SUCCESS);
	assert(test_arrive(&evidence, 0, 3,
		&generation) == CB_SUCCESS);
	ack = (struct rendezvous_call_arg) {
		.evidence = &evidence, .generation = generation, .cpu = 0,
	};
	evidence_hook_point = 10;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&ack_thread, NULL, rendezvous_ack_thread, &ack));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	admission = (struct smm_invocation_admission_token) {
		.evidence_identity = (uintptr_t)&evidence,
		.attempt_nonce = __atomic_load_n(&evidence.state,
			__ATOMIC_ACQUIRE) >> 12,
		.loader_instance_nonce = NONCE(7),
		.invocation_generation = generation,
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.kind = SMM_INVOCATION_ADMISSION_ACK,
	};
	assert(smm_invocation_evidence_admission_fail(&evidence, &admission) ==
		CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONING);
	assert(__atomic_load_n(&evidence.rendezvous_fail_requested,
		__ATOMIC_ACQUIRE));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(ack_thread, NULL));
	assert(ack.result == CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	assert(!__atomic_load_n(&evidence.generation, __ATOMIC_ACQUIRE));

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(7),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) ==
		CB_SUCCESS);
	assert(test_arrive(&evidence, 0, 3,
		&generation) == CB_SUCCESS);
	for (uint32_t caller = 0; caller < 2; caller++)
		fail[caller] = (struct rendezvous_call_arg) {
			.evidence = &evidence, .generation = generation,
		};
	evidence_hook_point = 11;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&fail_threads[0], NULL, rendezvous_fail_thread,
		&fail[0]));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&fail_threads[1], NULL, rendezvous_fail_thread,
		&fail[1]));
	assert(!pthread_join(fail_threads[1], NULL));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(fail_threads[0], NULL));
	assert(fail[0].result == CB_ERR && fail[1].result == CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	assert(!__atomic_load_n(&evidence.arrival_failed, __ATOMIC_ACQUIRE));
	assert(__atomic_load_n(&evidence.rendezvous_fail_requested,
		__ATOMIC_ACQUIRE));
	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_poison_admitting_handoff(void)
{
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 2,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(31),
		.participant_apic_ids = { 3, 7 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct evidence_arrival_arg last = {
		.evidence = &evidence, .cpu = 1, .apic_id = 7,
	};
	struct rendezvous_call_arg fail;
	pthread_t arrival_thread;
	pthread_t fail_thread;
	uint64_t generation;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(31),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) == CB_SUCCESS);
	assert(test_arrive(&evidence, 0, 3,
		&generation) == CB_SUCCESS);
	fail = (struct rendezvous_call_arg) {
		.evidence = &evidence, .generation = generation,
	};
	evidence_hook_point = 7;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	evidence_hook_point_secondary = 23;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&arrival_thread, NULL, evidence_arrival_thread,
		&last));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&fail_thread, NULL, rendezvous_fail_thread,
		&fail));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISON_ADMITTING);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(arrival_thread, NULL));
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISON_ADMITTING);
	assert(!__atomic_load_n(&evidence.arrival_failed, __ATOMIC_ACQUIRE));
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(fail_thread, NULL));
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	assert(!__atomic_load_n(&evidence.arrival_failed, __ATOMIC_ACQUIRE));

	evidence_hook_point = 0;
	evidence_hook_release = 1;
	evidence_hook_point_secondary = 0;
	evidence_hook_release_secondary = 1;
}

static void test_transient_owner_shutdown_races(void)
{
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(7),
		.participant_apic_ids = { 3, 7 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct ack_arm_arg arm = {
		.evidence = &evidence,
		.loader_instance_nonce = NONCE(7),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct shutdown_arg shutdown = { .evidence = &evidence };
	pthread_t owner;
	pthread_t closer;
	struct smm_invocation_admission_token admission;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	evidence_hook_point = 9;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner, NULL, ack_arm_thread, &arm));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&closer, NULL, shutdown_thread, &shutdown));
	while (!smm_invocation_evidence_shutdown_requested(&evidence))
		__asm__ volatile ("pause");
	assert(!__atomic_load_n(&shutdown.completed, __ATOMIC_ACQUIRE));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner, NULL));
	assert(!pthread_join(closer, NULL));
	assert(arm.result == CB_ERR && shutdown.result == CB_SUCCESS);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_CLOSED);
	assert(__atomic_load_n(&evidence.rendezvous_fail_requested,
		__ATOMIC_ACQUIRE));
	assert(!__atomic_load_n(&evidence.rendezvous_ack_required,
		__ATOMIC_ACQUIRE));

	evidence = (struct smm_invocation_evidence) {
		.state = SMM_INVOCATION_READY,
		.bsp_cpu = 0,
		.closed_generation = 7,
		.closed_loader_instance_nonce = NONCE(9),
		.closed_lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	shutdown = (struct shutdown_arg) { .evidence = &evidence };
	struct eos_consume_arg consume = {
		.evidence = &evidence,
		.generation = 7,
		.loader_instance_nonce = NONCE(9),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.cpu = 0,
	};
	evidence_hook_point = 12;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner, NULL, eos_consume_thread, &consume));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&closer, NULL, shutdown_thread, &shutdown));
	while (!smm_invocation_evidence_shutdown_requested(&evidence))
		__asm__ volatile ("pause");
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner, NULL));
	assert(!pthread_join(closer, NULL));
	assert(!consume.result && shutdown.result == CB_SUCCESS);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_CLOSED);
	assert(!__atomic_load_n(&evidence.closed_eos_consumed,
		__ATOMIC_ACQUIRE));

	const enum smm_invocation_evidence_phase stale_phases[] = {
		SMM_INVOCATION_READY,
		SMM_INVOCATION_ACK_ARMING,
		SMM_INVOCATION_EOS_ADMITTING,
		SMM_INVOCATION_CLAIMING,
	};
	for (size_t i = 0; i < ARRAY_SIZE(stale_phases); i++) {
		evidence = (struct smm_invocation_evidence) {
			.state = stale_phases[i],
			.generation = 7,
		};
		assert(smm_invocation_evidence_rendezvous_fail(&evidence, 7) ==
			CB_ERR);
		assert(smm_invocation_evidence_phase(&evidence) ==
			stale_phases[i]);
		assert(!__atomic_load_n(&evidence.rendezvous_fail_requested,
			__ATOMIC_ACQUIRE));
	}

	memset(&evidence, 0, sizeof(evidence));
	seed.active_cpus = 2;
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(7),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) ==
		CB_SUCCESS);
	uint64_t generation;
	assert(test_arrive(&evidence, 0, 3,
		&generation) == CB_SUCCESS);
	struct evidence_arrival_arg second = {
		.evidence = &evidence, .cpu = 1, .apic_id = 7,
	};
	evidence_hook_point = 5;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner, NULL, evidence_arrival_thread, &second));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	admission = (struct smm_invocation_admission_token) {
		.evidence_identity = (uintptr_t)&evidence,
		.attempt_nonce = __atomic_load_n(&evidence.state,
			__ATOMIC_ACQUIRE) >> 12,
		.loader_instance_nonce = NONCE(7),
		.invocation_generation = generation,
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.kind = SMM_INVOCATION_ADMISSION_ARRIVE,
	};
	assert(smm_invocation_evidence_admission_fail(&evidence, &admission) ==
		CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONING);
	assert(__atomic_load_n(&evidence.rendezvous_fail_requested,
		__ATOMIC_ACQUIRE));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner, NULL));
	assert(second.result == CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_reset_paths(void)
{
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 2,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(7),
		.participant_apic_ids = { 0x12345, 0xabcdef },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_entry_cause cause = {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
		.size = sizeof(cause),
		.loader_instance_nonce = NONCE(7),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.command = 0xa5,
		.recognized = 1,
	};
	uint8_t reset_code = 73;
	fail_stop_exit_code = reset_code;
	struct smm_invocation_entry_policy policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(policy),
		.max_polls = 2,
	};
	pid_t child = fork();

	assert(child >= 0);
	if (!child) {
		struct smm_invocation_evidence evidence = { 0 };
		struct smm_invocation_entry_ticket ticket;

		assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
		(void)smm_invocation_entry_arrive(&evidence, &cause, &policy, NONCE(7),
			0xa5, 0, 0x12345, &ticket);
		abort();
	}
	expect_exit(child, reset_code);

	reset_code = 74;
	fail_stop_exit_code = reset_code;
	seed.active_cpus = 1;
	child = fork();
	assert(child >= 0);
	if (!child) {
		struct smm_invocation_evidence evidence = { 0 };
		struct smm_invocation_entry_ticket ticket;

		assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
		(void)smm_invocation_entry_arrive(&evidence, &cause, &policy, NONCE(7),
			0xa5, 0, 0x54321, &ticket);
		abort();
	}
	expect_exit(child, reset_code);

	reset_code = 75;
	fail_stop_exit_code = reset_code;
	seed.active_cpus = 2;
	policy.max_polls = 1000000;
	child = fork();
	assert(child >= 0);
	if (!child) {
		struct smm_invocation_evidence evidence = { 0 };
		struct arrival_arg arrival = {
			.evidence = &evidence,
			.cause = &cause,
			.policy = &policy,
			.cpu = 0,
			.apic_id = 0x12345,
		};
		pthread_t thread;

		assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
		entry_hook_point = 2;
		entry_hook_cpu = 0;
		entry_hook_entered = 0;
		entry_hook_release = 0;
		assert(!pthread_create(&thread, NULL, arrival_thread, &arrival));
		while (!__atomic_load_n(&entry_hook_entered, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		cause.command ^= 1U;
		__atomic_store_n(&entry_hook_release, 1U, __ATOMIC_RELEASE);
		(void)pthread_join(thread, NULL);
		abort();
	}
	expect_exit(child, reset_code);

	reset_code = 76;
	fail_stop_exit_code = reset_code;
	child = fork();
	assert(child >= 0);
	if (!child) {
		struct smm_invocation_evidence evidence = { 0 };
		struct arrival_arg arrival = {
			.evidence = &evidence,
			.cause = &cause,
			.policy = &policy,
			.cpu = 0,
			.apic_id = 0x12345,
		};
		pthread_t thread;

		assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
		entry_hook_point = 2;
		entry_hook_cpu = 0;
		entry_hook_entered = 0;
		entry_hook_release = 0;
		assert(!pthread_create(&thread, NULL, arrival_thread, &arrival));
		while (!__atomic_load_n(&entry_hook_entered, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		policy.max_polls--;
		__atomic_store_n(&entry_hook_release, 1U, __ATOMIC_RELEASE);
		(void)pthread_join(thread, NULL);
		abort();
	}
	expect_exit(child, reset_code);

	reset_code = 77;
	fail_stop_exit_code = reset_code;
	seed.active_cpus = 1;
	child = fork();
	assert(child >= 0);
	if (!child) {
		struct smm_invocation_evidence evidence = { 0 };
		em64t101_smm_state_save_area_t state = { 0 };
		struct intel_smm_invocation_adapter adapter;
		struct smm_invocation_save_state_ops ops;
		uintptr_t top = (uintptr_t)&state + sizeof(state);
		struct smm_invocation_token token;
		struct arrival_arg arrival = {
			.evidence = &evidence, .cause = &cause, .policy = &policy,
			.cpu = 0, .apic_id = 0x12345,
		};
		pthread_t thread;

		assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
		state.smm_revision = REV101;
		state.io_misc_info = EXACT_IO;
		state.rax = 0xa5;
		assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
			sizeof(state), REV101) == CB_SUCCESS);
		assert(intel_smm_invocation_adapter_ops(&adapter, &ops) ==
			CB_SUCCESS);
		entry_hook_point = 3;
		entry_hook_cpu = 0;
		entry_hook_entered = 0;
		entry_hook_release = 0;
		assert(!pthread_create(&thread, NULL, arrival_thread, &arrival));
		while (!__atomic_load_n(&entry_hook_entered, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		cause.loader_instance_nonce.high++;
		assert(smm_invocation_evidence_claim(&evidence, 0xa5,
			0x11223344556677a5ULL, &ops, &token) == CB_ERR);
		__atomic_store_n(&entry_hook_release, 1U, __ATOMIC_RELEASE);
		(void)pthread_join(thread, NULL);
		abort();
	}
	expect_exit(child, reset_code);

	reset_code = 78;
	fail_stop_exit_code = reset_code;
	child = fork();
	assert(child >= 0);
	if (!child) {
		struct smm_invocation_evidence evidence = { 0 };
		struct arrival_arg arrival = {
			.evidence = &evidence, .cause = &cause, .policy = &policy,
			.cpu = 0, .apic_id = 0x12345,
		};
		pthread_t thread;

		assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
		entry_hook_point = 3;
		entry_hook_cpu = 0;
		entry_hook_entered = 0;
		entry_hook_release = 0;
		assert(!pthread_create(&thread, NULL, arrival_thread, &arrival));
		while (!__atomic_load_n(&entry_hook_entered, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		arrival.ticket.generation++;
		__atomic_store_n(&entry_hook_release, 1U, __ATOMIC_RELEASE);
		(void)pthread_join(thread, NULL);
		abort();
	}
	expect_exit(child, reset_code);
	entry_hook_point = 0;
	entry_hook_cpu = UINT32_MAX;
	entry_hook_release = 1;
	fail_stop_exit_code = 0;
}

static void test_bounded_transient_resets(void)
{
	for (uint8_t mode = 0; mode < 5; mode++) {
		uint8_t reset_code = (uint8_t)(81U + mode);
		fail_stop_exit_code = reset_code;
		pid_t child = fork();

		assert(child >= 0);
		if (!child) {
			struct smm_invocation_evidence evidence = { 0 };
			struct smm_invocation_loader_seed seed = {
				.revision = SMM_INVOCATION_EVIDENCE_REVISION,
				.size = sizeof(seed),
				.active_cpus = 2,
				.bsp_cpu = 0,
				.loader_instance_nonce = NONCE(7),
				.participant_apic_ids = { 3, 7 },
				.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
			};
			struct smm_invocation_entry_cause cause = {
				.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
				.size = sizeof(cause),
				.loader_instance_nonce = NONCE(7),
				.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
				.command = 0xa5,
				.recognized = 1,
			};
			struct smm_invocation_entry_policy policy = {
				.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
				.size = sizeof(policy),
				.max_polls = 2,
			};
			struct smm_invocation_entry_ticket ticket;
			struct ack_arm_arg arm = {
				.evidence = &evidence,
				.loader_instance_nonce = NONCE(7),
				.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
			};
			struct evidence_arrival_arg evidence_arrival = {
				.evidence = &evidence,
				.cpu = 0,
				.apic_id = 3,
			};
			pthread_t owner;

			assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
			evidence_hook_entered = 0;
			evidence_hook_release = 0;
			if (mode == 0) {
				evidence_hook_point = 9;
				assert(!pthread_create(&owner, NULL, ack_arm_thread,
					&arm));
			} else if (mode < 4) {
				assert(test_arm(
					&evidence, NONCE(7), SMM_INVOCATION_LOADER_NON_S3_LOAD) ==
					CB_SUCCESS);
				evidence_hook_point = mode == 1 ? 1 :
					(mode == 2 ? 5 : 7);
				if (mode == 2)
					assert(test_arrive(&evidence,
						0, 3, &evidence_arrival.generation) ==
						CB_SUCCESS);
				if (mode == 2) {
					evidence_arrival.cpu = 1;
					evidence_arrival.apic_id = 7;
				}
				assert(!pthread_create(&owner, NULL,
					evidence_arrival_thread, &evidence_arrival));
			} else {
				__atomic_store_n(&evidence.state,
					SMM_INVOCATION_ACK_ADMITTING,
					__ATOMIC_RELEASE);
				__atomic_store_n(&evidence.generation, 1U,
					__ATOMIC_RELEASE);
				smm_invocation_entry_test_ack_wait(&evidence, 1, 0,
					&policy);
				abort();
			}
			while (!__atomic_load_n(&evidence_hook_entered,
				__ATOMIC_ACQUIRE))
				__asm__ volatile ("pause");
			(void)smm_invocation_entry_arrive(&evidence, &cause, &policy,
				NONCE(7), 0xa5, mode == 2 ? 0 : 1,
				mode == 2 ? 3 : 7, &ticket);
			abort();
		}
		expect_exit(child, reset_code);
	}
	fail_stop_exit_code = 0;
	evidence_hook_point = 0;
	evidence_hook_release = 1;
	entry_barrier_target = 0;
}

static void test_admission_token_replay_across_invocations(void)
{
	em64t101_smm_state_save_area_t state = { 0 };
	uintptr_t top = (uintptr_t)&state + sizeof(state);
	struct intel_smm_invocation_adapter adapter;
	struct smm_invocation_save_state_ops ops;
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(43),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_admission_token arm;
	struct smm_invocation_admission_token arrive;
	struct smm_invocation_admission_token ack;
	struct smm_invocation_admission_token current;
	struct smm_invocation_evidence snapshot;
	struct smm_invocation_token claim;
	uint64_t generation;

	state.smm_revision = REV101;
	state.io_misc_info = EXACT_IO;
	state.rax = 0xa5;
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(state), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence, NONCE(43),
		SMM_INVOCATION_LOADER_NON_S3_LOAD, &arm) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
		&generation, &arrive) == SMM_INVOCATION_TRY_SUCCESS);
	arrive.invocation_generation = generation;
	assert(smm_invocation_evidence_rendezvous_ack_try(&evidence, generation,
		0, &ack) == SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_claim(&evidence, 0xa5,
		0x11223344556677a5ULL, &ops, &claim) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(&evidence, &claim, &ops) ==
		CB_SUCCESS);
	assert(smm_invocation_evidence_depart_try(&evidence, 0, generation) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_READY);
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	assert(smm_invocation_evidence_admission_fail(&evidence, &arm) == CB_ERR);
	assert(smm_invocation_evidence_admission_fail(&evidence, &arrive) ==
		CB_ERR);
	assert(smm_invocation_evidence_admission_fail(&evidence, &ack) == CB_ERR);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));

	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence, NONCE(43),
		SMM_INVOCATION_LOADER_NON_S3_LOAD, &current) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
		&generation, &current) == SMM_INVOCATION_TRY_SUCCESS);
	assert(generation == 2);
	assert(smm_invocation_evidence_rendezvous_ack_try(&evidence, generation,
		0, &current) == SMM_INVOCATION_TRY_SUCCESS);
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	assert(smm_invocation_evidence_admission_fail(&evidence, &arm) == CB_ERR);
	assert(smm_invocation_evidence_admission_fail(&evidence, &arrive) ==
		CB_ERR);
	assert(smm_invocation_evidence_admission_fail(&evidence, &ack) == CB_ERR);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));
	assert(smm_invocation_evidence_admission_fail(&evidence, &current) ==
		CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	assert(smm_invocation_evidence_admission_fail(&evidence, &current) ==
		CB_ERR);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));
}

static void test_departure_timeout_suppresses_owner(void)
{
	em64t101_smm_state_save_area_t state = {
		.smm_revision = REV101,
		.io_misc_info = EXACT_IO,
		.rax = 0xa5,
	};
	const uintptr_t top = (uintptr_t)&state + sizeof(state);
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(61),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_evidence terminal;
	struct intel_smm_invocation_adapter adapter;
	struct smm_invocation_save_state_ops ops;
	struct smm_invocation_token token;
	struct smm_invocation_admission_token admission;
	struct smm_invocation_entry_ticket ticket = {
		.loader_instance_nonce = NONCE(61),
		.cpu = 0,
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.max_polls = 1,
		.command = 0xa5,
	};
	struct departure_try_arg owner = {
		.evidence = &evidence, .cpu = 0,
	};
	struct ticket_call_arg timeout = {
		.evidence = &evidence, .ticket = &ticket,
	};
	pthread_t owner_thread;
	pthread_t timeout_thread;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence, NONCE(61),
		SMM_INVOCATION_LOADER_NON_S3_LOAD, &admission) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
		&ticket.generation, &admission) == SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_rendezvous_ack_try(&evidence,
		ticket.generation, 0, &admission) == SMM_INVOCATION_TRY_SUCCESS);
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(state), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(smm_invocation_evidence_claim(&evidence, 0xa5,
		0x11223344556677a5ULL, &ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(&evidence, &token, &ops) ==
		CB_SUCCESS);
	owner.generation = ticket.generation;
	evidence_hook_point = 6;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	evidence_hook_point_secondary = 26;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&owner_thread, NULL, departure_try_thread,
		&owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	fail_stop_thread_exit = 1;
	assert(!pthread_create(&timeout_thread, NULL, depart_thread, &timeout));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(__atomic_load_n(&evidence.departure_failed, __ATOMIC_ACQUIRE));
	assert(!smm_invocation_evidence_shutdown_requested(&evidence));
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_DEPARTURE_FAIL_ADMITTING);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_ERROR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_DEPARTURE_FAIL_ADMITTING);
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(timeout_thread, NULL));
	assert(__atomic_load_n(&fail_stop_calls, __ATOMIC_RELAXED) == 1U);
	__atomic_store_n(&fail_stop_calls, 0U, __ATOMIC_RELAXED);
	fail_stop_thread_exit = 0;
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	assert(!__atomic_load_n(&evidence.departed_cpus, __ATOMIC_ACQUIRE));
	memcpy(&terminal, &evidence, sizeof(terminal));
	assert(smm_invocation_evidence_ticket_fail(&evidence,
		ticket.generation) == CB_ERR);
	assert(!memcmp(&terminal, &evidence, sizeof(terminal)));

	evidence_hook_point = 0;
	evidence_hook_release = 1;
	evidence_hook_point_secondary = 0;
	evidence_hook_release_secondary = 1;
}

static uint64_t close_single_cpu_invocation(
	struct smm_invocation_evidence *evidence, struct smm_invocation_loader_instance_nonce loader_instance_nonce)
{
	em64t101_smm_state_save_area_t state = {
		.smm_revision = REV101,
		.io_misc_info = EXACT_IO,
		.rax = 0xa5,
	};
	const uintptr_t top = (uintptr_t)&state + sizeof(state);
	struct intel_smm_invocation_adapter adapter;
	struct smm_invocation_save_state_ops ops;
	struct smm_invocation_token token;
	struct smm_invocation_admission_token admission;
	uint64_t generation;

	assert(smm_invocation_evidence_require_rendezvous_ack_try(evidence,
		loader_instance_nonce, SMM_INVOCATION_LOADER_NON_S3_LOAD, &admission) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_arrive_try(evidence, 0, 3,
		&generation, &admission) == SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_rendezvous_ack_try(evidence,
		generation, 0, &admission) == SMM_INVOCATION_TRY_SUCCESS);
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(state), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(smm_invocation_evidence_claim(evidence, 0xa5,
		0x11223344556677a5ULL, &ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(evidence, &token, &ops) ==
		CB_SUCCESS);
	return generation;
}

static void finish_single_cpu_collecting(
	struct smm_invocation_evidence *evidence, uint64_t generation)
{
	em64t101_smm_state_save_area_t state = {
		.smm_revision = REV101,
		.io_misc_info = EXACT_IO,
		.rax = 0xa5,
	};
	const uintptr_t top = (uintptr_t)&state + sizeof(state);
	struct intel_smm_invocation_adapter adapter;
	struct smm_invocation_save_state_ops ops;
	struct smm_invocation_token token;
	struct smm_invocation_admission_token admission;

	assert(smm_invocation_evidence_rendezvous_ack_try(evidence,
		generation, 0, &admission) == SMM_INVOCATION_TRY_SUCCESS);
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(state), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(smm_invocation_evidence_claim(evidence, 0xa5,
		0x11223344556677a5ULL, &ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(evidence, &token, &ops) ==
		CB_SUCCESS);
	assert(smm_invocation_evidence_depart_try(evidence, 0, generation) ==
		SMM_INVOCATION_TRY_SUCCESS);
}

static uint64_t prepare_single_cpu_departure(
	struct smm_invocation_evidence *evidence, struct smm_invocation_loader_instance_nonce loader_instance_nonce)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = loader_instance_nonce,
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};

	assert(smm_invocation_evidence_provision(evidence, &seed) == CB_SUCCESS);
	return close_single_cpu_invocation(evidence, loader_instance_nonce);
}

static void test_departure_failure_final_arbitration(void)
{
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_evidence snapshot;
	struct departure_try_arg owner = {
		.evidence = &evidence,
		.cpu = 0,
	};
	pthread_t owner_thread;

	owner.generation = prepare_single_cpu_departure(&evidence, NONCE(62));
	evidence_hook_point = 28;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner_thread, NULL, departure_try_thread,
		&owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_ticket_fail(&evidence,
		owner.generation) == CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_DEPARTURE_FAIL_ADMITTING ||
	       smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONING);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_ERROR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);
	assert(!__atomic_load_n(&evidence.departed_cpus, __ATOMIC_ACQUIRE));

	memset(&evidence, 0, sizeof(evidence));
	owner.generation = prepare_single_cpu_departure(&evidence, NONCE(63));
	evidence_hook_point = 29;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner_thread, NULL, departure_try_thread,
		&owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_DEPARTURE_COMMITTING);
	assert(smm_invocation_evidence_ticket_fail(&evidence,
		owner.generation) == CB_ERR);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_READY);
	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_stale_generation_cannot_claim_repeated_phase(void)
{
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_evidence snapshot;
	struct generation_call_arg failure = { .evidence = &evidence };
	struct departure_try_arg departure = {
		.evidence = &evidence,
		.cpu = 0,
	};
	struct rendezvous_call_arg rendezvous = {
		.evidence = &evidence,
	};
	struct eos_consume_arg eos = {
		.evidence = &evidence,
		.loader_instance_nonce = NONCE(69),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.cpu = 0,
	};
	struct smm_invocation_admission_token admission;
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(68),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	pthread_t thread;
	uint64_t next_generation;

	failure.generation = prepare_single_cpu_departure(&evidence, NONCE(65));
	evidence_hook_point = 31;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, ticket_fail_thread, &failure));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_depart_try(&evidence, 0,
		failure.generation) == SMM_INVOCATION_TRY_SUCCESS);
	next_generation = close_single_cpu_invocation(&evidence, NONCE(65));
	assert(next_generation != failure.generation);
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(failure.result == CB_ERR);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));

	memset(&evidence, 0, sizeof(evidence));
	departure.generation = prepare_single_cpu_departure(&evidence, NONCE(66));
	evidence_hook_point = 32;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, departure_try_thread,
		&departure));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_depart_try(&evidence, 0,
		departure.generation) == SMM_INVOCATION_TRY_SUCCESS);
	next_generation = close_single_cpu_invocation(&evidence, NONCE(66));
	assert(next_generation != departure.generation);
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(departure.result == SMM_INVOCATION_TRY_RETRY);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
		NONCE(68), SMM_INVOCATION_LOADER_NON_S3_LOAD, &admission) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
		&rendezvous.generation, &admission) ==
		SMM_INVOCATION_TRY_SUCCESS);
	evidence_hook_point = 11;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, rendezvous_fail_thread,
		&rendezvous));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	finish_single_cpu_collecting(&evidence, rendezvous.generation);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
		NONCE(68), SMM_INVOCATION_LOADER_NON_S3_LOAD, &admission) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
		&next_generation, &admission) == SMM_INVOCATION_TRY_SUCCESS);
	assert(next_generation != rendezvous.generation);
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(rendezvous.result == CB_ERR);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));

	memset(&evidence, 0, sizeof(evidence));
	eos.generation = prepare_single_cpu_departure(&evidence, NONCE(69));
	assert(smm_invocation_evidence_depart_try(&evidence, 0,
		eos.generation) == SMM_INVOCATION_TRY_SUCCESS);
	evidence_hook_point = 33;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, eos_consume_thread, &eos));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_eos_consume(&evidence, eos.generation,
		NONCE(69), SMM_INVOCATION_LOADER_NON_S3_LOAD, 0));
	next_generation = close_single_cpu_invocation(&evidence, NONCE(69));
	assert(next_generation != eos.generation);
	assert(smm_invocation_evidence_depart_try(&evidence, 0,
		next_generation) == SMM_INVOCATION_TRY_SUCCESS);
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(!eos.result);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));
	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_stale_shutdown_cannot_dirty_terminal(void)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(67),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_evidence terminal;
	struct shutdown_arg stale = { .evidence = &evidence };
	pthread_t thread;
	uint32_t state;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	state = __atomic_load_n(&evidence.state, __ATOMIC_ACQUIRE);
	__atomic_store_n(&evidence.state,
		(state & ~0x1fU) | SMM_INVOCATION_PROVISIONING,
		__ATOMIC_RELEASE);
	evidence_hook_point = 30;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&thread, NULL, shutdown_thread, &stale));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	state = __atomic_load_n(&evidence.state, __ATOMIC_ACQUIRE);
	__atomic_store_n(&evidence.state,
		(state & ~0x1fU) | SMM_INVOCATION_READY, __ATOMIC_RELEASE);
	assert(smm_invocation_evidence_shutdown(&evidence) == CB_SUCCESS);
	memcpy(&terminal, &evidence, sizeof(terminal));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(stale.result == CB_SUCCESS);
	assert(!memcmp(&terminal, &evidence, sizeof(terminal)));
	assert(__atomic_load_n(&evidence.state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_CLOSED);
	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_packed_latches_and_terminal_shutdown(void)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(71),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_evidence snapshot;
	struct smm_invocation_admission_token admission;
	struct provision_arg provision = {
		.evidence = &evidence,
		.seed = &seed,
	};
	struct shutdown_arg stale = { .evidence = &evidence };
	struct shutdown_arg owner = { .evidence = &evidence };
	pthread_t first;
	pthread_t second;
	uint64_t generation;
	uint32_t state;

	evidence_hook_point = 36;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	evidence_hook_point_secondary = 37;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&first, NULL, provision_thread, &provision));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&second, NULL, shutdown_thread, &owner));
	while (!smm_invocation_evidence_shutdown_requested(&evidence))
		__asm__ volatile ("pause");
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(second, NULL));
	assert(provision.result == CB_ERR && owner.result == CB_SUCCESS);
	assert(__atomic_load_n(&evidence.state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_CLOSED);

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	__atomic_fetch_or(&evidence.state, TEST_SHUTDOWN_REQUESTED,
		__ATOMIC_ACQ_REL);
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
		NONCE(71), SMM_INVOCATION_LOADER_NON_S3_LOAD, &admission) ==
		SMM_INVOCATION_TRY_ERROR);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
		NONCE(71), SMM_INVOCATION_LOADER_NON_S3_LOAD, &admission) ==
		SMM_INVOCATION_TRY_SUCCESS);
	__atomic_fetch_or(&evidence.state, TEST_SHUTDOWN_REQUESTED,
		__ATOMIC_ACQ_REL);
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
		&generation, &admission) == SMM_INVOCATION_TRY_ERROR);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
		NONCE(71), SMM_INVOCATION_LOADER_NON_S3_LOAD, &admission) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
		&generation, &admission) == SMM_INVOCATION_TRY_SUCCESS);
	__atomic_fetch_or(&evidence.state, TEST_SHUTDOWN_REQUESTED,
		__ATOMIC_ACQ_REL);
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	assert(smm_invocation_evidence_rendezvous_ack_try(&evidence,
		generation, 0, &admission) == SMM_INVOCATION_TRY_ERROR);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
		NONCE(71), SMM_INVOCATION_LOADER_NON_S3_LOAD, &admission) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
		&generation, &admission) == SMM_INVOCATION_TRY_SUCCESS);
	evidence_hook_point = 37;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&first, NULL, shutdown_thread, &stale));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	owner = (struct shutdown_arg) { .evidence = &evidence };
	assert(!pthread_create(&second, NULL, shutdown_thread, &owner));
	while (smm_invocation_evidence_phase(&evidence) !=
		SMM_INVOCATION_CLOSING)
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_depart_try(&evidence, 0, generation) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(!pthread_join(second, NULL));
	assert(owner.result == CB_SUCCESS);
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(stale.result == CB_SUCCESS);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));
	assert(__atomic_load_n(&evidence.state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_CLOSED);

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	evidence_hook_point = 38;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	owner = (struct shutdown_arg) { .evidence = &evidence };
	assert(!pthread_create(&first, NULL, shutdown_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	fail_stop_thread_exit = 1;
	__atomic_store_n(&fail_stop_calls, 0U, __ATOMIC_RELAXED);
	stale = (struct shutdown_arg) { .evidence = &evidence };
	assert(!pthread_create(&second, NULL, shutdown_thread, &stale));
	assert(!pthread_join(second, NULL));
	assert(__atomic_load_n(&fail_stop_calls, __ATOMIC_RELAXED) == 1U);
	fail_stop_thread_exit = 0;
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(owner.result == CB_SUCCESS);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_CLOSED);

	fail_stop_exit_code = 97;
	__atomic_store_n(&fail_stop_calls, 0U, __ATOMIC_RELAXED);
	pid_t child = fork();
	assert(child >= 0);
	if (!child) {
		memset(&evidence, 0, sizeof(evidence));
		assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
		evidence_hook_point = 38;
		evidence_hook_entered = 0;
		evidence_hook_release = 0;
		owner = (struct shutdown_arg) { .evidence = &evidence };
		assert(!pthread_create(&first, NULL, shutdown_thread, &owner));
		while (!__atomic_load_n(&evidence_hook_entered,
			__ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		(void)smm_invocation_evidence_shutdown(&evidence);
		_exit(0);
	}
	expect_exit(child, fail_stop_exit_code);
	fail_stop_exit_code = 0;
	__atomic_store_n(&fail_stop_calls, 0U, __ATOMIC_RELAXED);

	evidence_hook_point = 0;
	evidence_hook_release = 1;
	evidence_hook_point_secondary = 0;
	evidence_hook_release_secondary = 1;
	state = __atomic_load_n(&snapshot.state, __ATOMIC_ACQUIRE);
	assert(!(state & (TEST_ADMISSION_BUSY | TEST_SHUTDOWN_REQUESTED)));
}

static void test_provision_rejects_dirty_empty_state(void)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(73),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	const uint32_t dirty_states[] = {
		1U << 5,
		2U << 5,
		3U << 5,
		TEST_ADMISSION_BUSY,
		TEST_ADMISSION_CONSUMED,
		TEST_SHUTDOWN_REQUESTED,
		TEST_REENTRY_DETECTED,
		TEST_CLOSE_REQUESTED,
		TEST_ADMISSION_NONCE_ONE,
		(2U << 5) | TEST_ADMISSION_BUSY | TEST_ADMISSION_NONCE_ONE,
		TEST_ADMISSION_CONSUMED | TEST_SHUTDOWN_REQUESTED |
			TEST_REENTRY_DETECTED | TEST_CLOSE_REQUESTED |
			(0xfffffU << 12),
	};

	for (size_t i = 0; i < ARRAY_SIZE(dirty_states); i++) {
		struct smm_invocation_evidence evidence = { 0 };
		struct smm_invocation_evidence snapshot;

		__atomic_store_n(&evidence.state, dirty_states[i],
			__ATOMIC_RELAXED);
		memcpy(&snapshot, &evidence, sizeof(snapshot));
		assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_ERR);
		assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));
	}
}

static void test_cleanup_claim_retries_packed_latch(void)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(74),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct rendezvous_call_arg failure = { .evidence = &evidence };
	struct departure_try_arg departure = {
		.evidence = &evidence,
		.cpu = 0,
	};
	pthread_t owner_thread;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, seed.loader_instance_nonce, seed.lifecycle) ==
		CB_SUCCESS);
	assert(test_arrive(&evidence, 0, 3, &failure.generation) ==
		CB_SUCCESS);
	evidence_hook_point = 39;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner_thread, NULL, rendezvous_fail_thread,
		&failure));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_fetch_or(&evidence.state, TEST_SHUTDOWN_REQUESTED,
		__ATOMIC_ACQ_REL);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(failure.result == CB_ERR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_POISONED);

	memset(&evidence, 0, sizeof(evidence));
	departure.generation = prepare_single_cpu_departure(&evidence,
		seed.loader_instance_nonce);
	evidence_hook_point = 40;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner_thread, NULL, departure_try_thread,
		&departure));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_fetch_or(&evidence.state, TEST_SHUTDOWN_REQUESTED,
		__ATOMIC_ACQ_REL);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(departure.result == SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_CLOSED);
	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_phase_claim_retries_packed_latch(void)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(75),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct admission_try_arg operation;
	struct shutdown_arg shutdown;
	pthread_t owner_thread;
	pthread_t shutdown_thread_id;
	uint64_t generation;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, seed.loader_instance_nonce, seed.lifecycle) ==
		CB_SUCCESS);
	operation = (struct admission_try_arg) {
		.evidence = &evidence,
		.cpu = 0,
		.apic_id = 3,
	};
	shutdown = (struct shutdown_arg) { .evidence = &evidence };
	evidence_hook_point = 41;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner_thread, NULL, arrival_try_thread,
		&operation));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	generation = __atomic_load_n(&evidence.generation, __ATOMIC_ACQUIRE);
	__atomic_fetch_or(&evidence.state, TEST_SHUTDOWN_REQUESTED,
		__ATOMIC_ACQ_REL);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(operation.result == SMM_INVOCATION_TRY_ERROR);
	assert(!pthread_create(&shutdown_thread_id, NULL, shutdown_thread,
		&shutdown));
	while (smm_invocation_evidence_phase(&evidence) !=
	       SMM_INVOCATION_CLOSING)
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_depart_try(&evidence, 0,
		generation) == SMM_INVOCATION_TRY_SUCCESS);
	assert(!pthread_join(shutdown_thread_id, NULL));
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_CLOSED);

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	operation = (struct admission_try_arg) {
		.evidence = &evidence,
		.loader_instance_nonce = seed.loader_instance_nonce,
		.lifecycle = seed.lifecycle,
	};
	operation.loader_instance_nonce.high++;
	shutdown = (struct shutdown_arg) { .evidence = &evidence };
	evidence_hook_point = 42;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner_thread, NULL, ack_arm_try_thread,
		&operation));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_fetch_or(&evidence.state, TEST_SHUTDOWN_REQUESTED,
		__ATOMIC_ACQ_REL);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(smm_invocation_evidence_shutdown(&evidence) == CB_SUCCESS);
	assert(operation.result == SMM_INVOCATION_TRY_ERROR);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_CLOSED);

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, seed.loader_instance_nonce, seed.lifecycle) ==
		CB_SUCCESS);
	operation = (struct admission_try_arg) {
		.evidence = &evidence,
		.cpu = 1,
	};
	assert(test_arrive(&evidence, 0, 3, &operation.generation) ==
		CB_SUCCESS);
	shutdown = (struct shutdown_arg) { .evidence = &evidence };
	evidence_hook_point = 43;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner_thread, NULL, rendezvous_ack_try_thread,
		&operation));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&shutdown_thread_id, NULL, shutdown_thread,
		&shutdown));
	while (!smm_invocation_evidence_shutdown_requested(&evidence))
		__asm__ volatile ("pause");
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(operation.result == SMM_INVOCATION_TRY_ERROR);
	while (smm_invocation_evidence_phase(&evidence) !=
	       SMM_INVOCATION_CLOSING)
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_depart_try(&evidence, 0,
		operation.generation) == SMM_INVOCATION_TRY_SUCCESS);
	assert(!pthread_join(shutdown_thread_id, NULL));
	assert(shutdown.result == CB_SUCCESS);
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_CLOSED);
	evidence_hook_point = 0;
	evidence_hook_release = 1;
}

static void test_stale_reentry_cannot_dirty_next_state(void)
{
	em64t101_smm_state_save_area_t state = {
		.smm_revision = REV101,
		.io_misc_info = EXACT_IO,
		.rax = 0xa5,
	};
	const uintptr_t top = (uintptr_t)&state + sizeof(state);
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(68),
		.participant_apic_ids = { 3 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_evidence snapshot;
	struct intel_smm_invocation_adapter adapter;
	struct smm_invocation_save_state_ops ops;
	struct smm_invocation_admission_token admission;
	struct claim_arg owner = { .evidence = &evidence, .ops = &ops };
	struct claim_arg stale = { .evidence = &evidence, .ops = &ops };
	pthread_t owner_thread;
	pthread_t stale_thread;
	uint64_t generation;

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence, NONCE(68),
		SMM_INVOCATION_LOADER_NON_S3_LOAD, &admission) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_arrive_try(&evidence, 0, 3,
		&generation, &admission) == SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_rendezvous_ack_try(&evidence,
		generation, 0, &admission) == SMM_INVOCATION_TRY_SUCCESS);
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(state), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	evidence_hook_point = 2;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	assert(!pthread_create(&owner_thread, NULL, claim_thread, &owner));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	evidence_hook_point_secondary = 34;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&stale_thread, NULL, claim_thread, &stale));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner_thread, NULL));
	assert(owner.result == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(&evidence, &owner.token, &ops) ==
		CB_SUCCESS);
	assert(smm_invocation_evidence_depart_try(&evidence, 0, generation) ==
		SMM_INVOCATION_TRY_SUCCESS);
	memcpy(&snapshot, &evidence, sizeof(snapshot));
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(stale_thread, NULL));
	assert(stale.result == CB_ERR);
	assert(!memcmp(&snapshot, &evidence, sizeof(snapshot)));
	evidence_hook_point = 0;
	evidence_hook_release = 1;
	evidence_hook_point_secondary = 0;
	evidence_hook_release_secondary = 1;
}

static void test_rendezvous_and_departure(void)
{
	em64t101_smm_state_save_area_t states[2] = { 0 };
	uintptr_t tops[2] = {
		(uintptr_t)&states[0] + sizeof(states[0]),
		(uintptr_t)&states[1] + sizeof(states[1]),
	};
	struct intel_smm_invocation_adapter adapter;
	struct smm_invocation_save_state_ops ops;
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 2,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(7),
		.participant_apic_ids = { 0x12345, 0xabcdef },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	const struct smm_invocation_entry_cause cause = {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
		.size = sizeof(cause),
		.loader_instance_nonce = NONCE(7),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.command = 0xa5,
		.recognized = 1,
	};
	const struct smm_invocation_entry_policy policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION,
		.size = sizeof(policy),
		.max_polls = 1000000,
	};
	struct arrival_arg arrivals[2] = {
		{ .evidence = &evidence, .cause = &cause, .policy = &policy,
		  .cpu = 0,
		  .apic_id = 0x12345 },
		{ .evidence = &evidence, .cause = &cause, .policy = &policy,
		  .cpu = 1,
		  .apic_id = 0xabcdef },
	};
	struct smm_invocation_token token;
	struct ack_ready_arg slow_observer;
	pthread_t observer;
	uint64_t generations[2];
	uintptr_t duplicate_tops[2] = { tops[0], tops[0] };

	assert(intel_smm_invocation_adapter_init(&adapter, 2, duplicate_tops,
		sizeof(states[0]), REV101) == CB_ERR);

	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	states[0].smm_revision = REV101;
	states[0].io_misc_info = EXACT_IO;
	states[0].rax = 0xa5;
	states[1].smm_revision = REV101;
	states[1].rax = 0;
	assert(intel_smm_invocation_adapter_init(&adapter, 2, tops,
		sizeof(states[0]), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(7),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) == CB_SUCCESS);
	for (uint32_t cpu = 0; cpu < 2; cpu++)
		assert(test_arrive(&evidence, cpu,
			seed.participant_apic_ids[cpu], &generations[cpu]) ==
			CB_SUCCESS);
	assert(generations[0] == generations[1]);
	for (uint32_t cpu = 0; cpu < 2; cpu++)
		assert(test_ack(&evidence,
			generations[cpu], cpu) == CB_SUCCESS);
	for (uint32_t cpu = 0; cpu < 2; cpu++) {
		arrivals[cpu].ticket = (struct smm_invocation_entry_ticket) {
			.generation = generations[cpu],
			.loader_instance_nonce = cause.loader_instance_nonce,
			.cpu = cpu,
			.lifecycle = cause.lifecycle,
			.max_polls = policy.max_polls,
			.command = cause.command,
		};
		arrivals[cpu].result = CB_SUCCESS;
	}
	slow_observer = (struct ack_ready_arg) {
		.evidence = &evidence,
		.generation = generations[1],
	};
	assert(!pthread_create(&observer, NULL, ack_ready_thread,
		&slow_observer));
	assert(smm_invocation_evidence_claim(&evidence, 0xa5,
		0x11223344556677a5ULL, &ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(&evidence, &token, &ops) ==
		CB_SUCCESS);
	__atomic_store_n(&slow_observer.release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(observer, NULL));
	assert(slow_observer.result);
	assert(arrivals[0].result == CB_SUCCESS);
	assert(arrivals[1].result == CB_SUCCESS);
	for (size_t cpu = 0; cpu < 2; cpu++)
		assert(arrivals[cpu].ticket.generation != 0 &&
		       arrivals[cpu].ticket.command == cause.command);
	entry_hook_cpu = UINT32_MAX;
	assert(smm_invocation_entry_depart(&evidence, (void *)&evidence) ==
		CB_ERR);
	pid_t mutation_child = fork();
	assert(mutation_child >= 0);
	if (!mutation_child) {
		struct smm_invocation_entry_ticket mutable = arrivals[1].ticket;
		struct ticket_call_arg call = {
			.evidence = &evidence,
			.ticket = &mutable,
		};
		pthread_t thread;

		fail_stop_thread_exit = 1;
		entry_hook_point = 5;
		entry_hook_cpu = mutable.cpu;
		entry_hook_entered = 0;
		entry_hook_release = 0;
		assert(!pthread_create(&thread, NULL, depart_thread, &call));
		while (!__atomic_load_n(&entry_hook_entered, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		mutable.generation++;
		__atomic_store_n(&entry_hook_release, 1U, __ATOMIC_RELEASE);
		(void)pthread_join(thread, NULL);
		assert(smm_invocation_evidence_phase(&evidence) ==
			SMM_INVOCATION_POISONED);
		assert(!(__atomic_load_n(&evidence.departed_cpus,
			__ATOMIC_ACQUIRE) & (1ULL << mutable.cpu)));
		_exit(0);
	}
	expect_exit(mutation_child, 0);
	assert(!smm_invocation_entry_eos_ready(&evidence,
		&arrivals[0].ticket));
	pid_t stalled_child = fork();
	assert(stalled_child >= 0);
	if (!stalled_child) {
		struct smm_invocation_entry_ticket stalled = arrivals[1].ticket;
		uint8_t reset_code = 96;
		fail_stop_exit_code = reset_code;

		stalled.max_polls = 2;
		__atomic_store_n(&evidence.state,
			SMM_INVOCATION_DEPARTURE_ADMITTING, __ATOMIC_RELEASE);
		(void)smm_invocation_entry_depart(&evidence, &stalled);
		abort();
	}
	expect_exit(stalled_child, 96);
	assert(smm_invocation_entry_depart(&evidence, &arrivals[1].ticket) ==
		CB_SUCCESS);
	assert(!smm_invocation_entry_eos_ready(&evidence,
		&arrivals[0].ticket));
	assert(smm_invocation_entry_depart(&evidence, &arrivals[0].ticket) ==
		CB_SUCCESS);
	assert(!smm_invocation_entry_eos_ready(&evidence, (void *)&evidence));
	mutation_child = fork();
	assert(mutation_child >= 0);
	if (!mutation_child) {
		struct smm_invocation_entry_ticket mutable = arrivals[0].ticket;
		struct ticket_call_arg call = {
			.evidence = &evidence,
			.ticket = &mutable,
		};
		pthread_t thread;

		fail_stop_thread_exit = 1;
		entry_hook_point = 7;
		entry_hook_cpu = mutable.cpu;
		entry_hook_entered = 0;
		entry_hook_release = 0;
		assert(!pthread_create(&thread, NULL, eos_thread, &call));
		while (!__atomic_load_n(&entry_hook_entered, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		mutable.loader_instance_nonce.high++;
		__atomic_store_n(&entry_hook_release, 1U, __ATOMIC_RELEASE);
		(void)pthread_join(thread, NULL);
		assert(smm_invocation_evidence_phase(&evidence) ==
			SMM_INVOCATION_CLOSED);
		assert(!__atomic_load_n(&evidence.closed_eos_consumed,
			__ATOMIC_ACQUIRE));
		_exit(0);
	}
	expect_exit(mutation_child, 0);
	struct smm_invocation_entry_ticket stale = arrivals[0].ticket;
	stale.generation++;
	assert(!smm_invocation_entry_eos_ready(&evidence, &stale));
	stale = arrivals[0].ticket;
	stale.loader_instance_nonce.low++;
	assert(!smm_invocation_entry_eos_ready(&evidence, &stale));
	stale = arrivals[0].ticket;
	stale.reserved[2] = 1U;
	assert(!smm_invocation_entry_eos_ready(&evidence, &stale));
	stale = arrivals[0].ticket;
	stale.loader_instance_nonce.high++;
	assert(!smm_invocation_entry_eos_ready(&evidence, &stale));
	stale = arrivals[0].ticket;
	stale.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	assert(!smm_invocation_entry_eos_ready(&evidence, &stale));
	assert(smm_invocation_entry_eos_ready(&evidence, &arrivals[0].ticket));
	assert(!smm_invocation_entry_eos_ready(&evidence,
		&arrivals[0].ticket));
	assert(!smm_invocation_entry_eos_ready(&evidence,
		&arrivals[1].ticket));
	pid_t child = fork();
	assert(child >= 0);
	if (!child) {
		(void)smm_invocation_entry_depart(&evidence, &arrivals[0].ticket);
		_exit(0);
	}
	expect_signal(child, SIGABRT);
}

static void test_claim_requires_all_acknowledgements(void)
{
	em64t101_smm_state_save_area_t states[2] = { 0 };
	uintptr_t tops[2] = {
		(uintptr_t)&states[0] + sizeof(states[0]),
		(uintptr_t)&states[1] + sizeof(states[1]),
	};
	struct intel_smm_invocation_adapter adapter;
	struct smm_invocation_save_state_ops ops;
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 2,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(9),
		.participant_apic_ids = { 3, 7 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_token token;
	struct claim_arg claim;
	struct rendezvous_call_arg stale_fail;
	pthread_t thread;
	pthread_t fail_thread;
	uint64_t generations[2];

	states[0].smm_revision = REV101;
	states[0].io_misc_info = EXACT_IO;
	states[0].rax = 0xa5;
	states[1].smm_revision = REV101;
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(test_arm(&evidence, NONCE(9),
		SMM_INVOCATION_LOADER_NON_S3_LOAD) ==
		CB_SUCCESS);
	assert(test_arrive(&evidence, 0, 3,
		&generations[0]) == CB_SUCCESS);
	assert(test_arrive(&evidence, 1, 7,
		&generations[1]) == CB_SUCCESS);
	assert(generations[0] == generations[1]);
	assert(test_ack(&evidence,
		generations[0], 0) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_init(&adapter, 2, tops,
		sizeof(states[0]), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(smm_invocation_evidence_claim(&evidence, 0xa5,
		0x11223344556677a5ULL, &ops, &token) == CB_ERR);
	assert(test_ack(&evidence,
		generations[1], 1) == CB_SUCCESS);
	claim = (struct claim_arg) { .evidence = &evidence, .ops = &ops };
	stale_fail = (struct rendezvous_call_arg) {
		.evidence = &evidence,
		.generation = generations[0],
	};
	evidence_hook_point = 11;
	evidence_hook_entered = 0;
	evidence_hook_release = 0;
	evidence_hook_point_secondary = 2;
	evidence_hook_entered_secondary = 0;
	evidence_hook_release_secondary = 0;
	assert(!pthread_create(&fail_thread, NULL, rendezvous_fail_thread,
		&stale_fail));
	while (!__atomic_load_n(&evidence_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&thread, NULL, claim_thread, &claim));
	while (!__atomic_load_n(&evidence_hook_entered_secondary,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_phase(&evidence) ==
		SMM_INVOCATION_CLAIMING);
	__atomic_store_n(&evidence_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(fail_thread, NULL));
	assert(stale_fail.result == CB_ERR);
	assert(!__atomic_load_n(&evidence.rendezvous_fail_requested,
		__ATOMIC_ACQUIRE));
	__atomic_store_n(&evidence_hook_release_secondary, 1U,
		__ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(claim.result == CB_SUCCESS);
	token = claim.token;
	evidence_hook_point = 0;
	evidence_hook_release = 1;
	evidence_hook_point_secondary = 0;
	evidence_hook_release_secondary = 1;
	assert(smm_invocation_evidence_abort(&evidence, &token, &ops) ==
		CB_SUCCESS);
	assert(test_depart(&evidence, 0, generations[0]) ==
		CB_SUCCESS);
	assert(test_depart(&evidence, 1, generations[1]) ==
		CB_SUCCESS);
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--ticket-focused")) {
		test_cause_validation();
		test_sparse_entry_nonce();
		return 0;
	}

	test_em64t101();
	test_em64t100();
	test_adapter_boundaries();
	test_cause_validation();
	test_sparse_entry_nonce();
	test_identity_and_aliases();
	test_policy_snapshot_mutation();
	test_ack_arm_race();
	test_ack_arm_published_marker_is_retry();
	test_departure_published_marker_is_retry();
	test_admission_completion_retries_competing_state_change();
	test_ack_arm_shutdown_failure_handoff();
	test_admission_completion_before_observer();
	test_retry_token_resnapshot_gaps();
	test_reservation_gap_failure_and_shutdown();
	test_transient_completion_before_observer();
	test_admission_token_binding();
	test_invalid_admission_state();
	test_admission_failure_completion_race();
	test_terminal_wins_stale_admission_failure();
	test_stale_failure_cannot_claim_new_attempt();
	test_opening_completion_before_failure();
	test_ack_failure_races();
	test_poison_admitting_handoff();
	test_transient_owner_shutdown_races();
	test_reset_paths();
	test_bounded_transient_resets();
	test_admission_token_replay_across_invocations();
	test_departure_timeout_suppresses_owner();
	test_departure_failure_final_arbitration();
	test_stale_generation_cannot_claim_repeated_phase();
	test_stale_shutdown_cannot_dirty_terminal();
	test_packed_latches_and_terminal_shutdown();
	test_provision_rejects_dirty_empty_state();
	test_cleanup_claim_retries_packed_latch();
	test_phase_claim_retries_packed_latch();
	test_stale_reentry_cannot_dirty_next_state();
	test_rendezvous_and_departure();
	test_claim_requires_all_acknowledgements();
	return 0;
}
