/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_smm_receipt_provision.h"
#include "dma_smm_authority.h"

#include <commonlib/helpers.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_invocation_loader_instance.h>
#include <cpu/x86/smm_invocation_topology.h>
#include <cpu/x86/smm.h>
#include <device/mmio.h>
#include <string.h>

#include "../../../../../lib/payload_mm_crypto/crypto.h"

#if !ENV_SMM && !ENV_TEST
#error "MTL DMA receipt receiver is SMM-only"
#endif

enum receipt_state { RECEIPT_EMPTY, RECEIPT_PROVISIONING, RECEIPT_READY,
	RECEIPT_FAILED };
enum epoch_state { EPOCH_EMPTY, EPOCH_CAPTURING, EPOCH_SEALED,
	EPOCH_ACTIVATING, EPOCH_ACTIVATION_PROOF, EPOCH_RETAINED,
	EPOCH_VERIFYING_ACTIVATION, EPOCH_VERIFYING_RETAINED, EPOCH_POISONED };

struct epoch_evidence_snapshot {
	uint32_t phase;
	uint32_t active_cpus;
	uint32_t bsp_cpu;
	uint32_t loader_lifecycle;
	uint32_t closed_lifecycle;
	uint32_t closed_eos_consumed;
	uint32_t arrival_failed;
	uint32_t rendezvous_fail_requested;
	uint32_t departure_failed;
	uint32_t rendezvous_ack_required;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	struct smm_invocation_loader_instance_nonce closed_loader_instance_nonce;
	uint64_t generation;
	uint64_t closed_generation;
	uint64_t expected_cpus;
	uint64_t arrived_cpus;
	uint64_t rendezvous_ack_cpus;
	uint64_t departed_cpus;
	uint32_t participant_apic_ids[SMM_INVOCATION_EVIDENCE_MAX_CPUS];
	struct smm_invocation_participant
		participants[SMM_INVOCATION_EVIDENCE_MAX_CPUS];
};

struct receipt_context {
	uint64_t identity;
	struct starbook_mtl_dma_smm_authority_workspace *authority;
};

static struct {
	uint32_t state;
	uint32_t reserved;
	struct receipt_context context;
	struct starbook_mtl_dma_receipt_dependencies dependencies;
	struct starbook_mtl_dma_smm_observer observer;
	struct starbook_mtl_dma_smm_receipt receipt;
	struct starbook_mtl_dma_smm_workspace workspace;
	struct starbook_mtl_dma_smm_authority_workspace authority;
	struct smm_dma_receipt_memory memory_snapshot;
	struct starbook_mtl_dma_receipt_frame snapshot;
	struct {
		uint32_t state;
		uint32_t reserved;
		const struct smm_invocation_loader_composition *composition;
		const struct smm_invocation_loader_instance *instance_identity;
		const struct smm_invocation_evidence *evidence_identity;
		const struct smm_invocation_topology *topology_identity;
		struct smm_invocation_loader_instance instance;
		struct smm_invocation_topology topology;
		struct starbook_mtl_dma_smm_receipt receipt;
		struct starbook_mtl_dma_smm_receipt sealed_receipt;
		struct starbook_mtl_dma_smm_epoch_range ranges[2];
		struct starbook_mtl_dma_smm_epoch_range sealed_ranges[2];
		uint8_t receipt_digest[STARBOOK_MTL_DMA_SMM_DIGEST_SIZE];
		uint8_t sealed_digest[STARBOOK_MTL_DMA_SMM_DIGEST_SIZE];
		struct starbook_mtl_dma_requester_binding live_binding;
		struct starbook_mtl_dma_requester_binding sealed_live_binding;
		struct epoch_evidence_snapshot predecessor;
		struct epoch_evidence_snapshot activation;
	} epoch;
} owner __aligned(8);

static void epoch_evidence_read(const struct smm_invocation_evidence *evidence,
	struct epoch_evidence_snapshot *snapshot)
{
	*snapshot = (struct epoch_evidence_snapshot) {
		.phase = smm_invocation_evidence_phase(evidence),
		.active_cpus = __atomic_load_n(&evidence->active_cpus,
			__ATOMIC_ACQUIRE),
		.bsp_cpu = __atomic_load_n(&evidence->bsp_cpu, __ATOMIC_ACQUIRE),
		.loader_lifecycle = __atomic_load_n(&evidence->loader_lifecycle,
			__ATOMIC_ACQUIRE),
		.closed_lifecycle = __atomic_load_n(&evidence->closed_lifecycle,
			__ATOMIC_ACQUIRE),
		.closed_eos_consumed = __atomic_load_n(
			&evidence->closed_eos_consumed, __ATOMIC_ACQUIRE),
		.arrival_failed = __atomic_load_n(&evidence->arrival_failed,
			__ATOMIC_ACQUIRE),
		.rendezvous_fail_requested = __atomic_load_n(
			&evidence->rendezvous_fail_requested, __ATOMIC_ACQUIRE),
		.departure_failed = __atomic_load_n(&evidence->departure_failed,
			__ATOMIC_ACQUIRE),
		.rendezvous_ack_required = __atomic_load_n(
			&evidence->rendezvous_ack_required, __ATOMIC_ACQUIRE),
		.loader_instance_nonce = {
			.low = __atomic_load_n(&evidence->loader_instance_nonce.low,
				__ATOMIC_ACQUIRE),
			.high = __atomic_load_n(&evidence->loader_instance_nonce.high,
				__ATOMIC_ACQUIRE),
		},
		.closed_loader_instance_nonce = {
			.low = __atomic_load_n(
				&evidence->closed_loader_instance_nonce.low,
				__ATOMIC_ACQUIRE),
			.high = __atomic_load_n(
				&evidence->closed_loader_instance_nonce.high,
				__ATOMIC_ACQUIRE),
		},
		.generation = __atomic_load_n(&evidence->generation,
			__ATOMIC_ACQUIRE),
		.closed_generation = __atomic_load_n(&evidence->closed_generation,
			__ATOMIC_ACQUIRE),
		.expected_cpus = __atomic_load_n(&evidence->expected_cpus,
			__ATOMIC_ACQUIRE),
		.arrived_cpus = __atomic_load_n(&evidence->arrived_cpus,
			__ATOMIC_ACQUIRE),
		.rendezvous_ack_cpus = __atomic_load_n(
			&evidence->rendezvous_ack_cpus, __ATOMIC_ACQUIRE),
		.departed_cpus = __atomic_load_n(&evidence->departed_cpus,
			__ATOMIC_ACQUIRE),
	};
	for (uint32_t cpu = 0; cpu < SMM_INVOCATION_EVIDENCE_MAX_CPUS; cpu++) {
		snapshot->participant_apic_ids[cpu] = __atomic_load_n(
			&evidence->participant_apic_ids[cpu], __ATOMIC_ACQUIRE);
		snapshot->participants[cpu] = (struct smm_invocation_participant) {
			.phase = __atomic_load_n(&evidence->participants[cpu].phase,
				__ATOMIC_ACQUIRE),
			.apic_id = __atomic_load_n(&evidence->participants[cpu].apic_id,
				__ATOMIC_ACQUIRE),
			.generation = __atomic_load_n(
				&evidence->participants[cpu].generation,
				__ATOMIC_ACQUIRE),
		};
	}
}

static bool epoch_evidence_stable(const struct smm_invocation_evidence *evidence,
	const struct epoch_evidence_snapshot *expected)
{
	struct epoch_evidence_snapshot after;

	epoch_evidence_read(evidence, &after);
	return !memcmp(&after, expected, sizeof(after));
}

static bool epoch_evidence_capture(
	const struct smm_invocation_evidence *evidence,
	struct epoch_evidence_snapshot *snapshot)
{
	if (!evidence || !snapshot)
		return false;
	epoch_evidence_read(evidence, snapshot);
	return true;
}

static bool epoch_evidence_valid(
	const struct epoch_evidence_snapshot *snapshot,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_topology *topology, uint64_t generation)
{
	uint64_t expected_cpus;

	if (!snapshot || !instance || !topology || !generation ||
	    !topology->active_cpus ||
	    topology->active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS)
		return false;
	expected_cpus = topology->active_cpus == 64U ? UINT64_MAX :
		(1ULL << topology->active_cpus) - 1U;
	return snapshot->phase == SMM_INVOCATION_READY &&
		snapshot->generation == generation &&
		snapshot->closed_generation == generation &&
		snapshot->closed_eos_consumed == 1U &&
		snapshot->active_cpus == topology->active_cpus &&
		snapshot->bsp_cpu == topology->bsp_cpu &&
		snapshot->expected_cpus == expected_cpus &&
		snapshot->loader_lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD &&
		snapshot->closed_lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD &&
		smm_invocation_loader_instance_nonce_equal(
			snapshot->loader_instance_nonce,
			instance->loader_instance_nonce) &&
		smm_invocation_loader_instance_nonce_equal(
			snapshot->closed_loader_instance_nonce,
			instance->loader_instance_nonce) &&
		!memcmp(snapshot->participant_apic_ids,
			topology->initial_apic_ids,
			sizeof(snapshot->participant_apic_ids));
}

static bool epoch_current_evidence_valid(
	const struct smm_invocation_evidence *evidence,
	const struct epoch_evidence_snapshot *snapshot,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_topology *topology,
	uint64_t activation_generation)
{
	uint64_t expected_cpus;

	if (!evidence || !snapshot || !instance || !topology ||
	    activation_generation == UINT64_MAX ||
	    snapshot->generation <= activation_generation ||
	    !topology->active_cpus ||
	    topology->active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS)
		return false;
	expected_cpus = topology->active_cpus == 64U ? UINT64_MAX :
		(1ULL << topology->active_cpus) - 1U;
	if (!((snapshot->phase == SMM_INVOCATION_COLLECTING) ||
	      (snapshot->phase >= SMM_INVOCATION_CLAIMING &&
	       snapshot->phase <= SMM_INVOCATION_CLOSING)) ||
	    snapshot->closed_generation || snapshot->closed_eos_consumed ||
	    !smm_invocation_loader_instance_nonce_is_zero(
		snapshot->closed_loader_instance_nonce) ||
	    snapshot->closed_lifecycle || snapshot->arrival_failed ||
	    snapshot->rendezvous_fail_requested || snapshot->departure_failed ||
	    snapshot->rendezvous_ack_required != 1U ||
	    snapshot->active_cpus != topology->active_cpus ||
	    snapshot->bsp_cpu != topology->bsp_cpu ||
	    snapshot->expected_cpus != expected_cpus ||
	    snapshot->arrived_cpus != expected_cpus ||
	    snapshot->rendezvous_ack_cpus != expected_cpus ||
	    snapshot->departed_cpus ||
	    snapshot->loader_lifecycle != SMM_INVOCATION_LOADER_S3_RELOAD ||
	    !smm_invocation_loader_instance_nonce_equal(
		snapshot->loader_instance_nonce,
		instance->loader_instance_nonce) ||
	    memcmp(snapshot->participant_apic_ids, topology->initial_apic_ids,
		sizeof(snapshot->participant_apic_ids)) ||
	    !smm_invocation_evidence_rendezvous_ack_ready(evidence,
		snapshot->generation))
		return false;
	for (uint32_t cpu = 0; cpu < topology->active_cpus; cpu++) {
		if (snapshot->participants[cpu].phase !=
			SMM_INVOCATION_PARTICIPANT_READY ||
		    snapshot->participants[cpu].generation != snapshot->generation ||
		    snapshot->participants[cpu].apic_id !=
			topology->initial_apic_ids[cpu])
			return false;
	}
	for (uint32_t cpu = topology->active_cpus;
	     cpu < SMM_INVOCATION_EVIDENCE_MAX_CPUS; cpu++) {
		if (snapshot->participants[cpu].phase !=
			SMM_INVOCATION_PARTICIPANT_EMPTY ||
		    snapshot->participants[cpu].generation ||
		    snapshot->participants[cpu].apic_id)
			return false;
	}
	return true;
}

static bool epoch_terminal_evidence_valid(
	const struct epoch_evidence_snapshot *snapshot,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_topology *topology,
	uint64_t activation_generation, uint64_t verified_generation)
{
	uint64_t expected_cpus;

	if (!snapshot || !instance || !topology ||
	    snapshot->generation < activation_generation ||
	    snapshot->generation < verified_generation ||
	    !epoch_evidence_valid(snapshot, instance, topology,
		snapshot->generation) || !topology->active_cpus ||
	    topology->active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS)
		return false;
	expected_cpus = topology->active_cpus == 64U ? UINT64_MAX :
		(1ULL << topology->active_cpus) - 1U;
	if (snapshot->expected_cpus != expected_cpus ||
	    snapshot->arrival_failed || snapshot->rendezvous_fail_requested ||
	    snapshot->departure_failed || snapshot->rendezvous_ack_required ||
	    snapshot->arrived_cpus || snapshot->rendezvous_ack_cpus ||
	    snapshot->departed_cpus)
		return false;
	for (uint32_t cpu = 0; cpu < SMM_INVOCATION_EVIDENCE_MAX_CPUS; cpu++) {
		if (snapshot->participants[cpu].phase !=
			SMM_INVOCATION_PARTICIPANT_EMPTY ||
		    snapshot->participants[cpu].generation ||
		    snapshot->participants[cpu].apic_id)
			return false;
	}
	return true;
}

static bool epoch_retained_evidence_valid(
	const struct smm_invocation_evidence *evidence,
	const struct epoch_evidence_snapshot *snapshot,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_topology *topology,
	uint64_t activation_generation, uint64_t verified_generation)
{
	if (snapshot->phase == SMM_INVOCATION_READY)
		return epoch_terminal_evidence_valid(snapshot, instance, topology,
			activation_generation, verified_generation);
	return epoch_current_evidence_valid(evidence, snapshot, instance,
		topology, activation_generation) &&
		snapshot->generation >= verified_generation;
}

static enum cb_err observer_verify_translation_epoch(void *context,
	const struct starbook_mtl_dma_smm_receipt *receipt)
{
	const struct receipt_context *receipt_context = context;

	if (!receipt_context ||
	    receipt_context->identity != 0x4d544c444d41524dULL ||
	    receipt_context->authority != &owner.authority ||
	    memcmp(&owner.epoch.live_binding,
		&owner.epoch.sealed_live_binding,
		sizeof(owner.epoch.live_binding)))
		return CB_ERR;
	return starbook_mtl_dma_smm_authority_verify_epoch(
		receipt_context->authority, receipt, &owner.epoch.live_binding);
}

static enum cb_err observer_verify_translation_live(void *context,
	const struct starbook_mtl_dma_smm_receipt *receipt)
{
	const struct receipt_context *receipt_context = context;

	if (!receipt_context ||
	    receipt_context->identity != 0x4d544c444d41524dULL ||
	    receipt_context->authority != &owner.authority ||
	    memcmp(&owner.epoch.live_binding,
		&owner.epoch.sealed_live_binding,
		sizeof(owner.epoch.live_binding)))
		return CB_ERR;
	return starbook_mtl_dma_smm_authority_verify_live_policy(
		receipt_context->authority, receipt, &owner.epoch.live_binding);
}

#if !ENV_TEST
static bool range_contains(const struct smm_dma_receipt_range *container,
	uint64_t base, size_t size)
{
	return base && size && base >= container->base &&
		base - container->base < container->size &&
		size <= container->size - (base - container->base);
}

static bool ordinary_dram_range(void *context, uint64_t base, size_t size)
{
	const struct smm_dma_receipt_memory *memory = context;

	return memory &&
		(range_contains(&memory->frame, base, size) ||
		 range_contains(&memory->dma, base, size) ||
		 range_contains(&memory->mirror, base, size));
}

enum cb_err starbook_mtl_dma_receipt_policy(
	const struct starbook_mtl_dma_receipt_dependencies **dependencies)
{
	static struct starbook_mtl_dma_receipt_dependencies policy;
	const struct smm_dma_receipt_memory *memory;

	if (!dependencies || !smm_get_dma_receipt_memory(&memory))
		return CB_ERR;
	policy = (struct starbook_mtl_dma_receipt_dependencies) {
		.revision = STARBOOK_MTL_DMA_RECEIPT_DEPENDENCIES_REVISION,
		.size = sizeof(policy),
		.context = (void *)memory,
		.context_size = sizeof(*memory),
		.ordinary_dram_range = ordinary_dram_range,
	};
	*dependencies = &policy;
	return CB_SUCCESS;
}
#endif

static enum cb_err observer_read32(void *context, uint64_t address,
	uint32_t *value)
{
	const struct receipt_context *receipt_context = context;

	if (!receipt_context ||
	    receipt_context->identity != 0x4d544c444d41524dULL || !value ||
	    address > (uintptr_t)-1 - sizeof(*value))
		return CB_ERR;
	*value = read32p((uintptr_t)address);
	return CB_SUCCESS;
}

static enum cb_err observer_sha256(void *context, const void *data,
	size_t size, uint8_t digest[STARBOOK_MTL_DMA_SMM_DIGEST_SIZE])
{
	const struct receipt_context *receipt_context = context;

	if (!receipt_context ||
	    receipt_context->identity != 0x4d544c444d41524dULL)
		return CB_ERR;
	return payload_mm_sha256(data, size, digest) == PAYLOAD_MM_VERIFY_OK ?
		CB_SUCCESS : CB_ERR;
}

static enum cb_err observer_verify_translation(void *context,
	const struct starbook_mtl_dma_smm_receipt *receipt)
{
	const struct receipt_context *receipt_context = context;

	if (!receipt_context ||
	    receipt_context->identity != 0x4d544c444d41524dULL ||
	    !receipt_context->authority)
		return CB_ERR;
	return starbook_mtl_dma_smm_authority_verify(
		receipt_context->authority, receipt);
}

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return !value;
}

static bool frame_valid(const struct starbook_mtl_dma_receipt_frame *frame)
{
	return frame && !((uintptr_t)frame % _Alignof(*frame)) &&
		(uintptr_t)frame <= (uintptr_t)-1 - (sizeof(*frame) - 1U) &&
		frame->revision == STARBOOK_MTL_DMA_RECEIPT_FRAME_REVISION &&
		frame->size == sizeof(*frame) &&
		frame->state == STARBOOK_MTL_DMA_RECEIPT_FRAME_REQUEST &&
		!frame->reserved && bytes_zero(frame->candidate.table_digest,
			sizeof(frame->candidate.table_digest)) &&
		smm_invocation_loader_instance_nonce_is_zero(
			frame->candidate.loader_instance_nonce) &&
		!frame->candidate.invocation_generation &&
		!frame->candidate.loader_lifecycle &&
		!frame->candidate.identity_reserved;
}

static bool dependencies_valid(
	const struct starbook_mtl_dma_receipt_dependencies *dependencies,
	const struct smm_invocation_runtime_view *runtime_view)
{
	return dependencies &&
		dependencies->revision ==
			STARBOOK_MTL_DMA_RECEIPT_DEPENDENCIES_REVISION &&
		dependencies->size == sizeof(*dependencies) &&
		dependencies->context && dependencies->context_size &&
		dependencies->ordinary_dram_range &&
		smm_invocation_runtime_range_is_protected(runtime_view, dependencies,
			sizeof(*dependencies)) == CB_SUCCESS &&
		smm_invocation_runtime_range_is_protected(runtime_view,
			dependencies->context, dependencies->context_size) == CB_SUCCESS &&
		smm_invocation_runtime_range_is_protected(runtime_view,
			(const void *)(uintptr_t)dependencies->ordinary_dram_range,
			1U) == CB_SUCCESS;
}

static bool receipt_ranges_allowed(
	const struct starbook_mtl_dma_smm_receipt *receipt)
{
	const struct starbook_mtl_dma_smm_range *const ranges[] = {
		&receipt->handoff,
		&receipt->tables,
		&receipt->table_mirror,
		&receipt->arenas[0],
		&receipt->arenas[1],
		&receipt->arenas[2],
	};

	for (size_t index = 0; index < ARRAY_SIZE(ranges); index++)
		if (ranges[index]->size > SIZE_MAX ||
		    !owner.dependencies.ordinary_dram_range(
			owner.dependencies.context, ranges[index]->base,
			(size_t)ranges[index]->size))
			return false;
	return true;
}

static bool receipt_layout_matches_memory(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	const struct smm_dma_receipt_memory *memory)
{
	const uint64_t page_size = 4096U;
	uint64_t next;
	uint64_t remaining_pages;

	if (!receipt || !memory || memory->dma.size % page_size ||
	    memory->mirror.size % page_size ||
	    receipt->handoff.base != memory->dma.base ||
	    receipt->handoff.size != page_size ||
	    receipt->tables.base != memory->dma.base + page_size ||
	    receipt->tables.size != memory->mirror.size ||
	    receipt->table_mirror.base != memory->mirror.base ||
	    receipt->table_mirror.size != memory->mirror.size ||
	    memory->dma.size < page_size + receipt->tables.size ||
	    (memory->dma.size - page_size - receipt->tables.size) % page_size)
		return false;
	next = receipt->tables.base + receipt->tables.size;
	remaining_pages = (memory->dma.size - page_size -
		receipt->tables.size) / page_size;
	for (size_t index = 0; index < STARBOOK_MTL_DMA_SMM_ARENAS; index++) {
		const uint64_t requesters = STARBOOK_MTL_DMA_SMM_ARENAS - index;
		const uint64_t pages = remaining_pages / requesters;

		if (!pages || receipt->arenas[index].base != next ||
		    receipt->arenas[index].size != pages * page_size)
			return false;
		next += pages * page_size;
		remaining_pages -= pages;
	}
	return !remaining_pages && next == memory->dma.base + memory->dma.size;
}

#if ENV_TEST
bool starbook_mtl_dma_receipt_layout_matches_memory_test(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	const struct smm_dma_receipt_memory *memory)
{
	return receipt_layout_matches_memory(receipt, memory);
}
#endif

#if !ENV_TEST
static bool receipt_memory_unchanged(
	const struct smm_dma_receipt_memory *expected,
	const struct smm_dma_receipt_memory *snapshot)
{
	const struct smm_dma_receipt_memory *current;

	return smm_get_dma_receipt_memory(&current) && current == expected &&
		!memcmp(current, snapshot, sizeof(*snapshot));
}
#endif

enum cb_err starbook_mtl_dma_receipt_provision_receive(
	const struct smm_invocation_save_state_ops *expected_active_ops)
{
	struct starbook_mtl_dma_receipt_frame *frame = NULL;
	const struct smm_invocation_runtime_view *runtime_view;
	struct smm_invocation_runtime_binding binding;
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_topology topology;
	const struct starbook_mtl_dma_receipt_dependencies *dependencies;
#if !ENV_TEST
	const struct smm_dma_receipt_memory *receipt_memory;
	struct smm_dma_receipt_memory memory_snapshot;
#endif
	uint64_t wire = 0;
	uintptr_t smram_base;
	size_t smram_size;
	uintptr_t frame_base;
	size_t frame_size;
	uint32_t matched_cpu = UINT32_MAX;
	uint32_t expected = RECEIPT_EMPTY;
	enum cb_err status = CB_ERR;

	if (!expected_active_ops || !expected_active_ops->match_apmc_write ||
	    !expected_active_ops->read_value || !expected_active_ops->write_value ||
	    !__atomic_compare_exchange_n(&owner.state, &expected,
		RECEIPT_PROVISIONING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return CB_ERR;
	if (smm_invocation_runtime_view_get(&runtime_view) != CB_SUCCESS ||
	    smm_invocation_runtime_binding_get(&binding) != CB_SUCCESS ||
	    smm_invocation_topology_read(binding.topology, &topology) != CB_SUCCESS ||
	    starbook_mtl_dma_receipt_policy(&dependencies) != CB_SUCCESS ||
	    !dependencies_valid(dependencies, runtime_view))
		goto out;
	owner.dependencies = *dependencies;
	if (memcmp(dependencies, &owner.dependencies, sizeof(owner.dependencies)) ||
	    !dependencies_valid(&owner.dependencies, runtime_view))
		goto out;
#if !ENV_TEST
	if (!smm_get_dma_receipt_memory(&receipt_memory))
		goto out;
	memory_snapshot = *receipt_memory;
	owner.memory_snapshot = memory_snapshot;
#endif
	for (uint32_t cpu = 0; cpu < topology.active_cpus; cpu++) {
		const enum smm_invocation_match match =
			expected_active_ops->match_apmc_write(
				expected_active_ops->context, cpu,
				SMM_APMC_STARBOOK_MTL_DMA_RECEIPT);

		if (match == SMM_INVOCATION_MATCH_ERROR ||
		    (match == SMM_INVOCATION_MATCHED && matched_cpu != UINT32_MAX))
			goto out;
		if (match == SMM_INVOCATION_MATCHED)
			matched_cpu = cpu;
	}
	if (matched_cpu != topology.bsp_cpu ||
	    expected_active_ops->read_value(expected_active_ops->context,
		matched_cpu, &wire) != CB_SUCCESS ||
	    (uint32_t)wire != STARBOOK_MTL_DMA_RECEIPT_WIRE_REQUEST)
		goto out;
	if (!smm_get_dma_receipt_frame(&frame_base, &frame_size) ||
	    frame_size != sizeof(*frame) || frame_base != (uint32_t)(wire >> 32) ||
	    !owner.dependencies.ordinary_dram_range(owner.dependencies.context,
		frame_base, frame_size))
		goto out;
	if (memcmp(dependencies, &owner.dependencies, sizeof(owner.dependencies)) ||
	    !dependencies_valid(&owner.dependencies, runtime_view))
		goto out;
	frame = (void *)frame_base;
	if (!frame_valid(frame) ||
	    smm_invocation_runtime_range_is_protected(runtime_view, frame,
		sizeof(*frame)) == CB_SUCCESS)
		goto out;
	owner.snapshot = *frame;
	if (!frame_valid(&owner.snapshot) ||
	    memcmp(frame, &owner.snapshot, sizeof(owner.snapshot)))
		goto out;
	owner.context = (struct receipt_context) {
		.identity = 0x4d544c444d41524dULL,
		.authority = &owner.authority,
	};
	owner.observer = (struct starbook_mtl_dma_smm_observer) {
		.context = &owner.context,
		.context_size = sizeof(owner.context),
		.read32 = observer_read32,
		.sha256 = observer_sha256,
		.verify_translation = observer_verify_translation,
	};
	owner.receipt = owner.snapshot.candidate;
	if (smm_invocation_loader_instance_read(binding.instance, &instance) !=
		CB_SUCCESS || !binding.evidence->generation ||
	    !smm_invocation_loader_instance_nonce_equal(
		instance.loader_instance_nonce,
		binding.evidence->loader_instance_nonce) ||
	    instance.lifecycle != binding.evidence->loader_lifecycle)
		goto out;
	owner.receipt.loader_instance_nonce = instance.loader_instance_nonce;
	owner.receipt.invocation_generation = binding.evidence->generation;
	owner.receipt.loader_lifecycle = instance.lifecycle;
	smm_region(&smram_base, &smram_size);
	if (!starbook_mtl_dma_smm_receipt_geometry_valid(&owner.receipt,
		smram_base, smram_size) ||
#if !ENV_TEST
	    !receipt_layout_matches_memory(&owner.receipt, &memory_snapshot) ||
	    !receipt_memory_unchanged(receipt_memory, &memory_snapshot) ||
#endif
	    !receipt_ranges_allowed(&owner.receipt) ||
	    memcmp(dependencies, &owner.dependencies,
		sizeof(owner.dependencies)) ||
	    !dependencies_valid(&owner.dependencies, runtime_view) ||
	    owner.observer.sha256(owner.observer.context,
		(const void *)(uintptr_t)owner.receipt.tables.base,
		(size_t)owner.receipt.tables.size,
		owner.receipt.table_digest) != CB_SUCCESS ||
	    starbook_mtl_dma_smm_verify(&owner.receipt, (uintptr_t)&owner,
		sizeof(owner), &owner.observer, runtime_view,
		&owner.workspace) != CB_SUCCESS ||
	    memcmp(frame, &owner.snapshot, sizeof(owner.snapshot)) ||
	    memcmp(dependencies, &owner.dependencies,
		sizeof(owner.dependencies)) ||
	    !dependencies_valid(&owner.dependencies, runtime_view) ||
	    !owner.dependencies.ordinary_dram_range(owner.dependencies.context,
		frame_base, frame_size) ||
	    !receipt_ranges_allowed(&owner.receipt) ||
#if !ENV_TEST
	    !receipt_memory_unchanged(receipt_memory, &memory_snapshot) ||
	    !receipt_layout_matches_memory(&owner.receipt, &memory_snapshot) ||
#endif
	    memcmp(dependencies, &owner.dependencies,
		sizeof(owner.dependencies)) ||
	    !dependencies_valid(&owner.dependencies, runtime_view))
		goto out;
	frame->state = STARBOOK_MTL_DMA_RECEIPT_FRAME_ACCEPTED;
	if (expected_active_ops->write_value(expected_active_ops->context,
		matched_cpu, STARBOOK_MTL_DMA_RECEIPT_WIRE_SUCCESS) != CB_SUCCESS)
		goto out;
	__atomic_store_n(&owner.state, RECEIPT_READY, __ATOMIC_RELEASE);
	status = CB_SUCCESS;
out:
	if (status != CB_SUCCESS) {
		if (frame && frame_valid(frame))
			frame->state = STARBOOK_MTL_DMA_RECEIPT_FRAME_REJECTED;
		memset(&owner.receipt, 0, sizeof(owner.receipt));
		memset(&owner.observer, 0, sizeof(owner.observer));
		memset(&owner.context, 0, sizeof(owner.context));
		memset(&owner.dependencies, 0, sizeof(owner.dependencies));
		memset(&owner.workspace, 0, sizeof(owner.workspace));
		memset(&owner.authority, 0, sizeof(owner.authority));
		memset(&owner.memory_snapshot, 0, sizeof(owner.memory_snapshot));
		__atomic_store_n(&owner.state, RECEIPT_FAILED, __ATOMIC_RELEASE);
	}
	memset(&owner.snapshot, 0, sizeof(owner.snapshot));
	memset(&binding, 0, sizeof(binding));
	memset(&instance, 0, sizeof(instance));
	memset(&topology, 0, sizeof(topology));
	return status;
}

enum cb_err starbook_mtl_dma_smm_binding_get(
	struct starbook_mtl_dma_smm_binding *binding)
{
	const struct smm_invocation_runtime_view *runtime_view;
	struct smm_invocation_runtime_binding runtime_binding;
	struct smm_invocation_loader_instance instance;
	struct starbook_mtl_dma_smm_binding value;
#if !ENV_TEST
	const struct smm_dma_receipt_memory *receipt_memory;
#endif

	if (!binding ||
	    smm_invocation_runtime_view_get(&runtime_view) != CB_SUCCESS ||
	    smm_invocation_runtime_binding_get(&runtime_binding) != CB_SUCCESS ||
	    smm_invocation_runtime_range_is_protected(runtime_view, binding,
		sizeof(*binding)) != CB_SUCCESS ||
	    smm_invocation_runtime_range_is_protected(runtime_view, &owner,
		sizeof(owner)) != CB_SUCCESS ||
	    __atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) != RECEIPT_READY ||
	    smm_invocation_loader_instance_read(runtime_binding.instance,
		&instance) != CB_SUCCESS ||
	    runtime_binding.evidence->generation !=
		owner.receipt.invocation_generation ||
	    instance.lifecycle != owner.receipt.loader_lifecycle ||
	    !smm_invocation_loader_instance_nonce_equal(
		instance.loader_instance_nonce,
		owner.receipt.loader_instance_nonce) ||
#if !ENV_TEST
	    !smm_get_dma_receipt_memory(&receipt_memory) ||
	    !receipt_memory_unchanged(receipt_memory, &owner.memory_snapshot) ||
	    !receipt_layout_matches_memory(&owner.receipt,
		&owner.memory_snapshot) ||
#endif
	    !dependencies_valid(&owner.dependencies, runtime_view) ||
	    !receipt_ranges_allowed(&owner.receipt) ||
	    starbook_mtl_dma_smm_verify(&owner.receipt, (uintptr_t)&owner,
		sizeof(owner), &owner.observer, runtime_view,
		&owner.workspace) != CB_SUCCESS) {
		if (__atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) == RECEIPT_READY)
			__atomic_store_n(&owner.state, RECEIPT_FAILED,
				__ATOMIC_RELEASE);
		return CB_ERR;
	}
	value.receipt = &owner.receipt;
	if (__atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) != RECEIPT_READY)
		return CB_ERR;
	*binding = value;
	return CB_SUCCESS;
}

static bool epoch_ranges_valid(
	const struct starbook_mtl_dma_smm_epoch_range ranges[2],
	const struct starbook_mtl_dma_smm_receipt *receipt)
{
	return ranges && receipt && ranges[0].size == 4096U &&
		ranges[1].size == 4096U && ranges[0].base && ranges[1].base &&
		!(ranges[0].base & 4095U) && !(ranges[1].base & 4095U) &&
		ranges[0].base <= UINT64_MAX - 4095U &&
		ranges[1].base <= UINT64_MAX - 4095U &&
		ranges[0].base != ranges[1].base &&
		starbook_mtl_dma_smm_receipt_geometry_valid(receipt,
			ranges[0].base, 4096U) &&
		starbook_mtl_dma_smm_receipt_geometry_valid(receipt,
			ranges[1].base, 4096U);
}

static bool epoch_owner_protected(
	const struct smm_invocation_runtime_view *runtime_view)
{
	return runtime_view &&
		smm_invocation_runtime_range_is_protected(runtime_view, &owner,
			sizeof(owner)) == CB_SUCCESS;
}

void starbook_mtl_dma_smm_epoch_poison(void)
{
	__atomic_store_n(&owner.epoch.state, EPOCH_POISONED, __ATOMIC_RELEASE);
}

enum cb_err starbook_mtl_dma_smm_epoch_prepare(
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct starbook_mtl_dma_smm_epoch_range ranges[2])
{
	const struct smm_invocation_runtime_view *runtime_view;
	struct smm_invocation_runtime_binding runtime;
	struct starbook_mtl_dma_smm_binding binding;
	struct smm_invocation_loader_instance instance_snapshot;
	struct smm_invocation_topology topology_snapshot;
	struct epoch_evidence_snapshot evidence_snapshot;
	uint32_t expected = EPOCH_EMPTY;

	if (!instance || !evidence || !topology || !ranges ||
	    smm_invocation_runtime_view_get(&runtime_view) != CB_SUCCESS ||
	    !epoch_owner_protected(runtime_view) ||
	    smm_invocation_runtime_range_is_protected(runtime_view, ranges,
		2U * sizeof(*ranges)) != CB_SUCCESS ||
	    !__atomic_compare_exchange_n(&owner.epoch.state, &expected,
		EPOCH_CAPTURING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
	    smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    runtime.instance != instance || runtime.evidence != evidence ||
	    runtime.topology != topology ||
	    smm_invocation_loader_instance_read(instance, &instance_snapshot) !=
		CB_SUCCESS || instance_snapshot.lifecycle !=
		SMM_INVOCATION_LOADER_S3_RELOAD ||
	    smm_invocation_topology_read(topology, &topology_snapshot) != CB_SUCCESS ||
	    (epoch_evidence_read(evidence, &evidence_snapshot),
	     !epoch_evidence_valid(&evidence_snapshot, &instance_snapshot,
		&topology_snapshot, evidence_snapshot.generation)) ||
	    starbook_mtl_dma_smm_binding_get(&binding) != CB_SUCCESS ||
	    !binding.receipt || !epoch_ranges_valid(ranges, binding.receipt))
		goto fail;
	owner.epoch.composition = runtime.composition;
	owner.epoch.instance_identity = instance;
	owner.epoch.evidence_identity = evidence;
	owner.epoch.topology_identity = topology;
	owner.epoch.instance = instance_snapshot;
	owner.epoch.topology = topology_snapshot;
	owner.epoch.receipt = *binding.receipt;
	owner.epoch.sealed_receipt = *binding.receipt;
	memcpy(owner.epoch.ranges, ranges, sizeof(owner.epoch.ranges));
	memcpy(owner.epoch.sealed_ranges, ranges,
		sizeof(owner.epoch.sealed_ranges));
	owner.epoch.predecessor = evidence_snapshot;
	if (owner.observer.sha256(owner.observer.context, &owner.epoch.receipt,
		sizeof(owner.epoch.receipt), owner.epoch.receipt_digest) != CB_SUCCESS)
		goto fail;
	memcpy(owner.epoch.sealed_digest, owner.epoch.receipt_digest,
		sizeof(owner.epoch.sealed_digest));
	if (!epoch_owner_protected(runtime_view) ||
	    memcmp(&owner.epoch.receipt, &owner.epoch.sealed_receipt,
		sizeof(owner.epoch.receipt)) ||
	    memcmp(owner.epoch.ranges, owner.epoch.sealed_ranges,
		sizeof(owner.epoch.ranges)) ||
	    memcmp(owner.epoch.receipt_digest, owner.epoch.sealed_digest,
		sizeof(owner.epoch.receipt_digest)) ||
	    memcmp(binding.receipt, &owner.epoch.receipt,
		sizeof(owner.epoch.receipt)) ||
	    !epoch_evidence_stable(evidence, &owner.epoch.predecessor))
		goto fail;
	__atomic_store_n(&owner.epoch.state, EPOCH_SEALED, __ATOMIC_RELEASE);
	return CB_SUCCESS;
fail:
	starbook_mtl_dma_smm_epoch_poison();
	return CB_ERR;
}

enum cb_err starbook_mtl_dma_smm_epoch_activate(
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_entry_ticket *ticket,
	const struct starbook_mtl_dma_smm_epoch_range ranges[2])
{
	const struct smm_invocation_runtime_view *runtime_view;
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_loader_instance instance_snapshot;
	struct smm_invocation_topology topology_snapshot;
	struct starbook_mtl_dma_smm_observer epoch_observer;
	struct epoch_evidence_snapshot successor;
	uint8_t digest[STARBOOK_MTL_DMA_SMM_DIGEST_SIZE];
#if !ENV_TEST
	const struct smm_dma_receipt_memory *receipt_memory;
#endif
	uint32_t expected = EPOCH_SEALED;

	if (!instance || !evidence || !topology || !ticket || !ranges ||
	    smm_invocation_runtime_view_get(&runtime_view) != CB_SUCCESS ||
	    !epoch_owner_protected(runtime_view) ||
	    !__atomic_compare_exchange_n(&owner.epoch.state, &expected,
		EPOCH_ACTIVATING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
	    smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    runtime.composition != owner.epoch.composition ||
	    instance != owner.epoch.instance_identity ||
	    evidence != owner.epoch.evidence_identity ||
	    topology != owner.epoch.topology_identity ||
	    runtime.instance != instance || runtime.evidence != evidence ||
	    runtime.topology != topology ||
	    smm_invocation_loader_instance_read(instance, &instance_snapshot) !=
		CB_SUCCESS || memcmp(&instance_snapshot, &owner.epoch.instance,
		sizeof(instance_snapshot)) ||
	    smm_invocation_topology_read(topology, &topology_snapshot) != CB_SUCCESS ||
	    memcmp(&topology_snapshot, &owner.epoch.topology,
		sizeof(topology_snapshot)) ||
	    owner.epoch.predecessor.generation == UINT64_MAX ||
	    ticket->generation != owner.epoch.predecessor.generation + 1U ||
	    ticket->cpu != topology_snapshot.bsp_cpu ||
	    ticket->lifecycle != SMM_INVOCATION_LOADER_S3_RELOAD ||
	    !smm_invocation_loader_instance_nonce_equal(ticket->loader_instance_nonce,
		instance_snapshot.loader_instance_nonce) ||
	    (epoch_evidence_read(evidence, &successor),
	     !epoch_evidence_valid(&successor, &instance_snapshot,
		&topology_snapshot, ticket->generation)) ||
	    memcmp(ranges, owner.epoch.ranges, sizeof(owner.epoch.ranges)) ||
	    memcmp(owner.epoch.ranges, owner.epoch.sealed_ranges,
		sizeof(owner.epoch.ranges)) ||
	    memcmp(&owner.epoch.receipt, &owner.epoch.sealed_receipt,
		sizeof(owner.epoch.receipt)) ||
	    memcmp(&owner.receipt, &owner.epoch.receipt,
		sizeof(owner.receipt)) ||
	    !epoch_ranges_valid(owner.epoch.ranges, &owner.epoch.receipt) ||
	    owner.observer.sha256(owner.observer.context, &owner.epoch.receipt,
		sizeof(owner.epoch.receipt), digest) != CB_SUCCESS ||
	    memcmp(digest, owner.epoch.receipt_digest, sizeof(digest)) ||
	    memcmp(digest, owner.epoch.sealed_digest, sizeof(digest)) ||
#if !ENV_TEST
	    !smm_get_dma_receipt_memory(&receipt_memory) ||
	    !receipt_memory_unchanged(receipt_memory, &owner.memory_snapshot) ||
	    !receipt_layout_matches_memory(&owner.epoch.receipt,
		&owner.memory_snapshot) ||
#endif
	    !dependencies_valid(&owner.dependencies, runtime_view) ||
	    !receipt_ranges_allowed(&owner.epoch.receipt))
		goto fail;
	owner.epoch.live_binding = owner.epoch.sealed_live_binding =
		(struct starbook_mtl_dma_requester_binding) {
			.loader_instance_nonce = instance_snapshot.loader_instance_nonce,
			.invocation_generation = ticket->generation,
			.loader_lifecycle = instance_snapshot.lifecycle,
		};
	epoch_observer = owner.observer;
	epoch_observer.verify_translation = observer_verify_translation_epoch;
	if (starbook_mtl_dma_smm_verify(&owner.epoch.receipt, (uintptr_t)&owner,
		sizeof(owner), &epoch_observer, runtime_view,
		&owner.workspace) != CB_SUCCESS ||
	    !epoch_ranges_valid(owner.epoch.ranges, &owner.epoch.receipt) ||
	    !epoch_owner_protected(runtime_view) ||
	    !epoch_evidence_stable(evidence, &successor))
		goto fail;
	owner.epoch.activation = successor;
	__atomic_store_n(&owner.epoch.state, EPOCH_ACTIVATION_PROOF,
		__ATOMIC_RELEASE);
	memset(digest, 0, sizeof(digest));
	return CB_SUCCESS;
fail:
	memset(digest, 0, sizeof(digest));
	starbook_mtl_dma_smm_epoch_poison();
	return CB_ERR;
}

enum cb_err starbook_mtl_dma_smm_epoch_retain(void)
{
	uint32_t expected = EPOCH_ACTIVATION_PROOF;

	if (!__atomic_compare_exchange_n(&owner.epoch.state, &expected,
		EPOCH_RETAINED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		starbook_mtl_dma_smm_epoch_poison();
		return CB_ERR;
	}
	return CB_SUCCESS;
}

bool starbook_mtl_dma_smm_epoch_range_protected(
	void *unused, uint64_t base, uint64_t size)
{
	const struct smm_invocation_runtime_view *runtime_view;
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_topology topology;
	struct epoch_evidence_snapshot evidence_snapshot;
	struct starbook_mtl_dma_smm_observer epoch_observer;
	uint8_t digest[STARBOOK_MTL_DMA_SMM_DIGEST_SIZE];
	uint32_t expected;
	uint32_t proof_state = EPOCH_EMPTY;
	bool valid = false;

	(void)unused;
	if (size != 4096U ||
	    (base != owner.epoch.ranges[0].base &&
	     base != owner.epoch.ranges[1].base))
		return false;
	for (uint32_t poll = 0; poll < SMM_INVOCATION_ENTRY_MAX_POLLS; poll++) {
		expected = EPOCH_ACTIVATION_PROOF;
		if (__atomic_compare_exchange_n(&owner.epoch.state, &expected,
			EPOCH_VERIFYING_ACTIVATION, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE)) {
			proof_state = EPOCH_ACTIVATION_PROOF;
			break;
		}
		expected = EPOCH_RETAINED;
		if (__atomic_compare_exchange_n(&owner.epoch.state, &expected,
			EPOCH_VERIFYING_RETAINED, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE)) {
			proof_state = EPOCH_RETAINED;
			break;
		}
		if (expected != EPOCH_VERIFYING_ACTIVATION &&
		    expected != EPOCH_VERIFYING_RETAINED)
			return false;
		__asm__ __volatile__("pause");
	}
	if (!proof_state)
		return false;
	if (smm_invocation_runtime_view_get(&runtime_view) != CB_SUCCESS ||
	    !epoch_owner_protected(runtime_view) ||
	    smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    runtime.composition != owner.epoch.composition ||
	    runtime.instance != owner.epoch.instance_identity ||
	    runtime.evidence != owner.epoch.evidence_identity ||
	    runtime.topology != owner.epoch.topology_identity ||
	    smm_invocation_loader_instance_read(runtime.instance, &instance) !=
		CB_SUCCESS || memcmp(&instance, &owner.epoch.instance,
		sizeof(instance)) ||
	    smm_invocation_topology_read(runtime.topology, &topology) != CB_SUCCESS ||
	    memcmp(&topology, &owner.epoch.topology, sizeof(topology)) ||
	    !epoch_evidence_capture(runtime.evidence, &evidence_snapshot) ||
	    (proof_state == EPOCH_ACTIVATION_PROOF ?
	     !epoch_evidence_valid(&evidence_snapshot, &instance, &topology,
		owner.epoch.activation.generation) :
	     !epoch_retained_evidence_valid(runtime.evidence, &evidence_snapshot,
		&instance, &topology, owner.epoch.activation.generation,
		owner.epoch.sealed_live_binding.invocation_generation)) ||
	    evidence_snapshot.generation <
		owner.epoch.sealed_live_binding.invocation_generation ||
	    memcmp(owner.epoch.ranges, owner.epoch.sealed_ranges,
		sizeof(owner.epoch.ranges)) ||
	    memcmp(&owner.epoch.receipt, &owner.epoch.sealed_receipt,
		sizeof(owner.epoch.receipt)) ||
	    !epoch_ranges_valid(owner.epoch.ranges, &owner.epoch.receipt))
		goto out;
	owner.epoch.live_binding = (struct starbook_mtl_dma_requester_binding) {
		.loader_instance_nonce = instance.loader_instance_nonce,
		.invocation_generation = evidence_snapshot.generation,
		.loader_lifecycle = instance.lifecycle,
	};
	owner.epoch.sealed_live_binding = owner.epoch.live_binding;
	epoch_observer = owner.observer;
	epoch_observer.verify_translation = observer_verify_translation_live;
	if (owner.observer.sha256(owner.observer.context, &owner.epoch.receipt,
		sizeof(owner.epoch.receipt), digest) != CB_SUCCESS ||
	    memcmp(digest, owner.epoch.receipt_digest, sizeof(digest)) ||
	    memcmp(digest, owner.epoch.sealed_digest, sizeof(digest)) ||
	    starbook_mtl_dma_smm_verify(&owner.epoch.receipt,
		(uintptr_t)&owner, sizeof(owner), &epoch_observer, runtime_view,
		&owner.workspace) != CB_SUCCESS ||
	    !epoch_owner_protected(runtime_view) ||
	    !epoch_evidence_stable(runtime.evidence, &evidence_snapshot) ||
	    memcmp(&owner.epoch.live_binding,
		&owner.epoch.sealed_live_binding,
		sizeof(owner.epoch.live_binding)))
		goto out;
	valid = true;
out:
	memset(&instance, 0, sizeof(instance));
	memset(&topology, 0, sizeof(topology));
	memset(&evidence_snapshot, 0, sizeof(evidence_snapshot));
	memset(&epoch_observer, 0, sizeof(epoch_observer));
	memset(digest, 0, sizeof(digest));
	if (!valid) {
		starbook_mtl_dma_smm_epoch_poison();
		return false;
	}
	__atomic_store_n(&owner.epoch.state, proof_state, __ATOMIC_RELEASE);
	return true;
}
