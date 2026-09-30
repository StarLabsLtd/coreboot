/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_DMA_LIVE_MIRROR_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_DMA_LIVE_MIRROR_H

#include <stddef.h>
#include <stdint.h>

void *starbook_mtl_dma_live_mirror_acquire(size_t size, uint32_t lifecycle);

#endif
