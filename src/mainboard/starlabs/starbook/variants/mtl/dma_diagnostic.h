/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_DMA_DIAGNOSTIC_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_DMA_DIAGNOSTIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STARBOOK_MTL_DMA_REQUESTER_COUNT 3U
#define STARBOOK_MTL_DMA_BME_MAX 256U

struct starbook_mtl_dma_diagnostic_io {
	void *context;
	uint32_t (*read_engine32)(void *context, uint32_t offset);
	uint16_t (*read_pci16)(void *context, uint8_t bus, uint8_t devfn,
		uint16_t offset);
};

struct starbook_mtl_dma_facts {
	uint32_t version;
	uint64_t capability;
	uint64_t extended_capability;
	uint32_t status;
	uint64_t root;
	uint32_t protected_memory_enable;
	uint32_t protected_low_base;
	uint32_t protected_low_limit;
	uint64_t protected_high_base;
	uint64_t protected_high_limit;
	uint64_t dma_buffer_base;
	uint64_t dma_buffer_size;
	uint32_t pci_function_count;
	uint32_t bus_master_count;
	uint16_t bus_master_bdf[STARBOOK_MTL_DMA_BME_MAX];
	uint16_t requester_bdf[STARBOOK_MTL_DMA_REQUESTER_COUNT];
	bool requester_present[STARBOOK_MTL_DMA_REQUESTER_COUNT];
	bool requester_bus_master[STARBOOK_MTL_DMA_REQUESTER_COUNT];
};

bool starbook_mtl_dma_collect_facts(
	const struct starbook_mtl_dma_diagnostic_io *io, uint16_t bus_count,
	const uint16_t requesters[STARBOOK_MTL_DMA_REQUESTER_COUNT],
	uint64_t dma_buffer_base, uint64_t dma_buffer_size,
	struct starbook_mtl_dma_facts *facts);

#endif
