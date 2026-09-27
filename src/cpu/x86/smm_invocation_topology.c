/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_topology.h>
#include <string.h>

#if defined(__TEST__)
void smm_invocation_topology_test_hook(uint32_t point);
#define TOPOLOGY_TEST_HOOK(point) smm_invocation_topology_test_hook(point)
#else
#define TOPOLOGY_TEST_HOOK(point) do { } while (0)
#endif

static void scrub_bytes(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

void smm_invocation_topology_scrub(
	struct smm_invocation_topology *topology)
{
	if (!topology)
		return;

	__atomic_store_n(&topology->state, SMM_INVOCATION_TOPOLOGY_TAKING,
		__ATOMIC_RELEASE);
	scrub_bytes((uint8_t *)topology + sizeof(topology->state),
		sizeof(*topology) - sizeof(topology->state));
	__atomic_store_n(&topology->state, SMM_INVOCATION_TOPOLOGY_EMPTY,
		__ATOMIC_RELEASE);
}

int smm_invocation_topology_loader_result(
	struct smm_invocation_topology *topology, int result)
{
	if (result)
		smm_invocation_topology_scrub(topology);
	return result;
}

enum cb_err smm_invocation_topology_begin(
	struct smm_invocation_topology_builder *builder,
	struct smm_invocation_topology *topology, uint32_t active_cpus,
	uint32_t bsp_apic_id)
{
	if (builder)
		memset(builder, 0, sizeof(*builder));
	if (!builder || !topology || !active_cpus ||
	    active_cpus > SMM_INVOCATION_TOPOLOGY_MAX_CPUS) {
		if (topology)
			smm_invocation_topology_scrub(topology);
		return CB_ERR;
	}

	smm_invocation_topology_scrub(topology);
	__atomic_store_n(&topology->state, SMM_INVOCATION_TOPOLOGY_TAKING,
		__ATOMIC_RELEASE);
	topology->revision = SMM_INVOCATION_TOPOLOGY_REVISION;
	topology->size = sizeof(*topology);
	topology->active_cpus = active_cpus;
	topology->bsp_cpu = UINT32_MAX;
	*builder = (struct smm_invocation_topology_builder) {
		.topology = topology,
		.active_cpus = active_cpus,
		.bsp_apic_id = bsp_apic_id,
	};
	return CB_SUCCESS;
}

enum cb_err smm_invocation_topology_append(
	struct smm_invocation_topology_builder *builder,
	uint32_t *installed_apic_id, uint32_t initial_apic_id)
{
	struct smm_invocation_topology *topology;

	if (!builder || !installed_apic_id || !builder->topology ||
	    builder->next_cpu >= builder->active_cpus) {
		if (builder && builder->topology)
			smm_invocation_topology_scrub(builder->topology);
		return CB_ERR;
	}
	topology = builder->topology;
	if (__atomic_load_n(&topology->state, __ATOMIC_ACQUIRE) !=
	    SMM_INVOCATION_TOPOLOGY_TAKING) {
		smm_invocation_topology_scrub(topology);
		return CB_ERR;
	}
	for (uint32_t cpu = 0; cpu < builder->next_cpu; cpu++)
		if (topology->initial_apic_ids[cpu] == initial_apic_id) {
			smm_invocation_topology_scrub(topology);
			return CB_ERR;
		}

	*installed_apic_id = initial_apic_id;
	topology->initial_apic_ids[builder->next_cpu] = initial_apic_id;
	if (initial_apic_id == builder->bsp_apic_id) {
		topology->bsp_cpu = builder->next_cpu;
		builder->bsp_matches++;
	}
	builder->next_cpu++;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_topology_publish(
	struct smm_invocation_topology_builder *builder,
	const uint32_t *installed_apic_ids, uint32_t installed_cpus,
	uint32_t runtime_cpus)
{
	struct smm_invocation_topology *topology;

	if (!builder || !installed_apic_ids || !builder->topology) {
		if (builder && builder->topology)
			smm_invocation_topology_scrub(builder->topology);
		return CB_ERR;
	}
	topology = builder->topology;
	TOPOLOGY_TEST_HOOK(1);
	if (__atomic_load_n(&topology->state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_TOPOLOGY_TAKING ||
	    topology->revision != SMM_INVOCATION_TOPOLOGY_REVISION ||
	    topology->size != sizeof(*topology) ||
	    topology->active_cpus != builder->active_cpus ||
	    topology->bsp_cpu != 0U ||
	    builder->next_cpu != builder->active_cpus ||
	    installed_cpus != builder->active_cpus ||
	    runtime_cpus != builder->active_cpus ||
	    builder->bsp_matches != 1U ||
	    topology->reserved[0] || topology->reserved[1] ||
	    topology->reserved[2] ||
	    memcmp(topology->initial_apic_ids, installed_apic_ids,
		builder->active_cpus * sizeof(*installed_apic_ids))) {
		smm_invocation_topology_scrub(topology);
		return CB_ERR;
	}

	TOPOLOGY_TEST_HOOK(2);
	__atomic_store_n(&topology->state, SMM_INVOCATION_TOPOLOGY_READY,
		__ATOMIC_RELEASE);
	return CB_SUCCESS;
}
