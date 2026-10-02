/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_EMULATION_QEMU_Q35_DMA_TABLE_IMAGE_H
#define MAINBOARD_EMULATION_QEMU_Q35_DMA_TABLE_IMAGE_H

#include "q35_dma_policy.h"

/* The caller must separately establish ownership, DMA quiescence and coherency. */
bool q35_dma_table_image_valid(const void *table, size_t table_size,
	uintptr_t arena, size_t arena_size,
	const struct q35_capsule_dma_geometry *targets);

#endif
