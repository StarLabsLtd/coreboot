/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_EMULATION_QEMU_Q35_DMA_COLD_H
#define MAINBOARD_EMULATION_QEMU_Q35_DMA_COLD_H

#include <stdbool.h>

/* Destructive early-boot operations, never a live-driver or runtime service. */
bool q35_dma_cold_quiesce(void);
/* Rechecks current owned geometry/tables after controller retirement. */
bool q35_dma_cold_current(void);
/* Destructive only under the actual held E8 wave and irreversible RAM window. */
bool q35_capsule_ram_dma_current(void);

#endif
