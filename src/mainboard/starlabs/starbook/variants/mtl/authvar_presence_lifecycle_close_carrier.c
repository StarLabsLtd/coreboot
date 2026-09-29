/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_smm_receipt_provision.h"

#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "MTL lifecycle-close carrier validator is SMM-only"
#endif

#define CARRIER_CONTEXT_REVISION 1U
#define CARRIER_CONTEXT_IDENTITY 0x4d544c4341525249ULL

static const struct {
	uint32_t revision;
	uint32_t size;
	uint64_t identity;
} carrier_context = {
	.revision = CARRIER_CONTEXT_REVISION,
	.size = sizeof(carrier_context),
	.identity = CARRIER_CONTEXT_IDENTITY,
};

static bool span_valid(uint64_t base, uint64_t size)
{
	return base && size && base <= UINT64_MAX - (size - 1U) &&
		base <= UINTPTR_MAX && size - 1U <= UINTPTR_MAX - (uintptr_t)base;
}

static bool overlap(uint64_t first, uint64_t first_size,
	uint64_t second, uint64_t second_size)
{
	if (!span_valid(first, first_size) || !span_valid(second, second_size))
		return true;
	return first <= second ? second - first < first_size :
		first - second < second_size;
}

static bool context_valid(const void *context,
	const struct smm_invocation_runtime_view *view)
{
	return context == &carrier_context &&
		carrier_context.revision == CARRIER_CONTEXT_REVISION &&
		carrier_context.size == sizeof(carrier_context) &&
		carrier_context.identity == CARRIER_CONTEXT_IDENTITY &&
		smm_invocation_runtime_range_is_protected(view, &carrier_context,
			sizeof(carrier_context)) == CB_SUCCESS;
}

bool starbook_mtl_lifecycle_install_communication_range_valid(void *context,
	uint64_t base, uint64_t size)
{
	const struct smm_invocation_runtime_view *view;
	const struct smm_dma_receipt_memory *memory;
	const struct smm_dma_receipt_memory *rechecked;
	struct smm_dma_receipt_memory snapshot;
	struct starbook_mtl_dma_smm_binding binding = { 0 };
	struct smm_save_state_span save_state;
	uintptr_t smram_base;
	size_t smram_size;
	uintptr_t frame_base;
	size_t frame_size;
	uint32_t cpus;
	bool valid = false;

	if (!span_valid(base, size) ||
	    size != sizeof(struct
		payload_mm_authvar_presence_lifecycle_close_install_frame) ||
	    smm_invocation_runtime_view_get(&view) != CB_SUCCESS ||
	    !context_valid(context, view) ||
	    !smm_get_dma_receipt_memory(&memory) || !memory ||
	    smm_invocation_runtime_range_is_protected(view, memory,
		sizeof(*memory)) != CB_SUCCESS)
		goto out;
	snapshot = *memory;
	smm_region(&smram_base, &smram_size);
	if (snapshot.revision != SMM_DMA_RECEIPT_MEMORY_REVISION ||
	    snapshot.size != sizeof(snapshot) ||
	    !span_valid(snapshot.frame.base, snapshot.frame.size) ||
	    !span_valid(snapshot.dma.base, snapshot.dma.size) ||
	    !span_valid(snapshot.mirror.base, snapshot.mirror.size) ||
	    snapshot.frame.size != sizeof(struct starbook_mtl_dma_receipt_frame) ||
	    snapshot.frame.base %
		_Alignof(struct starbook_mtl_dma_receipt_frame) ||
	    snapshot.frame.base > UINT32_MAX - (snapshot.frame.size - 1U) ||
	    base != snapshot.frame.base ||
	    size > snapshot.frame.size ||
	    overlap(snapshot.frame.base, snapshot.frame.size,
		snapshot.dma.base, snapshot.dma.size) ||
	    overlap(snapshot.frame.base, snapshot.frame.size,
		snapshot.mirror.base, snapshot.mirror.size) ||
	    overlap(snapshot.dma.base, snapshot.dma.size,
		snapshot.mirror.base, snapshot.mirror.size) ||
	    !span_valid(smram_base, smram_size) ||
	    overlap(snapshot.frame.base, snapshot.frame.size,
		smram_base, smram_size) ||
	    overlap(snapshot.dma.base, snapshot.dma.size, smram_base, smram_size) ||
	    overlap(snapshot.mirror.base, snapshot.mirror.size,
		smram_base, smram_size) ||
	    !smm_get_dma_receipt_frame(&frame_base, &frame_size) ||
	    frame_base != snapshot.frame.base || frame_size != snapshot.frame.size ||
	    smm_invocation_runtime_range_is_protected(view,
		(const void *)(uintptr_t)snapshot.frame.base,
		sizeof(struct starbook_mtl_dma_receipt_frame)) == CB_SUCCESS ||
	    smm_invocation_runtime_cpu_count(view, &cpus) != CB_SUCCESS)
		goto out;
	for (uint32_t cpu = 0; cpu < cpus; cpu++) {
		if (smm_invocation_runtime_save_state_span(view, cpu, &save_state) !=
		    CB_SUCCESS || overlap(snapshot.frame.base, snapshot.frame.size,
			save_state.base, save_state.size))
			goto out;
	}
	if (starbook_mtl_dma_smm_binding_get(&binding) != CB_SUCCESS ||
	    !binding.receipt ||
	    binding.current.loader_lifecycle !=
		SMM_INVOCATION_LOADER_NON_S3_LOAD ||
	    binding.receipt->loader_lifecycle !=
		binding.current.loader_lifecycle ||
	    !smm_get_dma_receipt_memory(&rechecked) ||
	    rechecked != memory || memcmp(rechecked, &snapshot, sizeof(snapshot)) ||
	    !context_valid(context, view))
		goto out;
	valid = true;
out:
	memset(&binding, 0, sizeof(binding));
	memset(&snapshot, 0, sizeof(snapshot));
	return valid;
}

void *starbook_mtl_lifecycle_install_communication_context(void)
{
	return (void *)&carrier_context;
}

size_t starbook_mtl_lifecycle_install_communication_context_size(void)
{
	return sizeof(carrier_context);
}

_Static_assert(sizeof(struct
	payload_mm_authvar_presence_lifecycle_close_install_frame) <=
	sizeof(struct starbook_mtl_dma_receipt_frame),
	"lifecycle-close install frame exceeds DMA receipt carrier");
