/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION_H

#include "dma_smm_policy.h"

#include <cpu/x86/smm_invocation_evidence.h>
#include <cpu/x86/smm_command.h>

#define STARBOOK_MTL_DMA_RECEIPT_FRAME_REVISION 1U
#define STARBOOK_MTL_DMA_RECEIPT_FRAME_REQUEST 1U
#define STARBOOK_MTL_DMA_RECEIPT_FRAME_ACCEPTED 2U
#define STARBOOK_MTL_DMA_RECEIPT_FRAME_REJECTED 3U
#define STARBOOK_MTL_DMA_RECEIPT_WIRE_REQUEST 0x525043fdU
#define STARBOOK_MTL_DMA_RECEIPT_WIRE_SUCCESS 0x52504f4bU
#define STARBOOK_MTL_DMA_RECEIPT_DEPENDENCIES_REVISION 1U

struct starbook_mtl_dma_receipt_dependencies {
	uint32_t revision;
	uint32_t size;
	void *context;
	size_t context_size;
	bool (*ordinary_dram_range)(void *context, uint64_t base, size_t size);
};

struct starbook_mtl_dma_receipt_frame {
	uint32_t revision;
	uint32_t size;
	uint32_t state;
	uint32_t reserved;
	struct starbook_mtl_dma_smm_receipt candidate;
} __aligned(8);

struct starbook_mtl_dma_smm_binding {
	const struct starbook_mtl_dma_smm_receipt *receipt;
};

enum cb_err starbook_mtl_dma_receipt_provision_send(void);
enum cb_err starbook_mtl_dma_receipt_candidate_build(
	struct starbook_mtl_dma_smm_receipt *candidate);

#if ENV_SMM || ENV_TEST
enum cb_err starbook_mtl_dma_receipt_policy(
	const struct starbook_mtl_dma_receipt_dependencies **dependencies);
enum cb_err starbook_mtl_dma_receipt_provision_receive(
	const struct smm_invocation_save_state_ops *expected_active_ops);
enum cb_err starbook_mtl_dma_smm_binding_get(
	struct starbook_mtl_dma_smm_binding *binding);
#endif

_Static_assert(sizeof(struct starbook_mtl_dma_receipt_frame) == 6392,
	"MTL DMA receipt frame ABI changed");

#endif
