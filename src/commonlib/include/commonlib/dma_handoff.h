/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef COMMONLIB_DMA_HANDOFF_H
#define COMMONLIB_DMA_HANDOFF_H

#include <commonlib/bsd/compiler.h>
#include <stdint.h>

#define DMA_HANDOFF_REVISION 1U
#define DMA_HANDOFF_MAX_REQUESTERS 2U
#define DMA_HANDOFF_MAX_TABLES 5U

#define DMA_HANDOFF_PROTECTION_ACTIVE (1U << 0)
#define DMA_HANDOFF_DEFAULT_DENY       (1U << 1)
#define DMA_HANDOFF_BME_CLEAR          (1U << 2)
#define DMA_HANDOFF_TABLES_RESIDENT    (1U << 3)
#define DMA_HANDOFF_PRH_REVISION_4     (1U << 4)
#define DMA_HANDOFF_REQUIRED_FLAGS \
	(DMA_HANDOFF_PROTECTION_ACTIVE | DMA_HANDOFF_DEFAULT_DENY | \
	 DMA_HANDOFF_BME_CLEAR | DMA_HANDOFF_TABLES_RESIDENT | \
	 DMA_HANDOFF_PRH_REVISION_4)

#define DMA_HANDOFF_REQUESTER_DENY_DOMAIN (1U << 0)
#define DMA_HANDOFF_REQUESTER_BME_CLEAR   (1U << 1)
#define DMA_HANDOFF_REQUESTER_FLAGS \
	(DMA_HANDOFF_REQUESTER_DENY_DOMAIN | DMA_HANDOFF_REQUESTER_BME_CLEAR)

#define DMA_HANDOFF_TABLE_RESIDENT  (1U << 0)
#define DMA_HANDOFF_TABLE_IMMUTABLE (1U << 1)
#define DMA_HANDOFF_TABLE_FLAGS \
	(DMA_HANDOFF_TABLE_RESIDENT | DMA_HANDOFF_TABLE_IMMUTABLE)

#define DMA_HANDOFF_TABLE_GLOBAL    1U
#define DMA_HANDOFF_TABLE_BUS       2U
#define DMA_HANDOFF_TABLE_REQUESTER 3U

struct dma_handoff_header {
	uint32_t revision;
	uint32_t size;
	uint16_t header_size;
	uint16_t requester_size;
	uint16_t table_size;
	uint16_t flags;
	uint32_t requester_count;
	uint32_t table_count;
	uint64_t generation;
	uint32_t reserved[2];
} __packed;

struct dma_handoff_requester {
	uint16_t segment;
	uint16_t bdf;
	uint16_t domain;
	uint16_t flags;
	uint32_t root_table;
	uint32_t context_table;
	uint32_t hierarchy_table;
	uint32_t reserved;
	uint64_t generation;
} __packed;

struct dma_handoff_table {
	uint64_t base;
	uint32_t pages;
	uint16_t owner_type;
	uint16_t reserved0;
	uint32_t owner_id;
	uint32_t flags;
	uint32_t reserved1;
} __packed;

_Static_assert(sizeof(struct dma_handoff_header) == 40,
	"unexpected DMA handoff header size");
_Static_assert(sizeof(struct dma_handoff_requester) == 32,
	"unexpected DMA requester size");
_Static_assert(sizeof(struct dma_handoff_table) == 28,
	"unexpected DMA table size");

#endif
