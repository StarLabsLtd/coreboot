/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cpu/x86/smm_invocation_evidence.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define TEST_CPUS 4U
#define TEST_COMMAND 0x5aU
#define TEST_SENTINEL 0x112233445566775aULL
#define NONCE(value) ((struct smm_invocation_loader_instance_nonce) { \
	.low = (value), .high = (value) ^ 0xa55aa55aa55aa55aULL })

static uint32_t test_hook_point;
static uint32_t test_hook_entered;
static uint32_t test_hook_release;
static uint32_t held_hook_point;
static uint32_t held_hook_entered;
static uint32_t held_hook_release;
static uint8_t fail_stop_exit_code;
static uint32_t fail_stop_thread_exit;
static uint32_t fail_stop_calls;

void smm_invocation_evidence_test_hook(uint32_t point)
{
	uint32_t expected = point;
	uint32_t held = point;

	if (__atomic_compare_exchange_n(&held_hook_point, &held, 0U, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&held_hook_entered, 1U, __ATOMIC_RELEASE);
		while (!__atomic_load_n(&held_hook_release, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		return;
	}

	if (!__atomic_compare_exchange_n(&test_hook_point, &expected, 0U, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return;
	__atomic_store_n(&test_hook_entered, 1U, __ATOMIC_RELEASE);
	while (!__atomic_load_n(&test_hook_release, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
}

static void test_hook_arm(uint32_t point)
{
	__atomic_store_n(&test_hook_entered, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&test_hook_release, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&test_hook_point, point, __ATOMIC_RELEASE);
}

static void test_hook_wait_and_release(void)
{
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
}

static enum cb_err test_arrive(struct smm_invocation_evidence *evidence,
	uint32_t cpu, uint32_t apic_id, uint64_t *generation)
{
	struct smm_invocation_admission_token token;

	return smm_invocation_evidence_arrive_try(evidence, cpu, apic_id,
		generation, &token) == SMM_INVOCATION_TRY_SUCCESS ?
		CB_SUCCESS : CB_ERR;
}

static enum cb_err test_depart(struct smm_invocation_evidence *evidence,
	uint32_t cpu, uint64_t generation)
{
	return smm_invocation_evidence_depart_try(evidence, cpu, generation) ==
		SMM_INVOCATION_TRY_SUCCESS ? CB_SUCCESS : CB_ERR;
}

struct mock_state {
	uint64_t rax[TEST_CPUS];
	uint32_t matched;
	uint32_t second_match;
	uint32_t fail_read;
	uint32_t fail_write;
	uint32_t mutate_ops;
	uint32_t mutate_ops_on_write;
	uint32_t shutdown_on_write;
	uint32_t strong_reenter;
	uint32_t strong_reenter_on_write;
	uint32_t read_calls;
	uint32_t wrong_write;
	uint32_t mutate_geometry_on_write;
	uint32_t reject_read_after_geometry_mutation;
	uint32_t mutate_ack_on_write;
	uint32_t reenter;
	uint32_t shutdown_reenter;
	uint32_t invalid_match;
	uint32_t callback_count;
	uint32_t block_match;
	uint32_t match_entered;
	uint32_t match_release;
	uint32_t block_read;
	uint32_t read_entered;
	uint32_t read_release;
	struct smm_invocation_evidence *evidence;
	struct smm_invocation_save_state_ops *ops;
};

struct fixture {
	struct smm_invocation_evidence evidence;
	struct smm_invocation_loader_seed seed;
	struct smm_invocation_save_state_ops ops;
	struct mock_state mock;
};

struct thread_arg {
	struct fixture *fixture;
	uint32_t cpu;
	uint64_t generation;
	enum cb_err result;
};

struct shutdown_arg {
	struct smm_invocation_evidence *evidence;
	enum cb_err result;
};

struct operation_arg {
	struct fixture *fixture;
	struct smm_invocation_token *token;
	uint64_t value;
	enum cb_err result;
};

struct admission_arg {
	struct fixture *fixture;
	struct smm_invocation_admission_token token;
	uint64_t generation;
	uint32_t cpu;
	enum smm_invocation_try_result result;
};

struct observer_arg {
	struct smm_invocation_evidence *evidence;
	uint32_t terminal_phase;
};

static bool bytes_nonzero(const void *address, size_t size)
{
	const uint8_t *byte = address;
	uint8_t value = 0;

	while (size--)
		value |= *byte++;
	return value != 0;
}

static enum smm_invocation_match match_apmc(void *context, uint32_t cpu,
	uint8_t command)
{
	struct mock_state *mock = context;

	mock->callback_count++;
	if (__atomic_load_n(&mock->block_match, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&mock->match_entered, 1U, __ATOMIC_RELEASE);
		while (!__atomic_load_n(&mock->match_release, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
	}
	if (mock->mutate_ops)
		mock->ops->context = NULL;
	if (mock->reenter) {
		struct smm_invocation_token token;

		mock->reenter = 0;
		assert(smm_invocation_evidence_claim(mock->evidence, command,
			TEST_SENTINEL, mock->ops, &token) == CB_ERR);
	}
	if (mock->shutdown_reenter) {
		mock->shutdown_reenter = 0;
		assert(smm_invocation_evidence_shutdown(mock->evidence) == CB_ERR);
	}
	if (mock->invalid_match)
		return (enum smm_invocation_match)3;
	if (command != TEST_COMMAND)
		return SMM_INVOCATION_MATCH_ERROR;
	if (cpu == mock->matched || cpu == mock->second_match)
		return SMM_INVOCATION_MATCHED;
	return SMM_INVOCATION_NOT_MATCHED;
}

static enum cb_err read_rax(void *context, uint32_t cpu, uint64_t *value)
{
	struct mock_state *mock = context;

	if (cpu >= TEST_CPUS)
		return CB_ERR;
	if (mock->reject_read_after_geometry_mutation &&
	    mock->mutate_geometry_on_write == 2U)
		abort();
	mock->read_calls++;
	if (mock->strong_reenter == mock->read_calls) {
		mock->strong_reenter = 0;
		(void)smm_invocation_evidence_publish_and_request_close(
			mock->evidence, NULL, 1, NULL);
	}
	if (__atomic_load_n(&mock->block_read, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&mock->read_entered, 1U, __ATOMIC_RELEASE);
		while (!__atomic_load_n(&mock->read_release, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
	}
	if (mock->fail_read) {
		mock->fail_read--;
		if (!mock->fail_read)
			return CB_ERR;
	}
	*value = mock->rax[cpu];
	return CB_SUCCESS;
}

static enum cb_err write_rax(void *context, uint32_t cpu, uint64_t value)
{
	struct mock_state *mock = context;

	if (cpu >= TEST_CPUS)
		return CB_ERR;
	mock->rax[cpu] = value;
	if (mock->strong_reenter_on_write) {
		mock->strong_reenter_on_write = 0;
		(void)smm_invocation_evidence_publish_and_request_close(
			mock->evidence, NULL, 1, NULL);
	}
	if (mock->wrong_write)
		mock->rax[cpu] ^= 1U;
	if (mock->mutate_geometry_on_write) {
		mock->evidence->generation++;
		mock->mutate_geometry_on_write = 2U;
	}
	if (mock->mutate_ack_on_write) {
		__atomic_store_n(&mock->evidence->rendezvous_ack_required, 1U,
			__ATOMIC_RELEASE);
		__atomic_store_n(&mock->evidence->rendezvous_ack_cpus,
			mock->evidence->expected_cpus, __ATOMIC_RELEASE);
	}
	if (mock->mutate_ops_on_write) {
		mock->mutate_ops_on_write = 0;
		mock->ops->context = NULL;
	}
	if (mock->shutdown_on_write) {
		mock->shutdown_on_write = 0;
		assert(smm_invocation_evidence_shutdown(mock->evidence) == CB_ERR);
	}
	if (mock->fail_write) {
		mock->fail_write--;
		return CB_ERR;
	}
	return CB_SUCCESS;
}

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

static void fixture_init(struct fixture *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->seed = (struct smm_invocation_loader_seed) {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(fixture->seed),
		.active_cpus = TEST_CPUS,
		.bsp_cpu = 0,
		.loader_instance_nonce = NONCE(7),
		.participant_apic_ids = { 10, 11, 12, 13 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	fixture->mock = (struct mock_state) {
		.matched = 0,
		.second_match = UINT32_MAX,
		.evidence = &fixture->evidence,
		.ops = &fixture->ops,
	};
	fixture->mock.rax[0] = TEST_COMMAND;
	fixture->ops = (struct smm_invocation_save_state_ops) {
		.match_apmc_write = match_apmc,
		.read_rax = read_rax,
		.write_rax = write_rax,
		.context = &fixture->mock,
		.context_size = sizeof(fixture->mock),
	};
	assert(smm_invocation_evidence_provision(&fixture->evidence,
		&fixture->seed) == CB_SUCCESS);
}

static void *arrive_thread(void *opaque)
{
	struct thread_arg *arg = opaque;

	arg->result = test_arrive(&arg->fixture->evidence,
		arg->cpu, arg->fixture->seed.participant_apic_ids[arg->cpu],
		&arg->generation);
	return NULL;
}

static void *depart_thread(void *opaque)
{
	struct thread_arg *arg = opaque;

	arg->result = test_depart(&arg->fixture->evidence,
		arg->cpu, arg->generation);
	return NULL;
}

static void *rendezvous_fail_thread(void *opaque)
{
	struct thread_arg *arg = opaque;

	arg->result = smm_invocation_evidence_rendezvous_fail(
		&arg->fixture->evidence, arg->generation);
	return NULL;
}

static void *arm_ack_thread(void *opaque)
{
	struct admission_arg *arg = opaque;

	arg->result = smm_invocation_evidence_require_rendezvous_ack_try(
		&arg->fixture->evidence, arg->fixture->seed.loader_instance_nonce,
		arg->fixture->seed.lifecycle, &arg->token);
	return NULL;
}

static void *rendezvous_ack_thread(void *opaque)
{
	struct admission_arg *arg = opaque;

	arg->result = smm_invocation_evidence_rendezvous_ack_try(
		&arg->fixture->evidence, arg->generation, arg->cpu, &arg->token);
	return NULL;
}

static void *admission_fail_thread(void *opaque)
{
	struct admission_arg *arg = opaque;

	arg->result = smm_invocation_evidence_admission_fail(
		&arg->fixture->evidence, &arg->token) == CB_SUCCESS ?
		SMM_INVOCATION_TRY_SUCCESS : SMM_INVOCATION_TRY_ERROR;
	return NULL;
}

static void *shutdown_thread(void *opaque)
{
	struct shutdown_arg *arg = opaque;

	arg->result = smm_invocation_evidence_shutdown(arg->evidence);
	return NULL;
}

static void *claim_thread(void *opaque)
{
	struct operation_arg *arg = opaque;

	arg->result = smm_invocation_evidence_claim(&arg->fixture->evidence,
		TEST_COMMAND, TEST_SENTINEL, &arg->fixture->ops, arg->token);
	return NULL;
}

static void *publish_thread(void *opaque)
{
	struct operation_arg *arg = opaque;

	arg->result = smm_invocation_evidence_publish(&arg->fixture->evidence,
		arg->token, 0x88776655ULL, &arg->fixture->ops);
	return NULL;
}

static void *strong_completion_thread(void *opaque)
{
	struct operation_arg *arg = opaque;

	arg->result = smm_invocation_evidence_publish_and_request_close(
		&arg->fixture->evidence, arg->token, arg->value,
		&arg->fixture->ops);
	return NULL;
}

static void *terminal_observer_thread(void *opaque)
{
	struct observer_arg *arg = opaque;

	while (smm_invocation_evidence_phase(arg->evidence) !=
		arg->terminal_phase)
		__asm__ volatile ("pause");
	assert(!bytes_nonzero(&arg->evidence->token,
		sizeof(arg->evidence->token)));
	assert(!arg->evidence->sentinel && !arg->evidence->original_rax);
	assert(!bytes_nonzero(arg->evidence->participants,
		sizeof(arg->evidence->participants)));
	return NULL;
}

static uint64_t arrive_all(struct fixture *fixture)
{
	uint64_t generation = 0;
	uint64_t first_generation = 0;

	for (uint32_t cpu = 0; cpu < TEST_CPUS; cpu++) {
		assert(test_arrive(&fixture->evidence, cpu,
			fixture->seed.participant_apic_ids[cpu], &generation) ==
			CB_SUCCESS);
		if (!cpu)
			first_generation = generation;
		assert(generation == first_generation);
	}
	return first_generation;
}

static void depart_all(struct fixture *fixture, uint64_t generation)
{
	for (uint32_t cpu = 0; cpu < TEST_CPUS; cpu++)
		assert(test_depart(&fixture->evidence, cpu,
			generation) == CB_SUCCESS);
}

static uint64_t prepare_final_rendezvous_ack(struct fixture *fixture)
{
	struct smm_invocation_admission_token admission;
	uint64_t generation;

	assert(smm_invocation_evidence_require_rendezvous_ack_try(
		&fixture->evidence, fixture->seed.loader_instance_nonce,
		fixture->seed.lifecycle, &admission) ==
		SMM_INVOCATION_TRY_SUCCESS);
	generation = arrive_all(fixture);
	for (uint32_t cpu = 0; cpu < TEST_CPUS - 1U; cpu++)
		assert(smm_invocation_evidence_rendezvous_ack_try(
			&fixture->evidence, generation, cpu, &admission) ==
			SMM_INVOCATION_TRY_SUCCESS);
	return generation;
}

static void exercise_sparse_nonce(
	struct smm_invocation_loader_instance_nonce nonce)
{
	struct fixture fixture;
	struct smm_invocation_admission_token admission;
	struct smm_invocation_token token;
	uint64_t generation = 0;
	uint64_t first_generation = 0;

	fixture_init(&fixture);
	assert(smm_invocation_evidence_shutdown(&fixture.evidence) == CB_SUCCESS);
	memset(&fixture.evidence, 0, sizeof(fixture.evidence));
	fixture.seed.loader_instance_nonce = nonce;
	assert(smm_invocation_evidence_provision(&fixture.evidence,
		&fixture.seed) == CB_SUCCESS);
	assert(smm_invocation_evidence_require_rendezvous_ack_try(
		&fixture.evidence, nonce, fixture.seed.lifecycle, &admission) ==
		SMM_INVOCATION_TRY_SUCCESS);
	for (uint32_t cpu = 0; cpu < TEST_CPUS; cpu++) {
		assert(smm_invocation_evidence_arrive_try(&fixture.evidence, cpu,
			fixture.seed.participant_apic_ids[cpu], &generation,
			&admission) == SMM_INVOCATION_TRY_SUCCESS);
		if (!cpu)
			first_generation = generation;
		assert(generation == first_generation);
	}
	for (uint32_t cpu = 0; cpu < TEST_CPUS; cpu++)
		assert(smm_invocation_evidence_rendezvous_ack_try(
			&fixture.evidence, generation, cpu, &admission) ==
			SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) == CB_SUCCESS);
	depart_all(&fixture, generation);
	assert(smm_invocation_evidence_eos_consume(&fixture.evidence,
		generation, nonce, fixture.seed.lifecycle, fixture.seed.bsp_cpu));
	assert(!smm_invocation_evidence_eos_consume(&fixture.evidence,
		generation, nonce, fixture.seed.lifecycle, fixture.seed.bsp_cpu));
}

static void test_sparse_nonce_lifecycle(void)
{
	exercise_sparse_nonce((struct smm_invocation_loader_instance_nonce) {
		.low = 1,
	});
	exercise_sparse_nonce((struct smm_invocation_loader_instance_nonce) {
		.high = 1,
	});
}

static void test_happy_path(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;
	struct observer_arg observer;
	pthread_t observer_thread;
	uint64_t generation;

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	assert(token.initiator_cpu == 0 && token.active_cpus == TEST_CPUS);
	assert(token.smi_generation == generation && token.bsp == 1);
	assert(fixture.mock.rax[0] == TEST_SENTINEL);
	assert(smm_invocation_evidence_publish(&fixture.evidence, &token,
		0x12345678ULL, &fixture.ops) == CB_SUCCESS);
	assert(fixture.mock.rax[0] == 0x12345678ULL);
	assert(smm_invocation_evidence_complete(&fixture.evidence, &token) ==
		CB_SUCCESS);
	observer = (struct observer_arg) {
		.evidence = &fixture.evidence,
		.terminal_phase = SMM_INVOCATION_READY,
	};
	assert(!pthread_create(&observer_thread, NULL, terminal_observer_thread,
		&observer));
	assert(test_depart(&fixture.evidence, 0,
		generation + 1U) == CB_ERR);
	depart_all(&fixture, generation);
	assert(!pthread_join(observer_thread, NULL));
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_READY);
	assert(!bytes_nonzero(&fixture.evidence.token,
		sizeof(fixture.evidence.token)));
	assert(smm_invocation_evidence_shutdown(&fixture.evidence) == CB_SUCCESS);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_CLOSED);
}

static void test_missing_and_wrong_participant(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;
	uint64_t generation;

	fixture_init(&fixture);
	assert(test_arrive(&fixture.evidence, 0, 99,
		&generation) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);
	fixture_init(&fixture);
	for (uint32_t cpu = 0; cpu < TEST_CPUS - 1; cpu++)
		assert(test_arrive(&fixture.evidence, cpu,
			10U + cpu, &generation) == CB_SUCCESS);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(test_arrive(&fixture.evidence, 3, 13,
		&generation) == CB_SUCCESS);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) ==
		CB_SUCCESS);
	depart_all(&fixture, generation);
}

static void test_exact_one_and_bsp(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	fixture.mock.second_match = 1;
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	assert(smm_invocation_evidence_shutdown(&fixture.evidence) == CB_SUCCESS);
	memset(&fixture.evidence, 0, sizeof(fixture.evidence));
	fixture.seed.bsp_cpu = TEST_CPUS - 1U;
	assert(smm_invocation_evidence_provision(&fixture.evidence,
		&fixture.seed) == CB_SUCCESS);
	(void)arrive_all(&fixture);
	fixture.mock.matched = 0;
	fixture.mock.second_match = TEST_CPUS - 1U;
	fixture.mock.rax[TEST_CPUS - 1U] = TEST_COMMAND;
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	fixture.mock.matched = 1;
	fixture.mock.rax[1] = TEST_COMMAND;
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);
}

static void test_save_state_failures_and_mutation(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	fixture.mock.fail_write = 1;
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);
	assert(fixture.mock.rax[0] == TEST_COMMAND);

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	fixture.mock.fail_read = 2;
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(fixture.mock.rax[0] == TEST_COMMAND);

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	fixture.mock.mutate_ops = 1;
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);
}

static void test_publish_guard_and_active_shutdown(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;
	struct shutdown_arg shutdown;
	pthread_t thread;
	uint64_t generation;

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	fixture.mock.rax[0] ^= 1U;
	assert(smm_invocation_evidence_publish(&fixture.evidence, &token,
		0x77ULL, &fixture.ops) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	fixture.mock.shutdown_reenter = 1;
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	shutdown = (struct shutdown_arg) { .evidence = &fixture.evidence };
	assert(!pthread_create(&thread, NULL, shutdown_thread, &shutdown));
	while (smm_invocation_evidence_phase(&fixture.evidence) !=
		SMM_INVOCATION_CLOSING)
		__asm__ volatile ("pause");
	depart_all(&fixture, generation);
	assert(!pthread_join(thread, NULL));
	assert(shutdown.result == CB_SUCCESS);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_CLOSED);
}

static void assert_strong_completion_rejection_is_inert(
	struct fixture *fixture, const struct smm_invocation_token *token,
	const struct smm_invocation_save_state_ops *ops)
{
	const struct fixture before = *fixture;
	struct smm_invocation_save_state_ops ops_before;
	struct smm_invocation_token token_before;

	if (token)
		memcpy(&token_before, token, sizeof(token_before));
	if (ops)
		memcpy(&ops_before, ops, sizeof(ops_before));

	assert(smm_invocation_evidence_publish_and_request_close(
		&fixture->evidence, token, 0x1234ULL, ops) == CB_ERR);
	assert(!memcmp(fixture, &before, sizeof(*fixture)));
	if (token)
		assert(!memcmp(token, &token_before, sizeof(token_before)));
	if (ops)
		assert(!memcmp(ops, &ops_before, sizeof(ops_before)));
}

static void test_strong_completion_rejection_is_inert(void)
{
	struct smm_invocation_save_state_ops invalid_ops;
	struct smm_invocation_save_state_ops overlap_ops;
	struct smm_invocation_token stale;
	struct smm_invocation_token token;
	struct fixture fixture;
	union {
		struct smm_invocation_token token;
		struct smm_invocation_save_state_ops ops;
	} overlap;
	uint64_t generation;

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	stale = token;
	stale.revision++;

	assert_strong_completion_rejection_is_inert(&fixture, &stale,
		&fixture.ops);
	for (uint32_t mode = 0; mode < 8U; mode++) {
		stale = token;
		switch (mode) {
		case 0:
			stale.size++;
			break;
		case 1:
			stale.active_cpus = 0;
			break;
		case 2:
			stale.active_cpus =
				SMM_INVOCATION_EVIDENCE_MAX_CPUS + 1U;
			break;
		case 3:
			stale.initiator_cpu = stale.active_cpus;
			break;
		case 4:
			stale.smi_generation = 0;
			break;
		case 5:
			stale.rendezvous_generation++;
			break;
		case 6:
			stale.bsp = 0;
			break;
		default:
			stale.reserved = 1;
			break;
		}
		assert_strong_completion_rejection_is_inert(&fixture, &stale,
			&fixture.ops);
	}
	for (uint32_t mode = 0; mode < 4U; mode++) {
		invalid_ops = fixture.ops;
		if (mode == 0)
			invalid_ops.match_apmc_write = NULL;
		else if (mode == 1)
			invalid_ops.read_rax = NULL;
		else if (mode == 2)
			invalid_ops.write_rax = NULL;
		else {
			invalid_ops.context = (void *)UINTPTR_MAX;
			invalid_ops.context_size = 2;
		}
		assert_strong_completion_rejection_is_inert(&fixture, &token,
			&invalid_ops);
	}
	assert_strong_completion_rejection_is_inert(&fixture, NULL,
		&fixture.ops);
	assert_strong_completion_rejection_is_inert(&fixture, &token, NULL);
	assert_strong_completion_rejection_is_inert(&fixture,
		(const struct smm_invocation_token *)&fixture.evidence.token,
		&fixture.ops);
	assert_strong_completion_rejection_is_inert(&fixture, &token,
		(const struct smm_invocation_save_state_ops *)
			&fixture.evidence.token);
	memcpy(&overlap.token, &token, sizeof(token));
	assert_strong_completion_rejection_is_inert(&fixture, &overlap.token,
		(const struct smm_invocation_save_state_ops *)&overlap.token);
	overlap_ops = fixture.ops;
	overlap_ops.context = &fixture.evidence;
	overlap_ops.context_size = 1;
	assert_strong_completion_rejection_is_inert(&fixture, &token,
		&overlap_ops);
	overlap_ops.context = &token;
	assert_strong_completion_rejection_is_inert(&fixture, &token,
		&overlap_ops);
	overlap_ops.context = &overlap_ops;
	assert_strong_completion_rejection_is_inert(&fixture, &token,
		&overlap_ops);
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) == CB_SUCCESS);
	depart_all(&fixture, generation);
}

static void test_strong_completion_success(void)
{
	struct fixture before;
	struct fixture fixture;
	struct smm_invocation_token token;
	uint64_t generation;
	uint32_t claimed_control;

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	claimed_control = __atomic_load_n(&fixture.evidence.state,
		__ATOMIC_ACQUIRE);
	assert(smm_invocation_evidence_publish_and_request_close(&fixture.evidence, &token,
		0x1020304050607080ULL, &fixture.ops) == CB_SUCCESS);
	assert(fixture.mock.rax[0] == 0x1020304050607080ULL);
	assert(smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_CLOSING);
	assert(__atomic_load_n(&fixture.evidence.state, __ATOMIC_ACQUIRE) ==
		((claimed_control & ~0x1fU) | (1U << 11) |
		 SMM_INVOCATION_CLOSING));
	before = fixture;
	assert(smm_invocation_evidence_publish_and_request_close(&fixture.evidence,
		&token, 0x1020304050607080ULL, &fixture.ops) == CB_ERR);
	assert(!memcmp(&fixture, &before, sizeof(fixture)));
	depart_all(&fixture, generation);
	assert(smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_READY);
	assert(smm_invocation_evidence_eos_consume(&fixture.evidence, generation,
		fixture.seed.loader_instance_nonce, fixture.seed.lifecycle,
		fixture.seed.bsp_cpu));
	assert(!smm_invocation_evidence_eos_consume(&fixture.evidence, generation,
		fixture.seed.loader_instance_nonce, fixture.seed.lifecycle,
		fixture.seed.bsp_cpu));
}

static void test_strong_completion_zero_result(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;
	uint64_t generation;

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_publish_and_request_close(&fixture.evidence,
		&token, 0, &fixture.ops) == CB_SUCCESS);
	assert(!fixture.mock.rax[0]);
	depart_all(&fixture, generation);
}

static void test_strong_completion_external_snapshot_is_immutable(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;
	struct operation_arg operation;
	pthread_t thread;
	uint64_t generation;

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	operation = (struct operation_arg) {
		.fixture = &fixture,
		.token = &token,
		.value = 0x445566ULL,
	};
	test_hook_arm(49);
	assert(!pthread_create(&thread, NULL, strong_completion_thread, &operation));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	token.smi_generation++;
	fixture.ops.context = NULL;
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(operation.result == CB_SUCCESS);
	depart_all(&fixture, generation);
}

static void strong_completion_fail_stop_case(uint32_t hook,
	uint32_t mutation)
{
	const pid_t child = fork();
	int status;

	assert(child >= 0);
	if (!child) {
		struct smm_invocation_token token;
		struct operation_arg operation;
		struct fixture fixture;
		pthread_t thread;

		fixture_init(&fixture);
		(void)arrive_all(&fixture);
		assert(smm_invocation_evidence_claim(&fixture.evidence,
			TEST_COMMAND, TEST_SENTINEL, &fixture.ops, &token) ==
			CB_SUCCESS);
		operation = (struct operation_arg) {
			.fixture = &fixture,
			.token = &token,
			.value = 0xa1b2c3d4ULL,
		};
		fail_stop_exit_code = 97;
		test_hook_arm(hook);
		assert(!pthread_create(&thread, NULL, strong_completion_thread,
			&operation));
		while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		if (mutation == 1U)
			fixture.evidence.token.smi_generation++;
		else if (mutation == 2U)
			fixture.evidence.generation++;
		else if (mutation == 3U)
			__atomic_fetch_or(&fixture.evidence.state, 1U << 9,
				__ATOMIC_ACQ_REL);
		else if (mutation == 4U)
			fixture.evidence.generation++;
		else
			__atomic_store_n(&fixture.evidence.state,
				SMM_INVOCATION_CLAIMED, __ATOMIC_RELEASE);
		__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
		(void)pthread_join(thread, NULL);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 97);
}

static void test_strong_completion_post_ownership_fail_stop(void)
{
	strong_completion_fail_stop_case(45, 1);
	strong_completion_fail_stop_case(46, 2);
	strong_completion_fail_stop_case(47, 3);
	strong_completion_fail_stop_case(47, 2);
	strong_completion_fail_stop_case(48, 4);
	strong_completion_fail_stop_case(53, 5);
}

static void test_strong_completion_semantic_rejection_fail_stop(void)
{
	for (uint32_t mode = 0; mode < 2U; mode++) {
		const pid_t child = fork();
		int status;

		assert(child >= 0);
		if (!child) {
			struct fixture fixture;
			struct smm_invocation_token token;

			fixture_init(&fixture);
			(void)arrive_all(&fixture);
			assert(smm_invocation_evidence_claim(&fixture.evidence,
				TEST_COMMAND, TEST_SENTINEL, &fixture.ops, &token) ==
				CB_SUCCESS);
			fixture.mock.read_calls = 0;
			if (!mode)
				token.smi_generation = ++token.rendezvous_generation;
			fail_stop_exit_code = 98;
			(void)smm_invocation_evidence_publish_and_request_close(
				&fixture.evidence, &token,
				mode ? TEST_SENTINEL : 0x7788ULL, &fixture.ops);
			_exit(0);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 98);
	}
}

static void test_strong_completion_callback_fail_stop(void)
{
	for (uint32_t mode = 0; mode < 11U; mode++) {
		const pid_t child = fork();
		int status;

		assert(child >= 0);
		if (!child) {
			struct fixture fixture;
			struct smm_invocation_token token;

			fixture_init(&fixture);
			(void)arrive_all(&fixture);
			assert(smm_invocation_evidence_claim(&fixture.evidence,
				TEST_COMMAND, TEST_SENTINEL, &fixture.ops, &token) ==
				CB_SUCCESS);
			fixture.mock.read_calls = 0;
			if (mode == 0U)
				fixture.mock.fail_read = 1;
			else if (mode == 1U)
				fixture.mock.rax[0] ^= 1U;
			else if (mode == 2U)
				fixture.mock.fail_write = 1;
			else if (mode == 3U)
				fixture.mock.fail_read = 2;
			else if (mode == 4U)
				fixture.mock.wrong_write = 1;
			else if (mode == 5U)
				fixture.mock.shutdown_on_write = 1;
			else if (mode == 6U)
				fixture.mock.strong_reenter = 1;
			else if (mode == 7U)
				fixture.mock.strong_reenter = 2;
			else if (mode == 8U)
				fixture.mock.strong_reenter_on_write = 1;
			else if (mode == 9U)
				fixture.mock.mutate_geometry_on_write =
					fixture.mock.reject_read_after_geometry_mutation = 1;
			else
				fixture.mock.mutate_ack_on_write = 1;
			fail_stop_exit_code = 99;
			(void)smm_invocation_evidence_publish_and_request_close(
				&fixture.evidence, &token, 0x8899ULL, &fixture.ops);
			_exit(0);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 99);
	}
}

static void test_strong_completion_claim_and_abort_reentry(void)
{
	for (uint32_t mode = 0; mode < 2U; mode++) {
		const pid_t child = fork();
		int status;

		assert(child >= 0);
		if (!child) {
			struct fixture fixture;
			struct smm_invocation_token token;

			fixture_init(&fixture);
			(void)arrive_all(&fixture);
			fail_stop_exit_code = 101;
			fixture.mock.strong_reenter = 1;
			if (!mode)
				(void)smm_invocation_evidence_claim(&fixture.evidence,
					TEST_COMMAND, TEST_SENTINEL, &fixture.ops,
					&token);
			else {
				fixture.mock.strong_reenter = 0;
				assert(smm_invocation_evidence_claim(&fixture.evidence,
					TEST_COMMAND, TEST_SENTINEL, &fixture.ops,
					&token) == CB_SUCCESS);
				fixture.mock.strong_reenter_on_write = 1;
				(void)smm_invocation_evidence_abort(
					&fixture.evidence, &token, &fixture.ops);
			}
			_exit(0);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 101);
	}
}

static void test_strong_completion_acquisition_cas_loss(void)
{
	const pid_t child = fork();
	int status;

	assert(child >= 0);
	if (!child) {
		struct fixture fixture;
		struct smm_invocation_token token;
		struct operation_arg operation;
		pthread_t thread;

		fixture_init(&fixture);
		(void)arrive_all(&fixture);
		assert(smm_invocation_evidence_claim(&fixture.evidence,
			TEST_COMMAND, TEST_SENTINEL, &fixture.ops, &token) ==
			CB_SUCCESS);
		operation = (struct operation_arg) {
			.fixture = &fixture,
			.token = &token,
			.value = 0x5566ULL,
		};
		fail_stop_exit_code = 100;
		test_hook_arm(49);
		assert(!pthread_create(&thread, NULL, strong_completion_thread,
			&operation));
		while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		__atomic_fetch_or(&fixture.evidence.state, 1U << 9,
			__ATOMIC_ACQ_REL);
		__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
		(void)pthread_join(thread, NULL);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 100);
}

static void test_strong_completion_second_phase_sample(void)
{
	const uint32_t phases[] = {
		SMM_INVOCATION_CLAIMING,
		SMM_INVOCATION_PUBLISHING,
		SMM_INVOCATION_ABORTING,
	};

	for (size_t index = 0; index < ARRAY_SIZE(phases);
	     index++) {
		const pid_t child = fork();
		int status;

		assert(child >= 0);
		if (!child) {
			struct fixture fixture;
			struct smm_invocation_token token;
			struct operation_arg operation;
			pthread_t thread;
			uint32_t state;

			fixture_init(&fixture);
			(void)arrive_all(&fixture);
			assert(smm_invocation_evidence_claim(&fixture.evidence,
				TEST_COMMAND, TEST_SENTINEL, &fixture.ops, &token) ==
				CB_SUCCESS);
			operation = (struct operation_arg) {
				.fixture = &fixture,
				.token = &token,
				.value = 0x6677ULL,
			};
			fail_stop_exit_code = 102;
			test_hook_arm(51);
			assert(!pthread_create(&thread, NULL,
				strong_completion_thread, &operation));
			while (!__atomic_load_n(&test_hook_entered,
				__ATOMIC_ACQUIRE))
				__asm__ volatile ("pause");
			state = __atomic_load_n(&fixture.evidence.state,
				__ATOMIC_ACQUIRE);
			__atomic_store_n(&fixture.evidence.state,
				(state & ~0x1fU) | phases[index], __ATOMIC_RELEASE);
			__atomic_store_n(&test_hook_release, 1U,
				__ATOMIC_RELEASE);
			(void)pthread_join(thread, NULL);
			_exit(0);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 102);
	}
	{
		struct fixture fixture;
		struct smm_invocation_token token;
		struct operation_arg operation;
		pthread_t thread;
		uint32_t state;

		fixture_init(&fixture);
		(void)arrive_all(&fixture);
		assert(smm_invocation_evidence_claim(&fixture.evidence,
			TEST_COMMAND, TEST_SENTINEL, &fixture.ops, &token) ==
			CB_SUCCESS);
		operation = (struct operation_arg) {
			.fixture = &fixture,
			.token = &token,
			.value = 0x6677ULL,
		};
		test_hook_arm(51);
		assert(!pthread_create(&thread, NULL, strong_completion_thread,
			&operation));
		while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		state = __atomic_load_n(&fixture.evidence.state, __ATOMIC_ACQUIRE);
		__atomic_store_n(&fixture.evidence.state,
			(state & ~0x1fU) | SMM_INVOCATION_CLOSING,
			__ATOMIC_RELEASE);
		__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
		assert(!pthread_join(thread, NULL));
		assert(operation.result == CB_ERR);
		assert(!__atomic_load_n(&fail_stop_calls, __ATOMIC_ACQUIRE));
	}
}

static void test_strong_completion_claimed_control_corruption(void)
{
	for (uint32_t mode = 0; mode < 6U; mode++) {
		const pid_t child = fork();
		int status;

		assert(child >= 0);
		if (!child) {
			struct fixture fixture;
			struct smm_invocation_token token;
			uint32_t state;

			fixture_init(&fixture);
			(void)arrive_all(&fixture);
			assert(smm_invocation_evidence_claim(&fixture.evidence,
				TEST_COMMAND, TEST_SENTINEL, &fixture.ops, &token) ==
				CB_SUCCESS);
			state = __atomic_load_n(&fixture.evidence.state,
				__ATOMIC_ACQUIRE);
			if (mode == 0U)
				state |= 1U << 7;
			else if (mode == 1U)
				state |= 1U << 8;
			else if (mode == 2U)
				state |= 1U << 9;
			else if (mode == 3U)
				state &= (1U << 12) - 1U;
			else if (mode == 4U)
				state = (state & ~(3U << 5)) | (1U << 5);
			else
				__atomic_store_n(
					&fixture.evidence.rendezvous_ack_required,
					2U, __ATOMIC_RELEASE);
			if (mode != 5U)
				__atomic_store_n(&fixture.evidence.state, state,
					__ATOMIC_RELEASE);
			fail_stop_exit_code = 103;
			(void)smm_invocation_evidence_publish_and_request_close(
				&fixture.evidence, &token, 0x7788ULL, &fixture.ops);
			_exit(0);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 103);
	}
}

static void test_reentry_stale_and_collision(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;
	struct smm_invocation_token stale;
	uint64_t generation;

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL ^ 1U, &fixture.ops, &token) == CB_ERR);
	fixture.mock.reenter = 1;
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(fixture.mock.callback_count == 1U);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	stale = token;
	assert(smm_invocation_evidence_publish(&fixture.evidence, &token,
		TEST_SENTINEL, &fixture.ops) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_CLOSING);
	depart_all(&fixture, generation);

	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_publish(&fixture.evidence, &stale,
		0x22ULL, &fixture.ops) == CB_ERR);
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) ==
		CB_SUCCESS);
	depart_all(&fixture, generation);
}

static void test_invalid_match_and_aliases(void)
{
	union {
		uint64_t alignment;
		uint8_t bytes[sizeof(struct smm_invocation_evidence) +
			sizeof(struct smm_invocation_loader_seed)];
	} overlap;
	struct fixture fixture;
	struct smm_invocation_token token;
	uint64_t generation;

	memset(&overlap, 0, sizeof(overlap));
	assert(smm_invocation_evidence_provision(
		(struct smm_invocation_evidence *)overlap.bytes,
		(struct smm_invocation_loader_seed *)(overlap.bytes +
			sizeof(struct smm_invocation_evidence) - 1U)) == CB_ERR);

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	fixture.mock.invalid_match = 1;
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(fixture.mock.callback_count == 1U);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	assert(test_arrive(&fixture.evidence, 0, 10,
		&fixture.evidence.loader_instance_nonce.low) == CB_ERR);

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops,
		(struct smm_invocation_token *)&fixture.evidence.token) == CB_ERR);

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	fixture.ops.context = &fixture.evidence;
	fixture.ops.context_size = sizeof(fixture.evidence);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_complete(&fixture.evidence,
		(struct smm_invocation_token *)&fixture.evidence.token) == CB_ERR);
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) == CB_SUCCESS);
	depart_all(&fixture, generation);
}

static void test_token_binds_loader_and_topology(void)
{
	struct fixture first;
	struct fixture second;
	struct fixture third;
	struct fixture fourth;
	struct smm_invocation_token first_token;
	struct smm_invocation_token second_token;
	struct smm_invocation_token third_token;
	struct smm_invocation_token fourth_token;
	uint64_t generation;

	fixture_init(&first);
	generation = arrive_all(&first);
	assert(smm_invocation_evidence_claim(&first.evidence, TEST_COMMAND,
		TEST_SENTINEL, &first.ops, &first_token) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(&first.evidence, &first_token,
		&first.ops) == CB_SUCCESS);
	depart_all(&first, generation);

	fixture_init(&second);
	assert(smm_invocation_evidence_shutdown(&second.evidence) == CB_SUCCESS);
	memset(&second.evidence, 0, sizeof(second.evidence));
	second.seed.loader_instance_nonce.high++;
	assert(smm_invocation_evidence_provision(&second.evidence, &second.seed) == CB_SUCCESS);
	generation = arrive_all(&second);
	assert(smm_invocation_evidence_claim(&second.evidence, TEST_COMMAND,
		TEST_SENTINEL, &second.ops, &second_token) == CB_SUCCESS);
	assert(memcmp(first_token.rendezvous_digest,
		second_token.rendezvous_digest,
		sizeof(first_token.rendezvous_digest)));
	assert(smm_invocation_evidence_abort(&second.evidence, &second_token,
		&second.ops) == CB_SUCCESS);
	depart_all(&second, generation);

	fixture_init(&third);
	assert(smm_invocation_evidence_shutdown(&third.evidence) == CB_SUCCESS);
	memset(&third.evidence, 0, sizeof(third.evidence));
	third.seed.loader_instance_nonce.low++;
	assert(smm_invocation_evidence_provision(&third.evidence, &third.seed) ==
		CB_SUCCESS);
	generation = arrive_all(&third);
	assert(smm_invocation_evidence_claim(&third.evidence, TEST_COMMAND,
		TEST_SENTINEL, &third.ops, &third_token) == CB_SUCCESS);
	assert(memcmp(first_token.rendezvous_digest,
		third_token.rendezvous_digest,
		sizeof(first_token.rendezvous_digest)));
	assert(smm_invocation_evidence_abort(&third.evidence, &third_token,
		&third.ops) == CB_SUCCESS);
	depart_all(&third, generation);

	fixture_init(&fourth);
	assert(smm_invocation_evidence_shutdown(&fourth.evidence) == CB_SUCCESS);
	memset(&fourth.evidence, 0, sizeof(fourth.evidence));
	fourth.seed.participant_apic_ids[TEST_CPUS - 1U]++;
	assert(smm_invocation_evidence_provision(&fourth.evidence,
		&fourth.seed) == CB_SUCCESS);
	generation = arrive_all(&fourth);
	assert(smm_invocation_evidence_claim(&fourth.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fourth.ops, &fourth_token) == CB_SUCCESS);
	assert(memcmp(first_token.rendezvous_digest,
		fourth_token.rendezvous_digest,
		sizeof(first_token.rendezvous_digest)));
	assert(smm_invocation_evidence_abort(&fourth.evidence, &fourth_token,
		&fourth.ops) == CB_SUCCESS);
	depart_all(&fourth, generation);
}

static void test_invalid_seed_and_duplicate(void)
{
	struct fixture fixture;
	uint64_t generation;

	memset(&fixture, 0, sizeof(fixture));
	fixture.seed = (struct smm_invocation_loader_seed) {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(fixture.seed),
		.active_cpus = 2,
		.loader_instance_nonce = NONCE(1),
		.participant_apic_ids = { 2, 2 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	assert(smm_invocation_evidence_provision(&fixture.evidence,
		&fixture.seed) == CB_ERR);
	fixture.seed.participant_apic_ids[1] = 3;
	fixture.seed.lifecycle = 0;
	assert(smm_invocation_evidence_provision(&fixture.evidence,
		&fixture.seed) == CB_ERR);

	fixture_init(&fixture);
	assert(test_arrive(&fixture.evidence, 0, 10,
		&generation) == CB_SUCCESS);
	assert(test_arrive(&fixture.evidence, 0, 10,
		&generation) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);
}

static void test_post_result_ambiguity_fail_stop(void)
{
	const pid_t child = fork();
	int status;

	assert(child >= 0);
	if (!child) {
		struct fixture fixture;
		struct smm_invocation_token token;

		fixture_init(&fixture);
		(void)arrive_all(&fixture);
		assert(smm_invocation_evidence_claim(&fixture.evidence,
			TEST_COMMAND, TEST_SENTINEL, &fixture.ops, &token) ==
			CB_SUCCESS);
		fixture.mock.fail_write = 1;
		(void)smm_invocation_evidence_publish(&fixture.evidence, &token,
			0x99ULL, &fixture.ops);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status));
	assert(WTERMSIG(status) == SIGABRT);
}

static void test_terminal_fail_stop_and_exhaustion(void)
{
	pid_t child;
	int status;
	struct fixture fixture;
	uint64_t generation;
	struct shutdown_arg shutdown;
	pthread_t owner;

	child = fork();
	assert(child >= 0);
	if (!child) {
		struct smm_invocation_token token;

		fixture_init(&fixture);
		(void)arrive_all(&fixture);
		assert(smm_invocation_evidence_claim(&fixture.evidence,
			TEST_COMMAND, TEST_SENTINEL, &fixture.ops, &token) ==
			CB_SUCCESS);
		(void)smm_invocation_evidence_shutdown(&fixture.evidence);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
	child = fork();
	assert(child >= 0);
	if (!child) {
		fixture_init(&fixture);
		__atomic_store_n(&fixture.evidence.state, UINT32_MAX,
			__ATOMIC_RELEASE);
		(void)smm_invocation_evidence_shutdown(&fixture.evidence);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);

	fixture_init(&fixture);
	test_hook_arm(38);
	shutdown = (struct shutdown_arg) { .evidence = &fixture.evidence };
	assert(!pthread_create(&owner, NULL, shutdown_thread, &shutdown));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	fail_stop_thread_exit = 1;
	__atomic_store_n(&fail_stop_calls, 0U, __ATOMIC_RELAXED);
	struct shutdown_arg timeout = { .evidence = &fixture.evidence };
	pthread_t waiter;
	assert(!pthread_create(&waiter, NULL, shutdown_thread, &timeout));
	assert(!pthread_join(waiter, NULL));
	assert(__atomic_load_n(&fail_stop_calls, __ATOMIC_RELAXED) == 1U);
	fail_stop_thread_exit = 0;
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(owner, NULL));
	assert(shutdown.result == CB_SUCCESS);
	assert(smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_CLOSED);

	fail_stop_exit_code = 97;
	__atomic_store_n(&fail_stop_calls, 0U, __ATOMIC_RELAXED);
	child = fork();
	assert(child >= 0);
	if (!child) {
		fixture_init(&fixture);
		test_hook_arm(38);
		shutdown = (struct shutdown_arg) { .evidence = &fixture.evidence };
		assert(!pthread_create(&owner, NULL, shutdown_thread, &shutdown));
		while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		(void)smm_invocation_evidence_shutdown(&fixture.evidence);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == fail_stop_exit_code);
	fail_stop_exit_code = 0;
	__atomic_store_n(&fail_stop_calls, 0U, __ATOMIC_RELAXED);

	fixture_init(&fixture);
	fixture.evidence.generation = UINT64_MAX;
	assert(test_arrive(&fixture.evidence, 0, 10,
		&generation) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);
}

static void test_abort_restore_mutation_and_reentry(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	fixture.mock.mutate_ops_on_write = 1;
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	fixture.mock.shutdown_on_write = 1;
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);
}

static void test_shutdown_at_callback_boundaries(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;
	struct smm_invocation_token token_before;
	struct operation_arg operation;
	struct shutdown_arg shutdown;
	pthread_t operation_thread;
	pthread_t stop_thread;
	uint64_t generation;

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	memset(&token, 0x5b, sizeof(token));
	memcpy(&token_before, &token, sizeof(token_before));
	__atomic_store_n(&fixture.mock.block_match, 1U, __ATOMIC_RELEASE);
	operation = (struct operation_arg) {
		.fixture = &fixture,
		.token = &token,
	};
	assert(!pthread_create(&operation_thread, NULL, claim_thread,
		&operation));
	while (!__atomic_load_n(&fixture.mock.match_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	shutdown = (struct shutdown_arg) { .evidence = &fixture.evidence };
	assert(!pthread_create(&stop_thread, NULL, shutdown_thread, &shutdown));
	while (!smm_invocation_evidence_shutdown_requested(&fixture.evidence))
		__asm__ volatile ("pause");
	__atomic_store_n(&fixture.mock.match_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(operation_thread, NULL));
	assert(operation.result == CB_ERR);
	assert(!memcmp(&token, &token_before, sizeof(token)));
	assert(!pthread_join(stop_thread, NULL));
	assert(shutdown.result == CB_ERR);
	if (smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_CLOSING) {
		depart_all(&fixture, generation);
		assert(smm_invocation_evidence_phase(&fixture.evidence) ==
			SMM_INVOCATION_CLOSED);
	} else {
		assert(smm_invocation_evidence_phase(&fixture.evidence) ==
			SMM_INVOCATION_POISONED);
	}

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	__atomic_store_n(&fixture.mock.block_read, 1U, __ATOMIC_RELEASE);
	operation = (struct operation_arg) {
		.fixture = &fixture,
		.token = &token,
	};
	assert(!pthread_create(&operation_thread, NULL, publish_thread,
		&operation));
	while (!__atomic_load_n(&fixture.mock.read_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	shutdown = (struct shutdown_arg) { .evidence = &fixture.evidence };
	assert(!pthread_create(&stop_thread, NULL, shutdown_thread, &shutdown));
	while (!smm_invocation_evidence_shutdown_requested(&fixture.evidence))
		__asm__ volatile ("pause");
	__atomic_store_n(&fixture.mock.read_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(operation_thread, NULL));
	assert(operation.result == CB_ERR);
	assert(fixture.mock.rax[0] == TEST_COMMAND);
	assert(!pthread_join(stop_thread, NULL));
	assert(shutdown.result == CB_ERR);
	if (smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_CLOSING) {
		depart_all(&fixture, generation);
		assert(smm_invocation_evidence_phase(&fixture.evidence) ==
			SMM_INVOCATION_CLOSED);
	} else {
		assert(smm_invocation_evidence_phase(&fixture.evidence) ==
			SMM_INVOCATION_POISONED);
	}
}

static void test_linearization_gaps(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;
	struct operation_arg operation;
	enum smm_invocation_evidence_phase phase;
	struct shutdown_arg shutdown;
	struct thread_arg participant;
	pthread_t first;
	pthread_t second;
	uint64_t generation;

	fixture_init(&fixture);
	test_hook_arm(1);
	participant = (struct thread_arg) { .fixture = &fixture, .cpu = 0 };
	assert(!pthread_create(&first, NULL, arrive_thread, &participant));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	shutdown = (struct shutdown_arg) { .evidence = &fixture.evidence };
	assert(!pthread_create(&second, NULL, shutdown_thread, &shutdown));
	while (!smm_invocation_evidence_shutdown_requested(&fixture.evidence))
		__asm__ volatile ("pause");
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(!pthread_join(second, NULL));
	assert(participant.result == CB_ERR && shutdown.result == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	test_hook_arm(2);
	operation = (struct operation_arg) {
		.fixture = &fixture,
		.token = &token,
	};
	assert(!pthread_create(&first, NULL, claim_thread, &operation));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	shutdown = (struct shutdown_arg) { .evidence = &fixture.evidence };
	assert(!pthread_create(&second, NULL, shutdown_thread, &shutdown));
	assert(!pthread_join(second, NULL));
	assert(shutdown.result == CB_ERR);
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(operation.result == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) == CB_SUCCESS);
	for (uint32_t cpu = 0; cpu < TEST_CPUS - 1U; cpu++)
		assert(test_depart(&fixture.evidence, cpu,
			generation) == CB_SUCCESS);
	test_hook_arm(3);
	participant = (struct thread_arg) {
		.fixture = &fixture,
		.cpu = 0,
		.generation = generation,
	};
	assert(!pthread_create(&first, NULL, depart_thread, &participant));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_depart_try(&fixture.evidence,
		TEST_CPUS - 1U, generation) == SMM_INVOCATION_TRY_RETRY);
	assert(smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_DEPARTURE_ADMITTING);
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(participant.result == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	test_hook_arm(4);
	operation = (struct operation_arg) {
		.fixture = &fixture,
		.token = &token,
	};
	assert(!pthread_create(&first, NULL, publish_thread, &operation));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	shutdown = (struct shutdown_arg) { .evidence = &fixture.evidence };
	assert(!pthread_create(&second, NULL, shutdown_thread, &shutdown));
	assert(!pthread_join(second, NULL));
	assert(shutdown.result == CB_ERR);
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(operation.result == CB_ERR);
	phase = smm_invocation_evidence_phase(&fixture.evidence);
	assert(phase == SMM_INVOCATION_CLOSING ||
		phase == SMM_INVOCATION_POISONED);
	if (phase == SMM_INVOCATION_CLOSING) {
		depart_all(&fixture, generation);
		assert(smm_invocation_evidence_phase(&fixture.evidence) ==
			SMM_INVOCATION_CLOSED);
	}
}

static void test_admission_completion_races(void)
{
	struct smm_invocation_token invocation;
	struct admission_arg admission;
	struct admission_arg failure;
	struct shutdown_arg shutdown;
	struct fixture fixture;
	pthread_t first;
	pthread_t second;
	uint64_t generation;
	uint32_t control;

	fixture_init(&fixture);
	test_hook_arm(35);
	admission = (struct admission_arg) { .fixture = &fixture };
	assert(!pthread_create(&first, NULL, arm_ack_thread, &admission));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&held_hook_entered, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&held_hook_release, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&held_hook_point, 24U, __ATOMIC_RELEASE);
	failure = admission;
	assert(!pthread_create(&second, NULL, admission_fail_thread, &failure));
	while (!__atomic_load_n(&held_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(admission.result == SMM_INVOCATION_TRY_ERROR);
	__atomic_store_n(&held_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(second, NULL));
	assert(failure.result == SMM_INVOCATION_TRY_ERROR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	test_hook_arm(35);
	admission = (struct admission_arg) { .fixture = &fixture };
	assert(!pthread_create(&first, NULL, arm_ack_thread, &admission));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_fetch_or(&fixture.evidence.state, 1U << 8,
		__ATOMIC_ACQ_REL);
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(admission.result == SMM_INVOCATION_TRY_ERROR);

	fixture_init(&fixture);
	test_hook_arm(35);
	admission = (struct admission_arg) { .fixture = &fixture };
	assert(!pthread_create(&first, NULL, arm_ack_thread, &admission));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_fetch_and(&fixture.evidence.state, ~(1U << 7),
		__ATOMIC_ACQ_REL);
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(admission.result == SMM_INVOCATION_TRY_ERROR);

	fixture_init(&fixture);
	test_hook_arm(35);
	admission = (struct admission_arg) { .fixture = &fixture };
	assert(!pthread_create(&first, NULL, arm_ack_thread, &admission));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	control = __atomic_load_n(&fixture.evidence.state, __ATOMIC_ACQUIRE);
	__atomic_store_n(&fixture.evidence.state,
		(control & ~0x1fU) | SMM_INVOCATION_COLLECTING,
		__ATOMIC_RELEASE);
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(admission.result == SMM_INVOCATION_TRY_ERROR);

	fixture_init(&fixture);
	generation = prepare_final_rendezvous_ack(&fixture);
	test_hook_arm(35);
	admission = (struct admission_arg) {
		.fixture = &fixture,
		.generation = generation,
		.cpu = TEST_CPUS - 1U,
	};
	assert(!pthread_create(&first, NULL, rendezvous_ack_thread,
		&admission));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_fetch_and(&fixture.evidence.state, ~(1U << 7),
		__ATOMIC_ACQ_REL);
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(admission.result == SMM_INVOCATION_TRY_ERROR);

	fixture_init(&fixture);
	generation = prepare_final_rendezvous_ack(&fixture);
	test_hook_arm(35);
	admission = (struct admission_arg) {
		.fixture = &fixture,
		.generation = generation,
		.cpu = TEST_CPUS - 1U,
	};
	assert(!pthread_create(&first, NULL, rendezvous_ack_thread,
		&admission));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&held_hook_entered, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&held_hook_release, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&held_hook_point, 24U, __ATOMIC_RELEASE);
	failure = admission;
	assert(!pthread_create(&second, NULL, admission_fail_thread, &failure));
	while (!__atomic_load_n(&held_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(admission.result == SMM_INVOCATION_TRY_ERROR);
	__atomic_store_n(&held_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(second, NULL));
	assert(smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	generation = prepare_final_rendezvous_ack(&fixture);
	test_hook_arm(35);
	admission = (struct admission_arg) {
		.fixture = &fixture,
		.generation = generation,
		.cpu = TEST_CPUS - 1U,
	};
	assert(!pthread_create(&first, NULL, rendezvous_ack_thread,
		&admission));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_fetch_or(&fixture.evidence.state, 1U << 8,
		__ATOMIC_ACQ_REL);
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(admission.result == SMM_INVOCATION_TRY_ERROR);

	fixture_init(&fixture);
	generation = prepare_final_rendezvous_ack(&fixture);
	test_hook_arm(35);
	admission = (struct admission_arg) {
		.fixture = &fixture,
		.generation = generation,
		.cpu = TEST_CPUS - 1U,
	};
	assert(!pthread_create(&first, NULL, rendezvous_ack_thread,
		&admission));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&held_hook_entered, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&held_hook_release, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&held_hook_point, 37U, __ATOMIC_RELEASE);
	shutdown = (struct shutdown_arg) { .evidence = &fixture.evidence };
	assert(!pthread_create(&second, NULL, shutdown_thread, &shutdown));
	while (!__atomic_load_n(&held_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(admission.result == SMM_INVOCATION_TRY_ERROR);
	__atomic_store_n(&held_hook_release, 1U, __ATOMIC_RELEASE);
	while (smm_invocation_evidence_phase(&fixture.evidence) !=
		SMM_INVOCATION_CLOSING)
		__asm__ volatile ("pause");
	depart_all(&fixture, generation);
	assert(!pthread_join(second, NULL));
	assert(shutdown.result == CB_SUCCESS);

	fixture_init(&fixture);
	generation = prepare_final_rendezvous_ack(&fixture);
	test_hook_arm(35);
	admission = (struct admission_arg) {
		.fixture = &fixture,
		.generation = generation,
		.cpu = TEST_CPUS - 1U,
	};
	assert(!pthread_create(&first, NULL, rendezvous_ack_thread,
		&admission));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &invocation) == CB_SUCCESS);
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(admission.result == SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_abort(&fixture.evidence, &invocation,
		&fixture.ops) == CB_SUCCESS);
	depart_all(&fixture, generation);
}

static void test_admission_and_duplicate_gaps(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;
	struct shutdown_arg shutdown;
	struct thread_arg first_arg;
	struct thread_arg second_arg;
	pthread_t first;
	pthread_t second;
	uint64_t generation = 0;

	fixture_init(&fixture);
	for (uint32_t cpu = 0; cpu < TEST_CPUS - 1U; cpu++)
		assert(test_arrive(&fixture.evidence, cpu,
			fixture.seed.participant_apic_ids[cpu], &generation) ==
			CB_SUCCESS);
	test_hook_arm(5);
	first_arg = (struct thread_arg) {
		.fixture = &fixture,
		.cpu = TEST_CPUS - 1U,
	};
	assert(!pthread_create(&first, NULL, arrive_thread, &first_arg));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	shutdown = (struct shutdown_arg) { .evidence = &fixture.evidence };
	assert(!pthread_create(&second, NULL, shutdown_thread, &shutdown));
	while (!smm_invocation_evidence_shutdown_requested(&fixture.evidence))
		__asm__ volatile ("pause");
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(first_arg.result == CB_ERR);
	while (smm_invocation_evidence_phase(&fixture.evidence) !=
		SMM_INVOCATION_CLOSING)
		__asm__ volatile ("pause");
	depart_all(&fixture, generation);
	assert(!pthread_join(second, NULL));
	assert(shutdown.result == CB_SUCCESS);

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) == CB_SUCCESS);
	for (uint32_t cpu = 0; cpu < TEST_CPUS - 1U; cpu++)
		assert(test_depart(&fixture.evidence, cpu,
			generation) == CB_SUCCESS);
	test_hook_arm(6);
	first_arg = (struct thread_arg) {
		.fixture = &fixture,
		.cpu = TEST_CPUS - 1U,
		.generation = generation,
	};
	assert(!pthread_create(&first, NULL, depart_thread, &first_arg));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_DEPARTURE_ADMITTING);
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(first_arg.result == CB_SUCCESS);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_READY);

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) == CB_SUCCESS);
	for (uint32_t cpu = 2; cpu < TEST_CPUS; cpu++)
		assert(test_depart(&fixture.evidence, cpu,
			generation) == CB_SUCCESS);
	test_hook_arm(54);
	first_arg = (struct thread_arg) {
		.fixture = &fixture,
		.cpu = 0,
		.generation = generation,
	};
	assert(!pthread_create(&first, NULL, depart_thread, &first_arg));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_CLOSING);
	assert(!__atomic_load_n(&fixture.evidence.departure_writers,
		__ATOMIC_ACQUIRE));
	assert(test_depart(&fixture.evidence, 1, generation) == CB_SUCCESS);
	assert(smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_READY);
	assert(!__atomic_load_n(&fixture.evidence.departure_writers,
		__ATOMIC_ACQUIRE));
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(first_arg.result == CB_SUCCESS);
	assert(smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_READY);
	assert(!__atomic_load_n(&fixture.evidence.departure_writers,
		__ATOMIC_ACQUIRE));

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) == CB_SUCCESS);
	test_hook_arm(6);
	first_arg = (struct thread_arg) {
		.fixture = &fixture,
		.cpu = 0,
		.generation = generation,
	};
	assert(!pthread_create(&first, NULL, depart_thread, &first_arg));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&fixture.evidence.generation, generation + 1U,
		__ATOMIC_RELEASE);
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(first_arg.result == CB_ERR);
	assert(!__atomic_load_n(&fixture.evidence.departed_cpus,
		__ATOMIC_ACQUIRE));

	fixture_init(&fixture);
	for (uint32_t cpu = 0; cpu < TEST_CPUS - 1U; cpu++)
		assert(test_arrive(&fixture.evidence, cpu,
			fixture.seed.participant_apic_ids[cpu], &generation) ==
			CB_SUCCESS);
	test_hook_arm(8);
	first_arg = (struct thread_arg) {
		.fixture = &fixture,
		.cpu = TEST_CPUS - 1U,
	};
	assert(!pthread_create(&first, NULL, arrive_thread, &first_arg));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(first_arg.result == CB_SUCCESS);
	assert(first_arg.generation == generation);
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) == CB_SUCCESS);
	depart_all(&fixture, generation);

	fixture_init(&fixture);
	test_hook_arm(8);
	first_arg = (struct thread_arg) { .fixture = &fixture, .cpu = 0 };
	assert(!pthread_create(&first, NULL, arrive_thread, &first_arg));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_evidence_rendezvous_fail(&fixture.evidence,
		first_arg.generation) == CB_ERR);
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(first_arg.result == CB_ERR);
	assert(!first_arg.generation);
	assert(smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	test_hook_arm(8);
	first_arg = (struct thread_arg) { .fixture = &fixture, .cpu = 0 };
	assert(!pthread_create(&first, NULL, arrive_thread, &first_arg));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&held_hook_entered, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&held_hook_release, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&held_hook_point, 23U, __ATOMIC_RELEASE);
	second_arg = (struct thread_arg) {
		.fixture = &fixture,
		.generation = first_arg.generation,
	};
	assert(!pthread_create(&second, NULL, rendezvous_fail_thread,
		&second_arg));
	while (!__atomic_load_n(&held_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(first_arg.result == CB_ERR && !first_arg.generation);
	__atomic_store_n(&held_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(second, NULL));
	assert(second_arg.result == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) ==
		SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	test_hook_arm(8);
	first_arg = (struct thread_arg) { .fixture = &fixture, .cpu = 0 };
	assert(!pthread_create(&first, NULL, arrive_thread, &first_arg));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_fetch_or(&fixture.evidence.state, 1U << 8,
		__ATOMIC_ACQ_REL);
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(first_arg.result == CB_ERR && !first_arg.generation);

	fixture_init(&fixture);
	test_hook_arm(8);
	first_arg = (struct thread_arg) { .fixture = &fixture, .cpu = 0 };
	assert(!pthread_create(&first, NULL, arrive_thread, &first_arg));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_fetch_and(&fixture.evidence.state, ~(1U << 7),
		__ATOMIC_ACQ_REL);
	__atomic_store_n(&test_hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(first, NULL));
	assert(first_arg.result == CB_ERR && !first_arg.generation);

	fixture_init(&fixture);
	test_hook_arm(7);
	first_arg = (struct thread_arg) { .fixture = &fixture, .cpu = 0 };
	second_arg = (struct thread_arg) { .fixture = &fixture, .cpu = 0 };
	assert(!pthread_create(&first, NULL, arrive_thread, &first_arg));
	while (!__atomic_load_n(&test_hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(!pthread_create(&second, NULL, arrive_thread, &second_arg));
	assert(!pthread_join(second, NULL));
	assert(second_arg.result == CB_ERR);
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(first_arg.result == CB_SUCCESS);
	assert(test_arrive(&fixture.evidence, 0, 10,
		&second_arg.generation) == CB_ERR);
	assert(smm_invocation_evidence_phase(&fixture.evidence) == SMM_INVOCATION_POISONED);
}

int main(void)
{
	test_happy_path();
	test_missing_and_wrong_participant();
	test_exact_one_and_bsp();
	test_save_state_failures_and_mutation();
	test_publish_guard_and_active_shutdown();
	test_strong_completion_rejection_is_inert();
	test_strong_completion_success();
	test_strong_completion_zero_result();
	test_strong_completion_external_snapshot_is_immutable();
	test_strong_completion_post_ownership_fail_stop();
	test_strong_completion_semantic_rejection_fail_stop();
	test_strong_completion_callback_fail_stop();
	test_strong_completion_claim_and_abort_reentry();
	test_strong_completion_acquisition_cas_loss();
	test_strong_completion_second_phase_sample();
	test_strong_completion_claimed_control_corruption();
	test_reentry_stale_and_collision();
	test_invalid_match_and_aliases();
	test_token_binds_loader_and_topology();
	test_sparse_nonce_lifecycle();
	test_invalid_seed_and_duplicate();
	test_post_result_ambiguity_fail_stop();
	test_terminal_fail_stop_and_exhaustion();
	test_abort_restore_mutation_and_reentry();
	test_shutdown_at_callback_boundaries();
	test_linearization_gaps();
	test_admission_completion_races();
	test_admission_and_duplicate_gaps();
	return 0;
}
