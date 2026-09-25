/* SPDX-License-Identifier: GPL-2.0-only */

#define _GNU_SOURCE

#include <assert.h>
#include <boot/payload_mm_authvar_mor_clear_x86.h>
#include <boot/payload_mm_authvar_mor_linear.h>
#include <boot/payload_mm_authvar_mor_live_inventory.h>
#include <boot/payload_mm_authvar_mor_private_smi.h>
#include <boot/payload_mm_authvar_smm_loader.h>
#include <bootmem.h>
#include <cpu/x86/pae.h>
#include <pthread.h>
#include <string.h>

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static unsigned int random_calls;
static unsigned int random_fail_at;
static unsigned int guard_calls;
static unsigned int register_calls;
static unsigned int query_calls;
static unsigned int query_fail_at;
static unsigned int snapshot_calls;
static unsigned int compose_calls;
static unsigned int prepare_calls;
static unsigned int send_calls;
static unsigned int close_calls;
static enum cb_err compose_status;
static enum cb_err close_status;
static bool required_during_registration;
static void *compose_workspace;
static pthread_barrier_t seed_claimed;
static pthread_barrier_t seed_release;
static bool seed_hook_active;
static bool seed_hook_channel;

static bool all_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	while (size--)
		combined |= *bytes++;
	return !combined;
}

static bool overlaps(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	return first_base < second_base + second_size &&
		second_base < first_base + first_size;
}

static void barrier_wait(pthread_barrier_t *barrier)
{
	const int status = pthread_barrier_wait(barrier);

	assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
}

void q35_mor_seed_claimed_test_hook(bool channel)
{
	if (!seed_hook_active || channel != seed_hook_channel)
		return;
	barrier_wait(&seed_claimed);
	barrier_wait(&seed_release);
}

enum cb_err get_random_number_64(uint64_t *value)
{
	random_calls++;
	if (random_fail_at == random_calls)
		return CB_ERR;
	*value = 0x1020304050607000ULL + random_calls;
	return CB_SUCCESS;
}

bool q35_mor_dma_pre_device_guard_valid(void)
{
	guard_calls++;
	return true;
}

int bootmem_aligned_reservations_register(
	const struct bootmem_aligned_reservation_request *request,
	size_t count, struct bootmem_aligned_reservation_handle *handle)
{
	register_calls++;
	required_during_registration =
		platform_payload_mm_authvar_smm_arena_required() ||
		platform_payload_mm_authvar_mor_private_smi_required();
	assert(count == 3U);
	assert(request[0].bytes == PAE_PGTL_SIZE &&
		request[0].alignment == PAE_PGTL_ALIGN &&
		request[0].tag == BM_MEM_TABLE);
	assert(request[1].bytes == PAE_VMEM_SIZE &&
		request[1].alignment == PAE_VMEM_ALIGN &&
		request[1].tag == BM_MEM_RESERVED);
	assert(request[2].bytes == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE &&
		request[2].alignment == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE &&
		request[2].tag == BM_MEM_TABLE);
	for (size_t index = 0; index < count; index++) {
		handle[index].opaque[0] = (uint32_t)index + 1U;
		handle[index].opaque[1] = 0xfeed0000U + (uint32_t)index;
	}
	return 0;
}

int bootmem_aligned_reservation_query(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_aligned_reservation *reservation)
{
	static const uint64_t base[] = { 0x100000, 0x200000, 0x400000 };
	static const uint64_t size[] = {
		PAE_PGTL_SIZE, PAE_VMEM_SIZE,
		PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE,
	};
	const size_t index = handle->opaque[0] - 1U;

	query_calls++;
	if (query_fail_at == query_calls)
		return -1;
	assert(index < ARRAY_SIZE(base));
	*reservation = (struct bootmem_aligned_reservation) {
		.base = base[index],
		.size = size[index],
		.tag = index == 1U ? BM_MEM_RESERVED : BM_MEM_TABLE,
	};
	return 0;
}

enum cb_err q35_mor_dma_snapshot(
	struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot)
{
	snapshot_calls++;
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->generation = 99U;
	memset(snapshot->identity, 0xa5, sizeof(snapshot->identity));
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_live_inventory_compose_owned(
	const struct payload_mm_authvar_mor_live_inventory_request *request,
	struct payload_mm_authvar_mor_clear_plan *plan,
	struct payload_mm_authvar_mor_live_inventory_workspace *workspace)
{
	compose_calls++;
	assert(request && plan && workspace);
	assert(!((uintptr_t)workspace % _Alignof(*workspace)));
	assert(!overlaps(request, sizeof(*request), workspace, sizeof(*workspace)));
	assert(!overlaps(plan, sizeof(*plan), workspace, sizeof(*workspace)));
	compose_workspace = workspace;
	memset(workspace, 0x5a, sizeof(*workspace));
	if (compose_status != CB_SUCCESS) {
		memset(workspace, 0, sizeof(*workspace));
		return compose_status;
	}
	memset(plan, 0, sizeof(*plan));
	plan->revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	plan->size = sizeof(*plan);
	plan->inventory_generation = request->generation;
	memcpy(plan->inventory_identity, request->identity,
		sizeof(plan->inventory_identity));
	plan->span_count = request->overlay_count;
	for (size_t index = 0; index < request->overlay_count; index++) {
		plan->spans[index].base = request->overlays[index].base;
		plan->spans[index].size = request->overlays[index].size;
		plan->spans[index].span_class =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED;
		plan->spans[index].exclusion_reason =
			request->overlays[index].exclusion_reason;
	}
	/* Model bootmem's excluded ramstage span containing static provider state. */
	plan->spans[0] = (struct payload_mm_authvar_mor_grant_span) {
		.base = 1U,
		.size = UINT64_MAX - 1U,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	memset(workspace, 0, sizeof(*workspace));
	return CB_SUCCESS;
}

static enum cb_err dummy(void)
{
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_clear_x86_prepare(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	void *page_tables, void *aperture,
	struct payload_mm_authvar_mor_clear_x86_backend *backend,
	struct payload_mm_authvar_mor_clear_executor_ops *ops)
{
	prepare_calls++;
	assert(plan->span_count == 3U);
	assert((uintptr_t)page_tables == 0x100000U);
	assert((uintptr_t)aperture == 0x200000U);
	memset(backend, 0, sizeof(*backend));
	memset(ops, 0, sizeof(*ops));
	ops->context = backend;
	ops->context_size = sizeof(*backend);
	ops->window_bytes = PAE_VMEM_SIZE;
	ops->map_window = (void *)dummy;
	ops->cache_writeback_invalidate = (void *)dummy;
	ops->fence = (void *)dummy;
	ops->unmap_window = (void *)dummy;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_private_smi_send_install(
	const struct payload_mm_authvar_mor_grant *grant)
{
	assert(grant);
	send_calls++;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_private_smi_close_unused(void)
{
	close_calls++;
	return close_status;
}

#include "../../src/mainboard/emulation/qemu-q35/mor_platform.c"

static void reset(void)
{
	memset(&platform, 0, sizeof(platform));
	memset(&scratch, 0, sizeof(scratch));
	random_calls = 0;
	random_fail_at = 0;
	guard_calls = 0;
	register_calls = 0;
	query_calls = 0;
	query_fail_at = 0;
	snapshot_calls = 0;
	compose_calls = 0;
	prepare_calls = 0;
	send_calls = 0;
	close_calls = 0;
	compose_status = CB_SUCCESS;
	close_status = CB_SUCCESS;
	required_during_registration = false;
	compose_workspace = NULL;
}

static void classify(struct payload_mm_authvar_mor_linear_ops *ops)
{
	struct payload_mm_authvar_mor_linear_boot boot = { 0 };

	assert(platform_payload_mm_authvar_mor_linear_ops(ops));
	assert(ops->classify_guard(ops->context, &boot) == CB_SUCCESS);
	assert(boot.kind == PAYLOAD_MM_AUTHVAR_MOR_LINEAR_COLD_BOOT);
	assert(boot.generation && guard_calls == 1U && random_calls == 9U);
}

static void request(struct payload_mm_authvar_mor_linear_ops *ops)
{
	classify(ops);
	assert(!platform_payload_mm_authvar_smm_arena_required());
	assert(!platform_payload_mm_authvar_mor_private_smi_required());
	assert(ops->reservations_register(ops->context) == CB_SUCCESS);
	assert(register_calls == 1U && !required_during_registration);
	assert(platform_payload_mm_authvar_smm_arena_required());
	assert(platform_payload_mm_authvar_mor_private_smi_required());
}

static void test_no_request(void)
{
	struct payload_mm_authvar_mor_linear_ops ops;

	reset();
	classify(&ops);
	assert(ops.private_close(ops.context) == CB_SUCCESS);
	assert(!register_calls && !close_calls);
	assert(all_zero(platform.capability, sizeof(platform.capability)));
	assert(all_zero(platform.receipt_secret, sizeof(platform.receipt_secret)));
}

static void test_classify_one_shot(void)
{
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_linear_boot duplicate;
	struct payload_mm_authvar_mor_linear_boot original;

	reset();
	classify(&ops);
	memset(&duplicate, 0xa5, sizeof(duplicate));
	memcpy(&original, &duplicate, sizeof(original));
	assert(ops.classify_guard(ops.context, &duplicate) == CB_ERR);
	assert(!memcmp(&duplicate, &original, sizeof(duplicate)));
	assert(guard_calls == 1U);
	assert(all_zero(platform.capability, sizeof(platform.capability)));
	assert(all_zero(platform.receipt_secret, sizeof(platform.receipt_secret)));
}

static void test_request(void)
{
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_smm_arena_seed arena = { 0 };
	struct payload_mm_authvar_mor_private_smi_seed channel = { 0 };
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct payload_mm_authvar_mor_clear_executor_ops executor = { 0 };
	struct payload_mm_authvar_mor_grant grant = { 0 };

	reset();
	request(&ops);
	assert(platform_payload_mm_authvar_smm_arena_seed(&arena));
	assert(platform_payload_mm_authvar_mor_private_smi_seed(&channel));
	assert(channel.page_handle.opaque[0] == 3U);
	assert(channel.cold_boot_generation == arena.cold_boot_generation);
	assert(all_zero(platform.capability, sizeof(platform.capability)));
	assert(all_zero(platform.receipt_secret, sizeof(platform.receipt_secret)));
	assert(ops.resolve_binding(ops.context, arena.cold_boot_generation,
		&plan, &executor) == CB_SUCCESS);
	assert(query_calls == 3U && snapshot_calls == 1U &&
		compose_calls == 1U && prepare_calls == 1U);
	assert(compose_workspace && all_zero(compose_workspace,
		sizeof(struct payload_mm_authvar_mor_live_inventory_workspace)));
	assert(!overlaps(compose_workspace,
		sizeof(struct payload_mm_authvar_mor_live_inventory_workspace),
		ops.context, ops.context_size));
	assert(executor.context == &platform.backend && executor.dma_snapshot &&
		executor.inventory_validate);
	assert(ops.private_complete(ops.context, &grant) == CB_SUCCESS);
	assert(send_calls == 1U && !close_calls);
}

static void test_faults_and_abort(void)
{
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_clear_executor_ops executor;
	uint8_t plan_original[sizeof(plan)];
	uint8_t executor_original[sizeof(executor)];

	reset();
	random_fail_at = 4U;
	assert(platform_payload_mm_authvar_mor_linear_ops(&ops));
	assert(ops.classify_guard(ops.context,
		&(struct payload_mm_authvar_mor_linear_boot){ 0 }) == CB_ERR);
	assert(all_zero(platform.capability, sizeof(platform.capability)));
	assert(all_zero(platform.receipt_secret, sizeof(platform.receipt_secret)));

	reset();
	request(&ops);
	memset(&plan, 0xa5, sizeof(plan));
	memset(&executor, 0x3c, sizeof(executor));
	memcpy(plan_original, &plan, sizeof(plan));
	memcpy(executor_original, &executor, sizeof(executor));
	compose_status = CB_ERR;
	assert(ops.resolve_binding(ops.context, platform.generation, &plan,
		&executor) == CB_ERR);
	assert(!memcmp(&plan, plan_original, sizeof(plan)));
	assert(!memcmp(&executor, executor_original, sizeof(executor)));
	assert(compose_workspace && all_zero(compose_workspace,
		sizeof(struct payload_mm_authvar_mor_live_inventory_workspace)));

	reset();
	request(&ops);
	platform_payload_mm_authvar_smm_arena_abort();
	assert(all_zero(platform.capability, sizeof(platform.capability)));
	assert(all_zero(platform.receipt_secret, sizeof(platform.receipt_secret)));
	assert(!platform_payload_mm_authvar_smm_arena_seed(
		&(struct payload_mm_authvar_smm_arena_seed){ 0 }));
	assert(!platform_payload_mm_authvar_mor_private_smi_seed(
		&(struct payload_mm_authvar_mor_private_smi_seed){ 0 }));
}

static void test_terminal_poison_closes_provisioned_channel_once(void)
{
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_smm_arena_seed arena = { 0 };
	struct payload_mm_authvar_mor_private_smi_seed channel = { 0 };
	struct payload_mm_authvar_mor_clear_plan plan = { 0 };
	struct payload_mm_authvar_mor_clear_executor_ops executor = { 0 };

	reset();
	request(&ops);
	assert(platform_payload_mm_authvar_smm_arena_seed(&arena));
	assert(platform_payload_mm_authvar_mor_private_smi_seed(&channel));
	assert(ops.resolve_binding(ops.context, arena.cold_boot_generation,
		&plan, &executor) == CB_SUCCESS);
	assert(executor.inventory_validate && close_calls == 0U &&
		platform.close_claimed == 0U);

	/* Model a competing callback owning scratch at inventory validation. */
	__atomic_store_n(&scratch.owner, Q35_MOR_SCRATCH_RESOLVE,
		__ATOMIC_RELEASE);
	assert(executor.inventory_validate(executor.inventory_context, &plan) ==
		CB_ERR);
	assert(__atomic_load_n(&scratch.owner, __ATOMIC_ACQUIRE) ==
		Q35_MOR_SCRATCH_POISONED);
	assert(platform.phase == Q35_MOR_TERMINAL &&
		platform.close_claimed == 0U && close_calls == 0U);

	/* The generic failure path owns one explicit close attempt. */
	assert(ops.private_close(ops.context) == CB_SUCCESS);
	assert(platform.close_claimed == 1U && close_calls == 1U);
	assert(ops.private_close(ops.context) == CB_ERR);
	assert(close_calls == 1U);
}

static void test_seed_ordering(void)
{
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_private_smi_seed channel;
	struct payload_mm_authvar_smm_arena_seed arena;
	uint8_t channel_original[sizeof(channel)];
	uint8_t arena_original[sizeof(arena)];

	reset();
	request(&ops);
	memset(&channel, 0xa5, sizeof(channel));
	memset(&arena, 0x3c, sizeof(arena));
	memcpy(channel_original, &channel, sizeof(channel));
	memcpy(arena_original, &arena, sizeof(arena));
	assert(!platform_payload_mm_authvar_mor_private_smi_seed(&channel));
	assert(!memcmp(&channel, channel_original, sizeof(channel)));
	/* An out-of-order seed poisons the request instead of permitting retry. */
	assert(!platform_payload_mm_authvar_smm_arena_seed(&arena));
	assert(!memcmp(&arena, arena_original, sizeof(arena)));
	assert(all_zero(platform.capability, sizeof(platform.capability)));
	assert(all_zero(platform.receipt_secret, sizeof(platform.receipt_secret)));
}

struct seed_thread {
	bool channel;
	bool result;
	union {
		struct payload_mm_authvar_smm_arena_seed arena;
		struct payload_mm_authvar_mor_private_smi_seed channel;
	} output;
};

static void *run_seed(void *argument)
{
	struct seed_thread *thread = argument;

	thread->result = thread->channel ?
		platform_payload_mm_authvar_mor_private_smi_seed(
			&thread->output.channel) :
		platform_payload_mm_authvar_smm_arena_seed(&thread->output.arena);
	return NULL;
}

static void test_seed_races(void)
{
	struct payload_mm_authvar_mor_linear_ops ops;
	struct seed_thread owner;
	struct seed_thread loser;
	pthread_t thread;

	for (unsigned int channel = 0; channel < 2U; channel++) {
		reset();
		request(&ops);
		if (channel)
			assert(platform_payload_mm_authvar_smm_arena_seed(
				&(struct payload_mm_authvar_smm_arena_seed){ 0 }));
		memset(&owner, 0xa5, sizeof(owner));
		memset(&loser, 0x3c, sizeof(loser));
		owner.channel = channel != 0U;
		loser.channel = channel != 0U;
		assert(!pthread_barrier_init(&seed_claimed, NULL, 2));
		assert(!pthread_barrier_init(&seed_release, NULL, 2));
		seed_hook_channel = channel != 0U;
		seed_hook_active = true;
		assert(!pthread_create(&thread, NULL, run_seed, &owner));
		barrier_wait(&seed_claimed);
		assert(!run_seed(&loser));
		barrier_wait(&seed_release);
		assert(!pthread_join(thread, NULL));
		seed_hook_active = false;
		assert(!owner.result && !loser.result);
		for (size_t index = 0; index < sizeof(owner.output); index++)
			assert(((const uint8_t *)&owner.output)[index] == 0xa5U);
		for (size_t index = 0; index < sizeof(loser.output); index++)
			assert(((const uint8_t *)&loser.output)[index] == 0x3cU);
		assert(!pthread_barrier_destroy(&seed_claimed));
		assert(!pthread_barrier_destroy(&seed_release));
	}
}

int main(void)
{
	test_no_request();
	test_classify_one_shot();
	test_request();
	test_faults_and_abort();
	test_terminal_poison_closes_provisioned_channel_once();
	test_seed_ordering();
	test_seed_races();
	return 0;
}
