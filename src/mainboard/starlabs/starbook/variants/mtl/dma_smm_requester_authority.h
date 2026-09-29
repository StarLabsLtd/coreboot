/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_DMA_SMM_REQUESTER_AUTHORITY_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_DMA_SMM_REQUESTER_AUTHORITY_H

#include <commonlib/bsd/cb_err.h>
#include <cpu/x86/smm_invocation_loader_identity.h>
#include <stddef.h>
#include <stdint.h>

#define STARBOOK_MTL_DMA_REQUESTER_ROLE_COUNT 3U

enum starbook_mtl_dma_requester_role {
	STARBOOK_MTL_DMA_REQUESTER_NVME,
	STARBOOK_MTL_DMA_REQUESTER_PCH_XHCI,
	STARBOOK_MTL_DMA_REQUESTER_TCSS_XHCI,
};

struct starbook_mtl_dma_requester_binding {
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	uint64_t invocation_generation;
	uint32_t loader_lifecycle;
	uint32_t reserved;
} __aligned(8);

struct starbook_mtl_dma_requester {
	uint16_t bdf;
	uint16_t domain;
	uint16_t vendor;
	uint16_t device;
	uint32_t class;
};

struct starbook_mtl_dma_requester_authority {
	struct starbook_mtl_dma_requester_binding binding;
	struct starbook_mtl_dma_requester requester[
		STARBOOK_MTL_DMA_REQUESTER_ROLE_COUNT];
};

struct starbook_mtl_dma_requester_authority_io {
	void *context;
	enum cb_err (*read_config32)(void *context, uint8_t bus, uint8_t devfn,
		uint16_t offset, uint32_t *value);
	enum cb_err (*read_binding)(void *context,
		struct starbook_mtl_dma_requester_binding *binding);
};

enum cb_err starbook_mtl_dma_requester_authority_derive(
	const struct starbook_mtl_dma_requester_authority_io *io,
	const struct starbook_mtl_dma_requester_binding *expected_binding,
	struct starbook_mtl_dma_requester_authority *authority);

#endif
