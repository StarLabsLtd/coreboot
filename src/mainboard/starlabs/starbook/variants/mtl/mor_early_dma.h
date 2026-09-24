/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_H

#include <commonlib/bsd/cb_err.h>
#include <device/pci_bme_quiesce.h>
#include <stdint.h>

#define STARBOOK_MTL_MOR_EARLY_DMA_REVISION 1U

struct starbook_mtl_mor_early_dma_payload {
	uint32_t revision;
	uint32_t size;
	uint64_t generation;
	uint8_t identity[32];
	struct pci_bme_quiesce_snapshot pci;
	uint64_t seal;
} __aligned(8);

struct starbook_mtl_mor_early_dma_record {
	struct starbook_mtl_mor_early_dma_payload primary;
	struct starbook_mtl_mor_early_dma_payload mirror;
} __aligned(8);

enum cb_err starbook_mtl_mor_early_dma_record_build(
	uint64_t generation, const struct pci_bme_quiesce_snapshot *snapshot,
	struct starbook_mtl_mor_early_dma_record *record);
enum cb_err starbook_mtl_mor_early_dma_record_validate(
	const struct starbook_mtl_mor_early_dma_record *record,
	uint64_t generation, const struct pci_bme_quiesce_snapshot *snapshot,
	uint8_t identity[32]);
enum cb_err mainboard_mor_early_dma_prepare(void);

#endif
