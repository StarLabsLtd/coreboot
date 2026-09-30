/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_DMA_SMM_AUTHORITY_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_DMA_SMM_AUTHORITY_H

#include "dma_smm_policy.h"
#include "dma_smm_requester_authority.h"

#include <soc/intel/common/block/vtd/vtd_translation.h>

struct starbook_mtl_dma_smm_authority_context;

struct starbook_mtl_dma_smm_authority_workspace {
	struct starbook_mtl_dma_requester_binding binding;
	struct starbook_mtl_dma_requester_authority authority;
	struct starbook_mtl_dma_requester_authority_io io;
	struct vtd_translation_requester requesters[
		STARBOOK_MTL_DMA_REQUESTER_ROLE_COUNT];
	struct vtd_translation_view view;
	struct starbook_mtl_dma_smm_range table;
	const struct smm_invocation_runtime_view *runtime_view;
	uint64_t identity;
};

enum cb_err starbook_mtl_dma_smm_authority_verify(void *context,
	const struct starbook_mtl_dma_smm_receipt *receipt);
enum cb_err starbook_mtl_dma_smm_authority_verify_epoch(void *context,
	const struct starbook_mtl_dma_smm_receipt *sealed_receipt,
	const struct starbook_mtl_dma_requester_binding *live_binding);
enum cb_err starbook_mtl_dma_smm_authority_verify_live_policy(void *context,
	const struct starbook_mtl_dma_smm_receipt *sealed_policy,
	const struct starbook_mtl_dma_requester_binding *current_binding);

#endif
