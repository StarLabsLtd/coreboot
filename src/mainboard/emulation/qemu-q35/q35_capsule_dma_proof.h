/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_EMULATION_QEMU_Q35_CAPSULE_DMA_PROOF_H
#define MAINBOARD_EMULATION_QEMU_Q35_CAPSULE_DMA_PROOF_H

#include <stdbool.h>
#include <stdint.h>

bool q35_capsule_dma_protected(void *context, uint64_t base, uint64_t size);

#endif
