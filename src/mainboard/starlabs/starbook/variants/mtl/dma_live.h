/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_DMA_LIVE_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_DMA_LIVE_H

#include <commonlib/dma_handoff.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STARBOOK_MTL_DMA_LIVE_REQUESTERS 3U
#define STARBOOK_MTL_DMA_LIVE_MAX_FUNCTIONS 512U

struct starbook_mtl_dma_live_pci_io {
	void *context;
	uint32_t (*read32)(void *context, uint8_t bus, uint8_t devfn,
		uint16_t offset);
	void (*write16)(void *context, uint8_t bus, uint8_t devfn,
		uint16_t offset, uint16_t value);
};

struct starbook_mtl_dma_live_identity {
	uint16_t bdf;
	uint16_t vendor;
	uint16_t device;
	uint32_t class;
};

struct starbook_mtl_dma_live_layout {
	void *handoff;
	size_t handoff_capacity;
	void *table_memory;
	uint64_t table_physical;
	size_t table_capacity_pages;
	size_t table_used_pages;
	uint64_t arena_base[STARBOOK_MTL_DMA_LIVE_REQUESTERS];
	uint32_t arena_pages[STARBOOK_MTL_DMA_LIVE_REQUESTERS];
};

struct vtd_transition_io;

int starbook_mtl_dma_live_establish(
	const struct starbook_mtl_dma_live_pci_io *pci_io, uint16_t bus_count,
	const struct starbook_mtl_dma_live_identity requesters[
		STARBOOK_MTL_DMA_LIVE_REQUESTERS],
	void *memory, uint64_t physical_base, size_t size,
	const struct vtd_transition_io *transition);
bool starbook_mtl_dma_live_verify_active(
	const struct starbook_mtl_dma_live_pci_io *pci_io, uint16_t bus_count);
const struct starbook_mtl_dma_live_layout *starbook_mtl_dma_live_layout(void);
bool starbook_mtl_dma_live_handoff_requesters(
	struct dma_handoff_requester output[STARBOOK_MTL_DMA_LIVE_REQUESTERS]);

#endif
