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

static uint32_t test_hook_point;
static uint32_t test_hook_entered;
static uint32_t test_hook_release;

void smm_invocation_evidence_test_hook(uint32_t point)
{
	uint32_t expected = point;

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

struct mock_state {
	uint64_t rax[TEST_CPUS];
	uint32_t matched;
	uint32_t second_match;
	uint32_t fail_read;
	uint32_t fail_write;
	uint32_t mutate_ops;
	uint32_t mutate_ops_on_write;
	uint32_t shutdown_on_write;
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
	enum cb_err result;
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

static void __noreturn test_fail_stop(void *context)
{
	(void)context;
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
		.boot_generation = 7,
		.participant_apic_ids = { 10, 11, 12, 13 },
		.lifecycle = SMM_INVOCATION_LOADER_COLD,
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
		&fixture->seed, test_fail_stop, NULL, 0) == CB_SUCCESS);
}

static void *arrive_thread(void *opaque)
{
	struct thread_arg *arg = opaque;

	arg->result = smm_invocation_evidence_arrive(&arg->fixture->evidence,
		arg->cpu, arg->fixture->seed.participant_apic_ids[arg->cpu],
		&arg->generation);
	return NULL;
}

static void *depart_thread(void *opaque)
{
	struct thread_arg *arg = opaque;

	arg->result = smm_invocation_evidence_depart(&arg->fixture->evidence,
		arg->cpu, arg->generation);
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

static void *terminal_observer_thread(void *opaque)
{
	struct observer_arg *arg = opaque;

	while (__atomic_load_n(&arg->evidence->phase, __ATOMIC_ACQUIRE) !=
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
		assert(smm_invocation_evidence_arrive(&fixture->evidence, cpu,
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
		assert(smm_invocation_evidence_depart(&fixture->evidence, cpu,
			generation) == CB_SUCCESS);
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
	assert(smm_invocation_evidence_depart(&fixture.evidence, 0,
		generation + 1U) == CB_ERR);
	depart_all(&fixture, generation);
	assert(!pthread_join(observer_thread, NULL));
	assert(fixture.evidence.phase == SMM_INVOCATION_READY);
	assert(!bytes_nonzero(&fixture.evidence.token,
		sizeof(fixture.evidence.token)));
	assert(smm_invocation_evidence_shutdown(&fixture.evidence) == CB_SUCCESS);
	assert(fixture.evidence.phase == SMM_INVOCATION_CLOSED);
}

static void test_missing_and_wrong_participant(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;
	uint64_t generation;

	fixture_init(&fixture);
	assert(smm_invocation_evidence_arrive(&fixture.evidence, 0, 99,
		&generation) == CB_ERR);
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);
	fixture_init(&fixture);
	for (uint32_t cpu = 0; cpu < TEST_CPUS - 1; cpu++)
		assert(smm_invocation_evidence_arrive(&fixture.evidence, cpu,
			10U + cpu, &generation) == CB_SUCCESS);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(smm_invocation_evidence_arrive(&fixture.evidence, 3, 13,
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
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	fixture.mock.matched = 1;
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);
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
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);
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
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);
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
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	fixture.mock.shutdown_reenter = 1;
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	shutdown = (struct shutdown_arg) { .evidence = &fixture.evidence };
	assert(!pthread_create(&thread, NULL, shutdown_thread, &shutdown));
	while (__atomic_load_n(&fixture.evidence.phase, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_CLOSING)
		__asm__ volatile ("pause");
	depart_all(&fixture, generation);
	assert(!pthread_join(thread, NULL));
	assert(shutdown.result == CB_SUCCESS);
	assert(fixture.evidence.phase == SMM_INVOCATION_CLOSED);
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
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	stale = token;
	assert(smm_invocation_evidence_publish(&fixture.evidence, &token,
		TEST_SENTINEL, &fixture.ops) == CB_ERR);
	assert(fixture.evidence.phase == SMM_INVOCATION_CLOSING);
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
			sizeof(struct smm_invocation_evidence) - 1U),
		test_fail_stop, NULL, 0) == CB_ERR);

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	fixture.mock.invalid_match = 1;
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_ERR);
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	assert(smm_invocation_evidence_arrive(&fixture.evidence, 0, 10,
		&fixture.evidence.boot_generation) == CB_ERR);

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
	struct smm_invocation_token first_token;
	struct smm_invocation_token second_token;
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
	second.seed.boot_generation++;
	second.seed.participant_apic_ids[1] = 21;
	assert(smm_invocation_evidence_provision(&second.evidence, &second.seed,
		test_fail_stop, NULL, 0) == CB_SUCCESS);
	generation = arrive_all(&second);
	assert(smm_invocation_evidence_claim(&second.evidence, TEST_COMMAND,
		TEST_SENTINEL, &second.ops, &second_token) == CB_SUCCESS);
	assert(memcmp(first_token.rendezvous_digest,
		second_token.rendezvous_digest,
		sizeof(first_token.rendezvous_digest)));
	assert(smm_invocation_evidence_abort(&second.evidence, &second_token,
		&second.ops) == CB_SUCCESS);
	depart_all(&second, generation);
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
		.boot_generation = 1,
		.participant_apic_ids = { 2, 2 },
		.lifecycle = SMM_INVOCATION_LOADER_COLD,
	};
	assert(smm_invocation_evidence_provision(&fixture.evidence,
		&fixture.seed, test_fail_stop, NULL, 0) == CB_ERR);
	fixture.seed.participant_apic_ids[1] = 3;
	fixture.seed.lifecycle = 0;
	assert(smm_invocation_evidence_provision(&fixture.evidence,
		&fixture.seed, test_fail_stop, NULL, 0) == CB_ERR);

	fixture_init(&fixture);
	assert(smm_invocation_evidence_arrive(&fixture.evidence, 0, 10,
		&generation) == CB_SUCCESS);
	assert(smm_invocation_evidence_arrive(&fixture.evidence, 0, 10,
		&generation) == CB_ERR);
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);
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
		__atomic_store_n(&fixture.evidence.phase, UINT32_MAX,
			__ATOMIC_RELEASE);
		(void)smm_invocation_evidence_shutdown(&fixture.evidence);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);

	fixture_init(&fixture);
	fixture.evidence.generation = UINT64_MAX;
	assert(smm_invocation_evidence_arrive(&fixture.evidence, 0, 10,
		&generation) == CB_ERR);
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);
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
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	fixture.mock.shutdown_on_write = 1;
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) == CB_ERR);
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);
}

static void test_shutdown_at_callback_boundaries(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;
	struct operation_arg operation;
	struct shutdown_arg shutdown;
	pthread_t operation_thread;
	pthread_t stop_thread;

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
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
	while (!__atomic_load_n(&fixture.evidence.shutdown_requested,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&fixture.mock.match_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(operation_thread, NULL));
	assert(operation.result == CB_ERR);
	assert(!pthread_join(stop_thread, NULL));
	assert(shutdown.result == CB_ERR);
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	(void)arrive_all(&fixture);
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
	while (!__atomic_load_n(&fixture.evidence.shutdown_requested,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	__atomic_store_n(&fixture.mock.read_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(operation_thread, NULL));
	assert(operation.result == CB_ERR);
	assert(!pthread_join(stop_thread, NULL));
	assert(shutdown.result == CB_ERR);
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);
}

static void test_linearization_gaps(void)
{
	struct fixture fixture;
	struct smm_invocation_token token;
	struct operation_arg operation;
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
	while (!__atomic_load_n(&fixture.evidence.shutdown_requested,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(!pthread_join(second, NULL));
	assert(participant.result == CB_ERR && shutdown.result == CB_ERR);
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);

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
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);

	fixture_init(&fixture);
	generation = arrive_all(&fixture);
	assert(smm_invocation_evidence_claim(&fixture.evidence, TEST_COMMAND,
		TEST_SENTINEL, &fixture.ops, &token) == CB_SUCCESS);
	assert(smm_invocation_evidence_abort(&fixture.evidence, &token,
		&fixture.ops) == CB_SUCCESS);
	for (uint32_t cpu = 0; cpu < TEST_CPUS - 1U; cpu++)
		assert(smm_invocation_evidence_depart(&fixture.evidence, cpu,
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
	assert(smm_invocation_evidence_depart(&fixture.evidence, TEST_CPUS - 1U,
		generation) == CB_SUCCESS);
	assert(fixture.evidence.phase == SMM_INVOCATION_CLOSE_CLEANING);
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(participant.result == CB_ERR);
	assert(fixture.evidence.phase == SMM_INVOCATION_READY);

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
	assert(fixture.evidence.phase == SMM_INVOCATION_CLOSING);
	depart_all(&fixture, generation);
	assert(fixture.evidence.phase == SMM_INVOCATION_CLOSED);
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
		assert(smm_invocation_evidence_arrive(&fixture.evidence, cpu,
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
	while (!__atomic_load_n(&fixture.evidence.shutdown_requested,
		__ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(first_arg.result == CB_ERR);
	while (__atomic_load_n(&fixture.evidence.phase, __ATOMIC_ACQUIRE) !=
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
		assert(smm_invocation_evidence_depart(&fixture.evidence, cpu,
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
	assert(fixture.evidence.phase == SMM_INVOCATION_DEPARTURE_ADMITTING);
	test_hook_wait_and_release();
	assert(!pthread_join(first, NULL));
	assert(first_arg.result == CB_SUCCESS);
	assert(fixture.evidence.phase == SMM_INVOCATION_READY);

	fixture_init(&fixture);
	for (uint32_t cpu = 0; cpu < TEST_CPUS - 1U; cpu++)
		assert(smm_invocation_evidence_arrive(&fixture.evidence, cpu,
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
	assert(first_arg.result == CB_ERR);
	assert(fixture.evidence.phase == SMM_INVOCATION_POISONED);
}

int main(void)
{
	test_happy_path();
	test_missing_and_wrong_participant();
	test_exact_one_and_bsp();
	test_save_state_failures_and_mutation();
	test_publish_guard_and_active_shutdown();
	test_reentry_stale_and_collision();
	test_invalid_match_and_aliases();
	test_token_binds_loader_and_topology();
	test_invalid_seed_and_duplicate();
	test_post_result_ambiguity_fail_stop();
	test_terminal_fail_stop_and_exhaustion();
	test_abort_restore_mutation_and_reentry();
	test_shutdown_at_callback_boundaries();
	test_linearization_gaps();
	test_admission_and_duplicate_gaps();
	return 0;
}
