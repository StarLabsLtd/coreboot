/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_AMD_CEZANNE_DMA_HANDOFF_H
#define SOC_AMD_CEZANNE_DMA_HANDOFF_H

#include <soc/dma_policy.h>
#include <stdint.h>

struct device;

/* Return a type only for an exact controller selected by board boot policy. */
enum cezanne_dma_boot_controller mainboard_cezanne_dma_boot_controller(
	const struct device *device, uint16_t *priority);

#endif
