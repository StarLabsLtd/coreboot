/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef COMMONLIB_DMA_HANDOFF_H
#define COMMONLIB_DMA_HANDOFF_H

#include <commonlib/bsd/compiler.h>
#include <commonlib/coreboot_tables.h>
#include <stdint.h>

#define DMA_HANDOFF_REVISION 2U
#define DMA_HANDOFF_GRANULE_SHIFT 12U
#define DMA_HANDOFF_MAX_REQUESTERS LB_PRH_PCI_TOPOLOGY_MAX_ENTRIES

#define DMA_HANDOFF_PROTECTION_ACTIVE    (1U << 0)
#define DMA_HANDOFF_DEFAULT_DENY         (1U << 1)
#define DMA_HANDOFF_COMPLETE_BME_INVENTORY_CLEAR (1U << 2)
#define DMA_HANDOFF_COREBOOT_OWNS_IOMMU  (1U << 3)
#define DMA_HANDOFF_PRH_REVISION_4       (1U << 4)
#define DMA_HANDOFF_VALID_UNTIL_EBS      (1U << 5)
#define DMA_HANDOFF_REQUIRED_FLAGS \
	(DMA_HANDOFF_PROTECTION_ACTIVE | DMA_HANDOFF_DEFAULT_DENY | \
	 DMA_HANDOFF_COMPLETE_BME_INVENTORY_CLEAR | \
	 DMA_HANDOFF_COREBOOT_OWNS_IOMMU | \
	 DMA_HANDOFF_PRH_REVISION_4 | DMA_HANDOFF_VALID_UNTIL_EBS)

#define DMA_HANDOFF_REQUESTER_EXACT_BDF       (1U << 0)
#define DMA_HANDOFF_REQUESTER_ISOLATED_DOMAIN (1U << 1)
#define DMA_HANDOFF_REQUESTER_BME_CLEAR       (1U << 2)
#define DMA_HANDOFF_REQUESTER_ARENA_ONLY      (1U << 3)
#define DMA_HANDOFF_REQUESTER_FLAGS \
	(DMA_HANDOFF_REQUESTER_EXACT_BDF | \
	 DMA_HANDOFF_REQUESTER_ISOLATED_DOMAIN | \
	 DMA_HANDOFF_REQUESTER_BME_CLEAR | DMA_HANDOFF_REQUESTER_ARENA_ONLY)

#define DMA_HANDOFF_ARENA_READ      (1U << 0)
#define DMA_HANDOFF_ARENA_WRITE     (1U << 1)
#define DMA_HANDOFF_ARENA_PREMAPPED (1U << 2)
#define DMA_HANDOFF_ARENA_IMMUTABLE (1U << 3)
#define DMA_HANDOFF_ARENA_COHERENT  (1U << 4)
#define DMA_HANDOFF_ARENA_FLAGS \
	(DMA_HANDOFF_ARENA_READ | DMA_HANDOFF_ARENA_WRITE | \
	 DMA_HANDOFF_ARENA_PREMAPPED | DMA_HANDOFF_ARENA_IMMUTABLE | \
	 DMA_HANDOFF_ARENA_COHERENT)

struct dma_handoff_header {
	uint32_t revision;
	uint32_t size;
	uint16_t header_size;
	uint16_t requester_size;
	uint16_t flags;
	uint16_t granule_shift;
	uint32_t requester_count;
	uint32_t crc32;
	uint64_t generation;
	uint32_t reserved[2];
} __packed;

struct dma_handoff_requester {
	uint16_t segment;
	uint16_t bdf;
	uint16_t protection_domain;
	uint16_t flags;
	uint64_t arena_cpu_base;
	uint64_t arena_device_base;
	uint32_t arena_pages;
	uint32_t arena_flags;
} __packed;

_Static_assert(sizeof(struct dma_handoff_header) == 40,
	"unexpected DMA handoff header size");
_Static_assert(sizeof(struct dma_handoff_requester) == 32,
	"unexpected DMA requester size");

#endif
