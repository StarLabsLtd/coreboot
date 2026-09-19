/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_DMA_HANDOFF_H
#define BOOT_DMA_HANDOFF_H

#include <commonlib/dma_handoff.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

struct lb_header;

enum cb_err dma_handoff_build(void *buffer, size_t capacity,
	uint64_t generation,
	const struct dma_handoff_requester *requesters, size_t requester_count,
	const struct dma_handoff_table *tables, size_t table_count,
	size_t *written);
enum cb_err dma_handoff_validate(const void *buffer, size_t bytes);
enum cb_err lb_add_dma_handoff(struct lb_header *header);
bool payload_dma_handoff_blob(uintptr_t *address, size_t *bytes);

#endif
