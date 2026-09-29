/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_DMA_SMM_POLICY_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_DMA_SMM_POLICY_H

#include <commonlib/bsd/cb_err.h>
#include <cpu/x86/smm_invocation_loader_identity.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <device/pci_bme_quiesce.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STARBOOK_MTL_DMA_SMM_RECEIPT_REVISION 1U
#define STARBOOK_MTL_DMA_SMM_ARENAS 3U
#define STARBOOK_MTL_DMA_SMM_DIGEST_SIZE 32U
#define STARBOOK_MTL_DMA_SMM_GFX_QUIESCED 1U

struct smm_invocation_runtime_view;

struct starbook_mtl_dma_smm_range {
	uint64_t base;
	uint64_t size;
};

/* Authority exists only after this entire value is copied into SMRAM. */
struct starbook_mtl_dma_smm_receipt {
	uint32_t revision;
	uint32_t size;
	uint16_t bus_count;
	uint16_t function_count;
	uint32_t gfx_mode;
	uint64_t ecam_base;
	uint64_t vtvc0_base;
	uint64_t vtvc0_rtaddr;
	uint64_t gfx_base;
	uint64_t gfxvtbar_register;
	uint64_t gfxvtbar_value;
	struct starbook_mtl_dma_smm_range handoff;
	struct starbook_mtl_dma_smm_range tables;
	struct starbook_mtl_dma_smm_range table_mirror;
	struct starbook_mtl_dma_smm_range arenas[STARBOOK_MTL_DMA_SMM_ARENAS];
	uint64_t table_used_bytes;
	uint8_t table_digest[STARBOOK_MTL_DMA_SMM_DIGEST_SIZE];
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	uint64_t invocation_generation;
	uint32_t loader_lifecycle;
	uint32_t identity_reserved;
	struct pci_bme_quiesce_function functions[
		PCI_BME_QUIESCE_MAX_FUNCTIONS];
};

struct starbook_mtl_dma_smm_observer {
	void *context;
	size_t context_size;
	enum cb_err (*read32)(void *context, uint64_t address, uint32_t *value);
	enum cb_err (*sha256)(void *context, const void *data, size_t size,
		uint8_t digest[STARBOOK_MTL_DMA_SMM_DIGEST_SIZE]);
};

struct starbook_mtl_dma_smm_workspace {
	struct starbook_mtl_dma_smm_receipt receipt;
	struct starbook_mtl_dma_smm_observer observer;
	uint8_t digest[STARBOOK_MTL_DMA_SMM_DIGEST_SIZE];
};

_Static_assert(sizeof(struct starbook_mtl_dma_smm_range) == 16,
	"MTL SMM DMA range ABI changed");
_Static_assert(sizeof(struct starbook_mtl_dma_smm_receipt) == 6376,
	"MTL SMM DMA receipt ABI changed");
_Static_assert(offsetof(struct starbook_mtl_dma_smm_receipt, functions) == 232,
	"MTL SMM DMA receipt inventory moved");

enum cb_err starbook_mtl_dma_smm_verify(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	uint64_t lifecycle_base, size_t lifecycle_size,
	const struct starbook_mtl_dma_smm_observer *observer,
	const struct smm_invocation_runtime_view *runtime_view,
	struct starbook_mtl_dma_smm_workspace *workspace);

bool starbook_mtl_dma_smm_receipt_geometry_valid(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	uint64_t forbidden_base, size_t forbidden_size);

#endif
