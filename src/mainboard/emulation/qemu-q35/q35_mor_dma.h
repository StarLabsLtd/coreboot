/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_EMULATION_QEMU_Q35_MOR_DMA_H
#define MAINBOARD_EMULATION_QEMU_Q35_MOR_DMA_H

#include <boot/payload_mm_authvar_mor_clear_executor.h>

bool q35_mor_dma_pre_device_guard_valid(void);
enum cb_err q35_mor_dma_snapshot(
	struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot);

#endif
