/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cpu/x86/smm_invocation_topology.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static volatile uint32_t hook_wait;
static volatile uint32_t hook_seen;

void smm_invocation_topology_test_hook(uint32_t point)
{
	if (point != 2 || !__atomic_load_n(&hook_wait, __ATOMIC_ACQUIRE))
		return;
	__atomic_store_n(&hook_seen, 1, __ATOMIC_RELEASE);
	while (__atomic_load_n(&hook_wait, __ATOMIC_ACQUIRE))
		sched_yield();
}

static void assert_empty(const struct smm_invocation_topology *topology)
{
	struct smm_invocation_topology zero = { 0 };

	assert(!memcmp(topology, &zero, sizeof(zero)));
}

static void test_exact_map(void)
{
	struct smm_invocation_topology topology = { 0 };
	struct smm_invocation_topology_builder builder;
	uint32_t installed[3] = { 0 };
	const uint32_t ids[] = { 0x1ffffU, 7U, 0x80000400U };

	assert(smm_invocation_topology_begin(&builder, &topology, 3,
		ids[0]) == CB_SUCCESS);
	for (uint32_t cpu = 0; cpu < 3; cpu++)
		assert(smm_invocation_topology_append(&builder, &installed[cpu],
			ids[cpu]) == CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, installed, 3, 3) ==
		CB_SUCCESS);
	assert(__atomic_load_n(&topology.state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_TOPOLOGY_READY);
	assert(topology.revision == SMM_INVOCATION_TOPOLOGY_REVISION);
	assert(topology.size == sizeof(topology));
	assert(topology.active_cpus == 3);
	assert(topology.bsp_cpu == 0);
	assert(!memcmp(installed, ids, sizeof(ids)));
	assert(!memcmp(topology.initial_apic_ids, installed, sizeof(installed)));
	assert(!topology.reserved[0] && !topology.reserved[1] &&
		!topology.reserved[2]);

	smm_invocation_topology_scrub(&topology);
	assert_empty(&topology);

	assert(smm_invocation_topology_begin(&builder, &topology, 2, 5) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[0], 4) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[1], 5) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, installed, 2, 2) == CB_ERR);
	assert_empty(&topology);
}

static void test_bounds_and_identity(void)
{
	struct smm_invocation_topology topology;
	struct smm_invocation_topology_builder builder;
	uint32_t installed[2] = { 0 };

	memset(&topology, 0xa5, sizeof(topology));
	assert(smm_invocation_topology_begin(&builder, &topology, 0, 1) ==
		CB_ERR);
	assert_empty(&topology);
	memset(&topology, 0xa5, sizeof(topology));
	assert(smm_invocation_topology_begin(&builder, &topology,
		SMM_INVOCATION_TOPOLOGY_MAX_CPUS + 1U, 1) == CB_ERR);
	assert_empty(&topology);

	assert(smm_invocation_topology_begin(&builder, &topology, 2, 9) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[0], 9) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[1], 9) ==
		CB_ERR);
	assert_empty(&topology);

	assert(smm_invocation_topology_begin(&builder, &topology, 2, 9) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[0], 4) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[1], 4) ==
		CB_ERR);
	assert_empty(&topology);

	assert(smm_invocation_topology_begin(&builder, &topology, 2, 9) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[0], 4) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[1], 5) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, installed, 2, 2) == CB_ERR);
	assert_empty(&topology);

	assert(smm_invocation_topology_begin(&builder, &topology, 2, 4) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[0], 4) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[1], 5) ==
		CB_SUCCESS);
	installed[1] = 6;
	assert(smm_invocation_topology_publish(&builder, installed, 2, 2) == CB_ERR);
	assert_empty(&topology);
}

static void test_maximum_and_corruption(void)
{
	struct smm_invocation_topology topology = { 0 };
	struct smm_invocation_topology_builder builder;
	uint32_t installed[SMM_INVOCATION_TOPOLOGY_MAX_CPUS] = { 0 };

	assert(smm_invocation_topology_begin(&builder, &topology,
		SMM_INVOCATION_TOPOLOGY_MAX_CPUS, 0x10000) == CB_SUCCESS);
	for (uint32_t cpu = 0; cpu < SMM_INVOCATION_TOPOLOGY_MAX_CPUS; cpu++)
		assert(smm_invocation_topology_append(&builder, &installed[cpu],
			0x10000U + cpu) == CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, installed,
		SMM_INVOCATION_TOPOLOGY_MAX_CPUS,
		SMM_INVOCATION_TOPOLOGY_MAX_CPUS) == CB_SUCCESS);
	assert(topology.bsp_cpu == 0);

#define CORRUPT_AND_REJECT(member, value) do { \
	assert(smm_invocation_topology_begin(&builder, &topology, 1, 9) == \
		CB_SUCCESS); \
	assert(smm_invocation_topology_append(&builder, &installed[0], 9) == \
		CB_SUCCESS); \
	topology.member = (value); \
	assert(smm_invocation_topology_publish(&builder, installed, 1, 1) == CB_ERR); \
	assert_empty(&topology); \
} while (0)
	CORRUPT_AND_REJECT(revision, 2U);
	CORRUPT_AND_REJECT(size, 1U);
	CORRUPT_AND_REJECT(active_cpus, 2U);
	CORRUPT_AND_REJECT(bsp_cpu, 1U);
	CORRUPT_AND_REJECT(reserved[1], 1U);
	CORRUPT_AND_REJECT(state, SMM_INVOCATION_TOPOLOGY_READY);
#undef CORRUPT_AND_REJECT

	assert(smm_invocation_topology_begin(&builder, &topology, 1, 9) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[0], 9) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, installed, 0, 1) == CB_ERR);
	assert_empty(&topology);

	assert(smm_invocation_topology_begin(&builder, &topology, 1, 9) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[0], 9) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, installed, 1, 2) == CB_ERR);
	assert_empty(&topology);
}

struct publish_args {
	struct smm_invocation_topology_builder *builder;
	uint32_t *installed;
	enum cb_err result;
};

static void *publish_thread(void *argument)
{
	struct publish_args *args = argument;

	args->result = smm_invocation_topology_publish(args->builder,
		args->installed, 2, 2);
	return NULL;
}

static void test_release_publication(void)
{
	struct smm_invocation_topology topology = { 0 };
	struct smm_invocation_topology_builder builder;
	uint32_t installed[2] = { 0 };
	struct publish_args args = { .builder = &builder, .installed = installed };
	pthread_t thread;

	assert(smm_invocation_topology_begin(&builder, &topology, 2, 0x301) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[0], 0x301) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[1], 0x101) ==
		CB_SUCCESS);
	__atomic_store_n(&hook_wait, 1, __ATOMIC_RELEASE);
	__atomic_store_n(&hook_seen, 0, __ATOMIC_RELEASE);
	assert(!pthread_create(&thread, NULL, publish_thread, &args));
	while (!__atomic_load_n(&hook_seen, __ATOMIC_ACQUIRE))
		sched_yield();
	assert(__atomic_load_n(&topology.state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_TOPOLOGY_TAKING);
	assert(topology.initial_apic_ids[0] == installed[0]);
	assert(topology.initial_apic_ids[1] == installed[1]);
	__atomic_store_n(&hook_wait, 0, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(args.result == CB_SUCCESS);
	assert(__atomic_load_n(&topology.state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_TOPOLOGY_READY);
}

static void test_dirty_reload(void)
{
	struct smm_invocation_topology topology;
	struct smm_invocation_topology_builder builder;
	uint32_t installed[2] = { 0 };

	memset(&topology, 0xff, sizeof(topology));
	assert(smm_invocation_topology_begin(&builder, &topology, 2,
		0x10000) == CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[0],
		0x10000) == CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[1],
		0x20000) == CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, installed, 2, 2) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_begin(&builder, &topology, 1,
		0x30000) == CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed[0],
		0x30000) == CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, installed, 1, 1) ==
		CB_SUCCESS);
	assert(!topology.reserved[0] && !topology.reserved[1] &&
		!topology.reserved[2]);
	assert(topology.initial_apic_ids[0] == 0x30000);
	assert(topology.initial_apic_ids[1] == 0);
	smm_invocation_topology_scrub(&topology);
	assert_empty(&topology);
}

static void test_post_publication_loader_failure(void)
{
	struct smm_invocation_topology topology = { 0 };
	struct smm_invocation_topology_builder builder;
	uint32_t installed = 0;

	assert(smm_invocation_topology_begin(&builder, &topology, 1,
		0x12345) == CB_SUCCESS);
	assert(smm_invocation_topology_append(&builder, &installed,
		0x12345) == CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, &installed, 1, 1) ==
		CB_SUCCESS);
	assert(smm_invocation_topology_loader_result(&topology, 0) == 0);
	assert(__atomic_load_n(&topology.state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_TOPOLOGY_READY);
	assert(smm_invocation_topology_loader_result(&topology, -1) == -1);
	assert_empty(&topology);
}

int main(void)
{
	test_exact_map();
	test_bounds_and_identity();
	test_maximum_and_corruption();
	test_release_publication();
	test_dirty_reload();
	test_post_publication_loader_failure();
	return 0;
}
