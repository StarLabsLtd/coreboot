/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_smm_receipt_provision.h"

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

struct receipt_context {
	uint64_t identity;
};

static struct {
	uint32_t state;
	uint32_t reserved;
	struct receipt_context context;
	struct starbook_mtl_dma_receipt_dependencies dependencies;
	struct starbook_mtl_dma_smm_observer observer;
	struct starbook_mtl_dma_smm_receipt receipt;
	struct starbook_mtl_dma_smm_workspace workspace;
	struct starbook_mtl_dma_receipt_frame snapshot;
} owner __aligned(8);

__weak enum cb_err starbook_mtl_dma_receipt_policy(
	const struct starbook_mtl_dma_receipt_dependencies **dependencies)
{
	if (dependencies)
		*dependencies = NULL;
	return CB_ERR;
}

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

enum cb_err starbook_mtl_dma_receipt_provision_receive(
	const struct smm_invocation_save_state_ops *expected_active_ops)
{
	struct starbook_mtl_dma_receipt_frame *frame = NULL;
	const struct smm_invocation_runtime_view *runtime_view;
	struct smm_invocation_runtime_binding binding;
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_topology topology;
	const struct starbook_mtl_dma_receipt_dependencies *dependencies;
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
	owner.context.identity = 0x4d544c444d41524dULL;
	owner.observer = (struct starbook_mtl_dma_smm_observer) {
		.context = &owner.context,
		.context_size = sizeof(owner.context),
		.read32 = observer_read32,
		.sha256 = observer_sha256,
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
	    !receipt_ranges_allowed(&owner.receipt) ||
	    memcmp(dependencies, &owner.dependencies,
		sizeof(owner.dependencies)) ||
	    !dependencies_valid(&owner.dependencies, runtime_view) ||
	    owner.observer.sha256(owner.observer.context,
		(const void *)(uintptr_t)owner.receipt.tables.base,
		owner.receipt.tables.size, owner.receipt.table_digest) != CB_SUCCESS ||
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
		owner.receipt.loader_instance_nonce)) {
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
