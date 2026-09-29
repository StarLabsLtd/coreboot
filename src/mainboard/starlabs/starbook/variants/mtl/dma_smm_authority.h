/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_DMA_SMM_AUTHORITY_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_DMA_SMM_AUTHORITY_H

#include "dma_smm_policy.h"
#include "dma_smm_requester_authority.h"

#include "../../../../../soc/intel/common/block/vtd/vtd_translation.h"

struct starbook_mtl_dma_smm_authority_context {
	uint64_t identity;
	const struct smm_invocation_runtime_view *runtime_view;
	const void *workspace;
	size_t workspace_size;
	const void *retained;
	size_t retained_size;
	uint64_t table_logical_base;
	uint64_t table_read_base;
	uint64_t table_size;
};

struct starbook_mtl_dma_smm_authority_workspace {
	struct starbook_mtl_dma_smm_receipt receipt;
	struct starbook_mtl_dma_smm_authority_context context;
	struct starbook_mtl_dma_requester_authority_io io;
	struct starbook_mtl_dma_requester_binding binding;
	struct starbook_mtl_dma_requester_authority authority;
	struct vtd_translation_requester requesters[
		STARBOOK_MTL_DMA_REQUESTER_ROLE_COUNT];
	struct vtd_translation_view view;
};

enum cb_err starbook_mtl_dma_smm_authority_verify(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	const struct smm_invocation_runtime_view *runtime_view,
	const void *retained, size_t retained_size,
	struct starbook_mtl_dma_smm_authority_workspace *workspace);

#endif
