/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cpu/intel/em64t100_save_state.h>
#include <cpu/intel/em64t101_save_state.h>
#include <cpu/intel/smm_invocation_adapter_provider.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_command.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#undef assert
extern int dprintf(int descriptor, const char *format, ...);
#define assert(condition) do { if (!(condition)) { \
	dprintf(2, "assertion line %d: %s\n", __LINE__, #condition); abort(); \
} } while (0)

#define REV100 0x30100U
#define REV101 0x30101U
#define TEST_CPUS 4U

static _Alignas(16) em64t101_smm_state_save_area_t states[TEST_CPUS + 1U];
static _Alignas(16) em64t100_smm_state_save_area_t states100[TEST_CPUS];
static _Alignas(16) em64t101_smm_state_save_area_t
	max_states[SMM_INVOCATION_EVIDENCE_MAX_CPUS];
static uint32_t runtime_failure;
static uint32_t runtime_cpus = TEST_CPUS;
static uint32_t view_calls;
static uint32_t count_calls;
static uint32_t span_calls;
static uintptr_t span_offset;
static bool max_fixture;
static uint32_t hook_point;
static enum smm_invocation_try_result hook_result;
static const struct smm_save_state_ops *selected_native_ops;
static bool revision_table_override;
static const uint32_t *selected_revision_table;
static atomic_uint threaded_hook_entered;
static atomic_uint threaded_hook_release;
static bool threaded_hook;

#ifndef PROVIDER_SOURCE
#define PROVIDER_SOURCE \
	"../../../src/soc/intel/common/block/smm/invocation_adapter_provider.c"
#endif
#include PROVIDER_SOURCE

void intel_smm_invocation_adapter_test_hook(uint32_t point)
{
	(void)point;
}

size_t intel_smm_invocation_adapter_test_revision_size(
	uint32_t layout_revision, size_t native_size)
{
	(void)layout_revision;
	return native_size;
}

void __noreturn smm_invocation_platform_fail_stop(void)
{
	abort();
}

void intel_smm_invocation_adapter_provider_test_hook(uint32_t point)
{
	const struct smm_invocation_save_state_ops *ops = (const void *)0xa5a5U;
	uint64_t generation = 0xa5a5a5a5a5a5a5a5ULL;

	if (hook_point != point)
		return;
	if (threaded_hook) {
		atomic_store_explicit(&threaded_hook_entered, 1U,
			memory_order_release);
		while (!atomic_load_explicit(&threaded_hook_release,
			memory_order_acquire))
			;
		return;
	}
	hook_point = 0;
	if (point == 1)
		hook_result = intel_smm_invocation_adapter_provider_provision(&ops);
	else if (point == 2)
		hook_result = intel_smm_invocation_adapter_provider_arm(&generation);
	else if (point == 3)
		hook_result = intel_smm_invocation_adapter_provider_retire(
			provider.active_generation);
	else
		hook_result = intel_smm_invocation_adapter_provider_arm(&generation);
	assert(ops == (const void *)0xa5a5U);
	assert(generation == 0xa5a5a5a5a5a5a5a5ULL);
}

static const uint32_t revisions100[] = { REV100, SMM_REV_INVALID };
static const uint32_t revisions101[] = { REV101, SMM_REV_INVALID };
const struct smm_save_state_ops em64t100_smm_ops = {
	.revision_table = revisions100,
};
const struct smm_save_state_ops em64t101_smm_ops = {
	.revision_table = revisions101,
};

const struct smm_save_state_ops *get_smm_save_state_ops(void)
{
	return selected_native_ops;
}

const uint32_t *intel_smm_invocation_adapter_provider_test_revision_table(
	const struct smm_save_state_ops *ops)
{
	return revision_table_override ? selected_revision_table :
		ops->revision_table;
}

enum cb_err smm_invocation_runtime_view_get(
	const struct smm_invocation_runtime_view **view)
{
	view_calls++;
	*view = (const void *)0x1000U;
	if (runtime_failure == 1U)
		return CB_ERR;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_cpu_count(
	const struct smm_invocation_runtime_view *view, uint32_t *active_cpus)
{
	count_calls++;
	assert(view == (const void *)0x1000U);
	*active_cpus = runtime_cpus;
	if (runtime_failure == 2U)
		return CB_ERR;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_save_state_span(
	const struct smm_invocation_runtime_view *view, uint32_t cpu,
	struct smm_save_state_span *span)
{
	span_calls++;
	assert(view == (const void *)0x1000U);
	if (runtime_failure == 3U ||
	    cpu >= (max_fixture ? SMM_INVOCATION_EVIDENCE_MAX_CPUS : TEST_CPUS))
		return CB_ERR;
	if (max_fixture)
		*span = (struct smm_save_state_span) {
			.base = (uintptr_t)&max_states[cpu],
			.size = sizeof(max_states[cpu]),
		};
	else if (selected_native_ops == &em64t100_smm_ops)
		*span = (struct smm_save_state_span) {
			.base = (uintptr_t)&states100[cpu] + span_offset,
			.size = sizeof(states100[cpu]),
		};
	else
		*span = (struct smm_save_state_span) {
			.base = (uintptr_t)&states[cpu] + span_offset,
			.size = sizeof(states[cpu]),
		};
	return CB_SUCCESS;
}

static void reset_provider(void)
{
	memset(&provider, 0, sizeof(provider));
	memset(states, 0, sizeof(states));
	memset(states100, 0, sizeof(states100));
	runtime_failure = 0;
	runtime_cpus = TEST_CPUS;
	view_calls = 0;
	count_calls = 0;
	span_calls = 0;
	span_offset = 0;
	max_fixture = false;
	hook_point = 0;
	hook_result = SMM_INVOCATION_TRY_ERROR;
	selected_native_ops = &em64t101_smm_ops;
	revision_table_override = false;
	selected_revision_table = NULL;
	atomic_store(&threaded_hook_entered, 0U);
	atomic_store(&threaded_hook_release, 0U);
	threaded_hook = false;
}

static const struct smm_invocation_save_state_ops *provision(uint32_t revision)
{
	const struct smm_invocation_save_state_ops *ops = NULL;

	selected_native_ops = revision == REV100 ? &em64t100_smm_ops :
		&em64t101_smm_ops;
	assert(intel_smm_invocation_adapter_provider_provision(&ops) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(ops == &provider.ops);
	assert(ops->context == &provider.adapter);
	assert(ops->context_size == sizeof(provider.adapter));
	assert(view_calls == 1U && count_calls == 1U && span_calls == TEST_CPUS);
	return ops;
}

static void two_independent_rounds(void)
{
	const struct smm_invocation_save_state_ops *ops;
	struct smm_invocation_save_state_ops descriptor;
	uint64_t first;
	uint64_t second;

	reset_provider();
	ops = provision(REV101);
	states[0].smm_revision = REV101;
	states[0].io_misc_info = (0xb2U << 16) | 3U;
	states[0].rax = 0x5aU;
	assert(ops->match_apmc_write(ops->context, 0, 0x5aU) ==
		SMM_INVOCATION_MATCH_ERROR);
	{
		uint64_t unchanged = UINT64_MAX;

		assert(ops->read_value(ops->context, 0, &unchanged) == CB_ERR);
		assert(unchanged == UINT64_MAX);
		assert(ops->write_value(ops->context, 0, 0x1122U) == CB_ERR);
		assert(states[0].rax == 0x5aU);
	}
	memcpy(&descriptor, ops, sizeof(descriptor));
	assert(intel_smm_invocation_adapter_provider_provision(&ops) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(!memcmp(&descriptor, ops, sizeof(descriptor)));
	assert(intel_smm_invocation_adapter_provider_arm(&first) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(first == 1U && provider.adapter.invocation_nonce == 2U);
	assert(ops->match_apmc_write(ops->context, 0, 0x5aU) ==
		SMM_INVOCATION_MATCHED);
	{
		const struct intel_smm_invocation_adapter captured = provider.adapter;

		assert(ops->match_apmc_write(ops->context, 0, 0x5aU) ==
			SMM_INVOCATION_MATCHED);
		assert(!memcmp(&captured, &provider.adapter, sizeof(captured)));
		states[1].smm_revision = REV101;
		assert(ops->match_apmc_write(ops->context, 1, 0x5aU) ==
			SMM_INVOCATION_NOT_MATCHED);
		assert(ops->match_apmc_write(ops->context, 0, 0x5bU) ==
			SMM_INVOCATION_MATCH_ERROR);
		states[0].rax |= 1ULL << 32;
		assert(ops->match_apmc_write(ops->context, 0, 0x5aU) ==
			SMM_INVOCATION_MATCH_ERROR);
		states[0].rax = captured.matched_rax;
		states[0].rcx++;
		assert(ops->match_apmc_write(ops->context, 0, 0x5aU) ==
			SMM_INVOCATION_MATCH_ERROR);
		states[0].rcx = captured.matched_rcx;
		provider.adapter.invocation_nonce++;
		assert(ops->match_apmc_write(ops->context, 0, 0x5aU) ==
			SMM_INVOCATION_MATCH_ERROR);
		provider.adapter.invocation_nonce = captured.invocation_nonce;
		provider.adapter.matched_command++;
		assert(ops->match_apmc_write(ops->context, 0, 0x5aU) ==
			SMM_INVOCATION_MATCH_ERROR);
		provider.adapter.matched_command = captured.matched_command;
		assert(!memcmp(&captured, &provider.adapter, sizeof(captured)));
	}
	second = UINT64_MAX;
	assert(intel_smm_invocation_adapter_provider_arm(&second) ==
		SMM_INVOCATION_TRY_RETRY && second == UINT64_MAX);
	assert(intel_smm_invocation_adapter_provider_retire(first) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(ops->match_apmc_write(ops->context, 0, 0x5aU) ==
		SMM_INVOCATION_MATCH_ERROR);
	{
		uint64_t unchanged = UINT64_MAX;
		const uint64_t native_rax = states[0].rax;

		assert(ops->read_value(ops->context, 0, &unchanged) == CB_ERR);
		assert(unchanged == UINT64_MAX);
		assert(ops->write_value(ops->context, 0, 0x1122U) == CB_ERR);
		assert(states[0].rax == native_rax);
	}
	assert(intel_smm_invocation_adapter_provider_arm(&second) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(second == 2U && provider.adapter.invocation_nonce == 3U);
	assert(!memcmp(&descriptor, ops, sizeof(descriptor)));
	assert(intel_smm_invocation_adapter_provider_retire(second) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(provider.active_generation == 0U);
}

static void invalid_and_conflicting_provision(void)
{
	static const uint32_t unsupported[] = { 0x30102U, SMM_REV_INVALID };
	static const uint32_t unterminated[] = { REV101, 0 };
	static const struct smm_save_state_ops impostor = {
		.revision_table = revisions101,
	};
	const struct smm_invocation_save_state_ops *ops = (const void *)0xa5a5U;
	const struct smm_invocation_save_state_ops *stable;

	reset_provider();
	selected_native_ops = NULL;
	assert(intel_smm_invocation_adapter_provider_provision(&ops) ==
		SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_EMPTY && ops == (const void *)0xa5a5U);
	selected_native_ops = &em64t101_smm_ops;
	revision_table_override = true;
	selected_revision_table = NULL;
	assert(intel_smm_invocation_adapter_provider_provision(&ops) ==
		SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_EMPTY && ops == (const void *)0xa5a5U);
	selected_revision_table = unsupported;
	assert(intel_smm_invocation_adapter_provider_provision(&ops) ==
		SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_EMPTY && ops == (const void *)0xa5a5U);
	selected_revision_table = unterminated;
	assert(intel_smm_invocation_adapter_provider_provision(&ops) ==
		SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_EMPTY && ops == (const void *)0xa5a5U);
	revision_table_override = false;
	selected_native_ops = &impostor;
	assert(intel_smm_invocation_adapter_provider_provision(&ops) ==
		SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_EMPTY && ops == (const void *)0xa5a5U);
	stable = provision(REV100);
	assert(provider.adapter.expected_revision == REV100);
	assert(provider.adapter.nodes[0].save_state == (uintptr_t)&states100[0]);
	ops = (const void *)0xa5a5U;
	selected_native_ops = &em64t101_smm_ops;
	assert(intel_smm_invocation_adapter_provider_provision(&ops) ==
		SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_READY && ops == (const void *)0xa5a5U);
	assert(stable == &provider.ops);
}

static void canonical_revision_guards(void)
{
	static const uint32_t unsupported[] = { 0x30102U, SMM_REV_INVALID };
	static const uint32_t unterminated[] = { REV101, 0 };
	static const struct smm_save_state_ops impostor = {
		.revision_table = revisions101,
	};
	uint32_t revision = 0xa5a5a5a5U;

	reset_provider();
	selected_native_ops = &impostor;
	assert(canonical_revision(&revision) == CB_ERR);
	assert(revision == 0xa5a5a5a5U);
	selected_native_ops = &em64t101_smm_ops;
	revision_table_override = true;
	selected_revision_table = NULL;
	assert(canonical_revision(&revision) == CB_ERR);
	assert(revision == 0xa5a5a5a5U);
	selected_revision_table = unsupported;
	assert(canonical_revision(&revision) == CB_ERR);
	assert(revision == 0xa5a5a5a5U);
	selected_revision_table = unterminated;
	assert(canonical_revision(&revision) == CB_ERR);
	assert(revision == 0xa5a5a5a5U);
	revision_table_override = false;
	selected_native_ops = &em64t100_smm_ops;
	assert(canonical_revision(&revision) == CB_SUCCESS && revision == REV100);
}

static void failure_poison_and_scrub(void)
{
	for (uint32_t failure = 1; failure <= 3; failure++) {
		const struct smm_invocation_save_state_ops *ops =
			(const void *)0xa5a5U;

		reset_provider();
		runtime_failure = failure;
		assert(intel_smm_invocation_adapter_provider_provision(&ops) ==
			SMM_INVOCATION_TRY_ERROR);
		assert(provider.state == PROVIDER_POISONED);
		for (size_t index = sizeof(provider.state); index < sizeof(provider);
		     index++)
			assert(!((const uint8_t *)&provider)[index]);
		assert(ops == (const void *)0xa5a5U);
		runtime_failure = 0;
		assert(intel_smm_invocation_adapter_provider_provision(&ops) ==
			SMM_INVOCATION_TRY_ERROR);
	}
}

static void invalid_runtime_counts_poison(void)
{
	const struct smm_invocation_save_state_ops *ops =
		(const void *)0xa5a5U;

	reset_provider();
	runtime_cpus = 0;
	assert(intel_smm_invocation_adapter_provider_provision(&ops) ==
		SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_POISONED);

	reset_provider();
	runtime_cpus = SMM_INVOCATION_EVIDENCE_MAX_CPUS;
	max_fixture = true;
	ops = NULL;
	assert(intel_smm_invocation_adapter_provider_provision(&ops) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(provider.adapter.active_cpus == SMM_INVOCATION_EVIDENCE_MAX_CPUS);
	assert(span_calls == SMM_INVOCATION_EVIDENCE_MAX_CPUS);

	reset_provider();
	runtime_cpus = SMM_INVOCATION_EVIDENCE_MAX_CPUS + 1U;
	max_fixture = true;
	ops = (const void *)0xa5a5U;
	assert(intel_smm_invocation_adapter_provider_provision(&ops) ==
		SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_POISONED && span_calls == 0U);
	assert(ops == (const void *)0xa5a5U);
}

static void descriptor_corruption_poison(void)
{
	for (size_t index = 0; index < sizeof(provider.ops); index++) {
		uint64_t generation = UINT64_MAX;

		reset_provider();
		provision(REV101);
		((uint8_t *)&provider.ops)[index] ^= 1U;
		assert(intel_smm_invocation_adapter_provider_arm(&generation) ==
			SMM_INVOCATION_TRY_ERROR);
		assert(provider.state == PROVIDER_POISONED &&
			generation == UINT64_MAX);
	}
}

static void contention_and_nonretention(void)
{
	uint64_t generation;

	reset_provider();
	hook_point = 1;
	provision(REV101);
	assert(hook_result == SMM_INVOCATION_TRY_RETRY);
	runtime_failure = 3;
	hook_point = 2;
	assert(intel_smm_invocation_adapter_provider_arm(&generation) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(hook_result == SMM_INVOCATION_TRY_RETRY);
	hook_point = 3;
	assert(intel_smm_invocation_adapter_provider_retire(generation) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(hook_result == SMM_INVOCATION_TRY_RETRY);
}

static void stale_wrong_and_exhaustion_poison(void)
{
	uint64_t generation;

	reset_provider();
	provision(REV101);
	assert(intel_smm_invocation_adapter_provider_arm(&generation) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(intel_smm_invocation_adapter_provider_retire(generation + 1U) ==
		SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_POISONED && !provider.ops.context);
	assert(intel_smm_invocation_adapter_provider_retire(generation) ==
		SMM_INVOCATION_TRY_ERROR);

	reset_provider();
	provision(REV101);
	provider.last_generation = UINT64_MAX;
	generation = 0xa5a5a5a5a5a5a5a5ULL;
	assert(intel_smm_invocation_adapter_provider_arm(&generation) ==
		SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_POISONED);
	assert(generation == 0xa5a5a5a5a5a5a5a5ULL);
}

static void stale_prior_round_poison(void)
{
	uint64_t first;
	uint64_t second;

	reset_provider();
	provision(REV101);
	assert(intel_smm_invocation_adapter_provider_arm(&first) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(intel_smm_invocation_adapter_provider_retire(first) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(intel_smm_invocation_adapter_provider_arm(&second) ==
		SMM_INVOCATION_TRY_SUCCESS && second != first);
	assert(intel_smm_invocation_adapter_provider_retire(first) ==
		SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_POISONED);
}

static void output_aliases_are_rejected(void)
{
	const struct smm_invocation_save_state_ops *ops = (const void *)0xa5a5U;
	uint64_t generation = 0xa5a5a5a5a5a5a5a5ULL;

	reset_provider();
	assert(intel_smm_invocation_adapter_provider_provision(
		(void *)&provider) == SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_EMPTY);
	provision(REV101);
	assert(intel_smm_invocation_adapter_provider_arm(
		(void *)&provider) == SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_READY);
	assert(ops == (const void *)0xa5a5U);
	assert(generation == 0xa5a5a5a5a5a5a5a5ULL);

	reset_provider();
	span_offset = 4U;
	assert(intel_smm_invocation_adapter_provider_provision(
		(void *)&states[0]) == SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_POISONED);
	reset_provider();
	span_offset = 4U;
	provision(REV101);
	assert(intel_smm_invocation_adapter_provider_provision(
		(void *)&states[0]) == SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_READY);
	assert(intel_smm_invocation_adapter_provider_arm(
		(void *)&states[0]) == SMM_INVOCATION_TRY_ERROR);
	assert(provider.state == PROVIDER_READY);
	assert(intel_smm_invocation_adapter_provider_provision(
		(void *)((uint8_t *)&ops + 1U)) == SMM_INVOCATION_TRY_ERROR);
	assert(intel_smm_invocation_adapter_provider_arm(
		(void *)((uint8_t *)&generation + 1U)) ==
		SMM_INVOCATION_TRY_ERROR);
}

struct thread_call {
	enum smm_invocation_try_result result;
	const struct smm_invocation_save_state_ops *ops;
	uint64_t generation;
};

static void *thread_provision(void *argument)
{
	struct thread_call *call = argument;

	call->result = intel_smm_invocation_adapter_provider_provision(&call->ops);
	return NULL;
}

static void *thread_arm(void *argument)
{
	struct thread_call *call = argument;

	call->result = intel_smm_invocation_adapter_provider_arm(&call->generation);
	return NULL;
}

static void *thread_retire(void *argument)
{
	struct thread_call *call = argument;

	call->result = intel_smm_invocation_adapter_provider_retire(call->generation);
	return NULL;
}

static void wait_for_thread_hook(void)
{
	while (!atomic_load_explicit(&threaded_hook_entered,
		memory_order_acquire))
		;
}

static void release_thread_hook(pthread_t thread)
{
	atomic_store_explicit(&threaded_hook_release, 1U, memory_order_release);
	assert(pthread_join(thread, NULL) == 0);
	threaded_hook = false;
	hook_point = 0;
}

static void threaded_ownership_schedules(void)
{
	const struct smm_invocation_save_state_ops *unchanged =
		(const void *)0xa5a5U;
	struct thread_call owner = { .ops = NULL };
	struct thread_call arm_owner = { .generation = UINT64_MAX };
	struct thread_call retire_owner;
	pthread_t thread;
	uint64_t generation = UINT64_MAX;

	reset_provider();
	threaded_hook = true;
	hook_point = 1;
	assert(pthread_create(&thread, NULL, thread_provision, &owner) == 0);
	wait_for_thread_hook();
	assert(intel_smm_invocation_adapter_provider_provision(&unchanged) ==
		SMM_INVOCATION_TRY_RETRY);
	assert(unchanged == (const void *)0xa5a5U);
	release_thread_hook(thread);
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS && owner.ops != NULL);

	atomic_store(&threaded_hook_entered, 0U);
	atomic_store(&threaded_hook_release, 0U);
	threaded_hook = true;
	hook_point = 4;
	owner.ops = (const void *)0xa5a5U;
	assert(pthread_create(&thread, NULL, thread_provision, &owner) == 0);
	wait_for_thread_hook();
	assert(intel_smm_invocation_adapter_provider_arm(&generation) ==
		SMM_INVOCATION_TRY_RETRY && generation == UINT64_MAX);
	release_thread_hook(thread);
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS &&
		owner.ops == &provider.ops);

	atomic_store(&threaded_hook_entered, 0U);
	atomic_store(&threaded_hook_release, 0U);
	threaded_hook = true;
	hook_point = 5;
	owner.ops = (const void *)0xa5a5U;
	assert(pthread_create(&thread, NULL, thread_provision, &owner) == 0);
	wait_for_thread_hook();
	assert(owner.ops == &provider.ops);
	assert(intel_smm_invocation_adapter_provider_arm(&generation) ==
		SMM_INVOCATION_TRY_RETRY && generation == UINT64_MAX);
	release_thread_hook(thread);
	assert(owner.result == SMM_INVOCATION_TRY_SUCCESS);

	atomic_store(&threaded_hook_entered, 0U);
	atomic_store(&threaded_hook_release, 0U);
	threaded_hook = true;
	hook_point = 2;
	assert(pthread_create(&thread, NULL, thread_arm, &arm_owner) == 0);
	wait_for_thread_hook();
	assert(intel_smm_invocation_adapter_provider_arm(&generation) ==
		SMM_INVOCATION_TRY_RETRY && generation == UINT64_MAX);
	release_thread_hook(thread);
	assert(arm_owner.result == SMM_INVOCATION_TRY_SUCCESS);

	atomic_store(&threaded_hook_entered, 0U);
	atomic_store(&threaded_hook_release, 0U);
	threaded_hook = true;
	hook_point = 3;
	retire_owner.generation = arm_owner.generation;
	assert(pthread_create(&thread, NULL, thread_retire, &retire_owner) == 0);
	wait_for_thread_hook();
	assert(intel_smm_invocation_adapter_provider_retire(
		arm_owner.generation) == SMM_INVOCATION_TRY_RETRY);
	release_thread_hook(thread);
	assert(retire_owner.result == SMM_INVOCATION_TRY_SUCCESS);
}

static void stale_adapter_revision_is_rejected(void)
{
	em64t101_smm_state_save_area_t state = { 0 };
	struct intel_smm_invocation_adapter adapter;
	struct intel_smm_invocation_adapter snapshot;
	struct smm_invocation_save_state_ops ops;
	const uintptr_t top = (uintptr_t)&state + sizeof(state);

	state.smm_revision = REV101;
	assert(intel_smm_invocation_adapter_init(&adapter, 1, &top,
		sizeof(state), REV101) == CB_SUCCESS);
	adapter.revision = 2U;
	memcpy(&snapshot, &adapter, sizeof(snapshot));
	assert(intel_smm_invocation_adapter_bind(&adapter, &ops) == CB_ERR);
	assert(intel_smm_invocation_adapter_begin(&adapter) == CB_ERR);
	assert(intel_smm_invocation_adapter_end(&adapter) == CB_ERR);
	assert(!intel_smm_invocation_adapter_range_disjoint(&adapter, &ops,
		sizeof(ops)));
	assert(!memcmp(&snapshot, &adapter, sizeof(snapshot)));
}

static void fixed_service_native_tuple(void)
{
	const uint32_t revisions[] = { REV100, REV101 };

	for (size_t layout = 0; layout < ARRAY_SIZE(revisions); layout++) {
		for (unsigned int fault = 0; fault < 6; fault++) {
			const struct smm_invocation_save_state_ops *ops;
			uint64_t generation, value = UINT64_MAX;
			uint64_t rax = fault == 1 ? (1ULL << 32) | 0xfcU :
				fault == 5 ? 0xfdU : 0xfcU;
			uint64_t rcx = fault == 2 ? 1 : fault == 3 ? 1ULL << 32 : 0;
			uint32_t io_misc = ((fault == 4 ? 0xb3U : 0xb2U) << 16) | 3U;

			reset_provider();
			ops = provision(revisions[layout]);
			if (revisions[layout] == REV100) {
				states100[0].smm_revision = REV100;
				states100[0].rax = rax;
				states100[0].rcx = rcx;
				states100[0].io_misc_info = io_misc;
			} else {
				states[0].smm_revision = REV101;
				states[0].rax = rax;
				states[0].rcx = rcx;
				states[0].io_misc_info = io_misc;
			}
			assert(intel_smm_invocation_adapter_provider_arm(&generation) ==
				SMM_INVOCATION_TRY_SUCCESS);
			assert(ops->match_apmc_write(ops->context, 0, 0xfcU) ==
				(fault ? SMM_INVOCATION_MATCH_ERROR : SMM_INVOCATION_MATCHED));
			if (!fault) {
				const struct intel_smm_invocation_adapter captured = provider.adapter;

				assert(ops->read_value(ops->context, 0, &value) == CB_SUCCESS);
				assert(value == 0xfcU);
				assert(ops->match_apmc_write(ops->context, 0, 0xfcU) ==
					SMM_INVOCATION_MATCHED);
				assert(!memcmp(&captured, &provider.adapter, sizeof(captured)));
			} else {
				assert(ops->read_value(ops->context, 0, &value) == CB_ERR);
				assert(value == UINT64_MAX);
			}
			assert(intel_smm_invocation_adapter_provider_retire(generation) ==
				SMM_INVOCATION_TRY_SUCCESS);
			assert(ops->match_apmc_write(ops->context, 0, 0xfcU) ==
				SMM_INVOCATION_MATCH_ERROR);
		}
	}
}

int main(void)
{
	two_independent_rounds();
	canonical_revision_guards();
	invalid_and_conflicting_provision();
	failure_poison_and_scrub();
	invalid_runtime_counts_poison();
	descriptor_corruption_poison();
	contention_and_nonretention();
	stale_wrong_and_exhaustion_poison();
	stale_prior_round_poison();
	output_aliases_are_rejected();
	threaded_ownership_schedules();
	stale_adapter_revision_is_rejected();
	fixed_service_native_tuple();
	return 0;
}
