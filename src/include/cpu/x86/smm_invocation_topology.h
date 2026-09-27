/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_X86_SMM_INVOCATION_TOPOLOGY_H
#define CPU_X86_SMM_INVOCATION_TOPOLOGY_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define SMM_INVOCATION_TOPOLOGY_REVISION 1U
#define SMM_INVOCATION_TOPOLOGY_MAX_CPUS 64U

enum smm_invocation_topology_state {
	SMM_INVOCATION_TOPOLOGY_EMPTY,
	SMM_INVOCATION_TOPOLOGY_TAKING,
	SMM_INVOCATION_TOPOLOGY_READY,
};

/* Loader-written POD. It contains no pointer or executable capability. */
struct smm_invocation_topology {
	uint32_t state;
	uint32_t revision;
	uint32_t size;
	uint32_t active_cpus;
	uint32_t bsp_cpu;
	uint32_t reserved[3];
	uint32_t initial_apic_ids[SMM_INVOCATION_TOPOLOGY_MAX_CPUS];
} __aligned(8);

struct smm_invocation_topology_builder {
	struct smm_invocation_topology *topology;
	uint32_t active_cpus;
	uint32_t bsp_apic_id;
	uint32_t bsp_matches;
	uint32_t next_cpu;
};

enum cb_err smm_invocation_topology_begin(
	struct smm_invocation_topology_builder *builder,
	struct smm_invocation_topology *topology, uint32_t active_cpus,
	uint32_t bsp_apic_id);
enum cb_err smm_invocation_topology_append(
	struct smm_invocation_topology_builder *builder,
	uint32_t *installed_apic_id, uint32_t initial_apic_id);
enum cb_err smm_invocation_topology_publish(
	struct smm_invocation_topology_builder *builder,
	const uint32_t *installed_apic_ids, uint32_t installed_cpus,
	uint32_t runtime_cpus);
enum cb_err smm_invocation_topology_read(
	const struct smm_invocation_topology *topology,
	struct smm_invocation_topology *snapshot);
void smm_invocation_topology_scrub(
	struct smm_invocation_topology *topology);
int smm_invocation_topology_loader_result(
	struct smm_invocation_topology *topology, int result);

#endif
