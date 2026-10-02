/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_EMULATION_QEMU_Q35_DMA_CONTROLLER_CANCEL_H
#define MAINBOARD_EMULATION_QEMU_Q35_DMA_CONTROLLER_CANCEL_H

#include <stdbool.h>
#include <stdint.h>

struct q35_dma_controller_io {
	void *context;
	uint32_t (*read32)(void *context, uint32_t offset);
	void (*write32)(void *context, uint32_t offset, uint32_t value);
	void (*delay_us)(void *context, uint32_t microseconds);
};

/*
 * Destructive cold-controller operations, not a DMA authority decision.
 * The caller must own the actual BAR and early execution window; these
 * operations must never reset a live firmware or OS driver's queues.
 */
bool q35_dma_nvme_cancel(const struct q35_dma_controller_io *io);
bool q35_dma_xhci_cancel(const struct q35_dma_controller_io *io);

#endif
