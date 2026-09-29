/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_runtime.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_provision.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

enum test_mode {
	VALID,
	WRONG_BASE,
	WRONG_SIZE,
	FRAME_DMA_OVERLAP,
	FRAME_MIRROR_OVERLAP,
	DMA_MIRROR_OVERLAP,
	FRAME_SMRAM_OVERLAP,
	FRAME_SAVE_STATE_OVERLAP,
	FRAME_PROTECTED,
	CONTEXT_UNPROTECTED,
	MEMORY_UNPROTECTED,
	MEMORY_MUTATION,
	FRAME_GETTER_MUTATION,
	BAD_MEMORY_REVISION,
	BAD_MEMORY_SIZE,
	MISALIGNED_FRAME,
	FRAME_ABOVE_4G,
	BINDING_FAILURE,
	S3_LIFECYCLE,
	RECEIPT_LIFECYCLE_DRIFT,
};

static enum test_mode mode;
static const void *legitimate_context;
static unsigned int memory_reads;
static const struct smm_invocation_runtime_view *const runtime_view =
	(const void *)(uintptr_t)0x1000;
static struct smm_dma_receipt_memory memory = {
	.revision = SMM_DMA_RECEIPT_MEMORY_REVISION,
	.size = sizeof(memory),
	.frame = { .base = 0x300000, .size = sizeof(struct starbook_mtl_dma_receipt_frame) },
	.dma = { .base = 0x400000, .size = 0x1000 },
	.mirror = { .base = 0x500000, .size = 0x1000 },
};
static struct starbook_mtl_dma_smm_receipt receipt = {
	.loader_lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
};

enum cb_err smm_invocation_runtime_view_get(
	const struct smm_invocation_runtime_view **view)
{
	*view = runtime_view;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base,
	size_t size)
{
	const uintptr_t address = (uintptr_t)base;

	assert(view == runtime_view);
	if (base == legitimate_context)
		return mode == CONTEXT_UNPROTECTED ? CB_ERR : CB_SUCCESS;
	if (base == &memory)
		return mode == MEMORY_UNPROTECTED ? CB_ERR : CB_SUCCESS;
	if (address == memory.frame.base && size == memory.frame.size)
		return mode == FRAME_PROTECTED ? CB_SUCCESS : CB_ERR;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_cpu_count(
	const struct smm_invocation_runtime_view *view, uint32_t *active_cpus)
{
	assert(view == runtime_view);
	*active_cpus = 1;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_save_state_span(
	const struct smm_invocation_runtime_view *view, uint32_t cpu,
	struct smm_save_state_span *span)
{
	assert(view == runtime_view && cpu == 0);
	span->base = mode == FRAME_SAVE_STATE_OVERLAP ? memory.frame.base : 0x108000;
	span->size = 0x1000;
	return CB_SUCCESS;
}

bool smm_get_dma_receipt_memory(const struct smm_dma_receipt_memory **output)
{
	memory_reads++;
	if (mode == MEMORY_MUTATION && memory_reads == 2)
		memory.mirror.base += 0x1000;
	*output = &memory;
	return true;
}

bool smm_get_dma_receipt_frame(uintptr_t *base, size_t *size)
{
	*base = memory.frame.base;
	*size = memory.frame.size;
	if (mode == FRAME_GETTER_MUTATION)
		(*base)++;
	return true;
}

void smm_region(uintptr_t *base, size_t *size)
{
	*base = mode == FRAME_SMRAM_OVERLAP ? memory.frame.base : 0x100000;
	*size = 0x10000;
}

enum cb_err starbook_mtl_dma_smm_binding_get(
	struct starbook_mtl_dma_smm_binding *binding)
{
	if (mode == BINDING_FAILURE)
		return CB_ERR;
	binding->receipt = &receipt;
	binding->current.loader_lifecycle = mode == S3_LIFECYCLE ?
		SMM_INVOCATION_LOADER_S3_RELOAD :
		SMM_INVOCATION_LOADER_NON_S3_LOAD;
	if (mode == RECEIPT_LIFECYCLE_DRIFT)
		receipt.loader_lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	return CB_SUCCESS;
}

static void select_mode(const char *name)
{
	static const struct {
		const char *name;
		enum test_mode mode;
	} cases[] = {
		{ "valid", VALID },
		{ "wrong-base", WRONG_BASE },
		{ "wrong-size", WRONG_SIZE },
		{ "frame-dma-overlap", FRAME_DMA_OVERLAP },
		{ "frame-mirror-overlap", FRAME_MIRROR_OVERLAP },
		{ "dma-mirror-overlap", DMA_MIRROR_OVERLAP },
		{ "frame-smram-overlap", FRAME_SMRAM_OVERLAP },
		{ "frame-save-state-overlap", FRAME_SAVE_STATE_OVERLAP },
		{ "frame-protected", FRAME_PROTECTED },
		{ "context-unprotected", CONTEXT_UNPROTECTED },
		{ "memory-unprotected", MEMORY_UNPROTECTED },
		{ "memory-mutation", MEMORY_MUTATION },
		{ "frame-getter-mutation", FRAME_GETTER_MUTATION },
		{ "bad-memory-revision", BAD_MEMORY_REVISION },
		{ "bad-memory-size", BAD_MEMORY_SIZE },
		{ "misaligned-frame", MISALIGNED_FRAME },
		{ "frame-above-4g", FRAME_ABOVE_4G },
		{ "binding-failure", BINDING_FAILURE },
		{ "s3-lifecycle", S3_LIFECYCLE },
		{ "receipt-lifecycle-drift", RECEIPT_LIFECYCLE_DRIFT },
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		if (!strcmp(name, cases[i].name)) {
			mode = cases[i].mode;
			return;
		}
	}
	abort();
}

int main(int argc, char **argv)
{
	uint64_t base;
	uint64_t size;
	bool expected;
	uint64_t fake_context = 0;

	assert(argc == 2);
	select_mode(argv[1]);
	legitimate_context =
		starbook_mtl_lifecycle_install_communication_context();
	assert(legitimate_context);
	assert(starbook_mtl_lifecycle_install_communication_context_size() != 0);

	if (mode == FRAME_DMA_OVERLAP)
		memory.dma.base = memory.frame.base;
	else if (mode == FRAME_MIRROR_OVERLAP)
		memory.mirror.base = memory.frame.base;
	else if (mode == DMA_MIRROR_OVERLAP)
		memory.mirror.base = memory.dma.base;
	else if (mode == BAD_MEMORY_REVISION)
		memory.revision++;
	else if (mode == BAD_MEMORY_SIZE)
		memory.size--;
	else if (mode == MISALIGNED_FRAME)
		memory.frame.base++;
	else if (mode == FRAME_ABOVE_4G)
		memory.frame.base = 0x100000000ULL;
	base = mode == WRONG_BASE ? memory.frame.base + 8U : memory.frame.base;
	size = mode == WRONG_SIZE ? sizeof(struct
		payload_mm_authvar_presence_lifecycle_close_install_frame) - 8U :
		sizeof(struct
		payload_mm_authvar_presence_lifecycle_close_install_frame);
	expected = mode == VALID;
	assert(starbook_mtl_lifecycle_install_communication_range_valid(
		(void *)legitimate_context, base, size) == expected);
	if (mode == VALID)
		assert(starbook_mtl_lifecycle_install_communication_range_valid(
			(void *)legitimate_context, base, size));
	assert(!starbook_mtl_lifecycle_install_communication_range_valid(
		(void *)legitimate_context, memory.frame.base,
		memory.frame.size));
	assert(!starbook_mtl_lifecycle_install_communication_range_valid(
		(void *)legitimate_context, memory.frame.base + 8U, size));
	assert(!starbook_mtl_lifecycle_install_communication_range_valid(
		(void *)legitimate_context, memory.dma.base, size));
	assert(!starbook_mtl_lifecycle_install_communication_range_valid(
		(void *)legitimate_context, memory.mirror.base, size));
	assert(!starbook_mtl_lifecycle_install_communication_range_valid(
		&fake_context, memory.frame.base, sizeof(struct
		payload_mm_authvar_presence_lifecycle_close_install_frame)));
	return 0;
}
