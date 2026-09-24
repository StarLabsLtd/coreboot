/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_DMA_LIVE_PLATFORM_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_DMA_LIVE_PLATFORM_H

#include <commonlib/bsd/cb_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum cb_err starbook_mtl_dma_live_backend_ensure(void);
bool starbook_mtl_dma_live_backend_handoff(uintptr_t *address, size_t *bytes);

#endif
