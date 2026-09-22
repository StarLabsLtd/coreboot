/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_AMD_CEZANNE_DMA_POLICY_H
#define SOC_AMD_CEZANNE_DMA_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define CEZANNE_DMA_POLICY_MAX_CONTROLLERS 512U

enum cezanne_dma_boot_controller {
	CEZANNE_DMA_BOOT_NONE,
	CEZANNE_DMA_BOOT_NVME,
	CEZANNE_DMA_BOOT_XHCI,
	CEZANNE_DMA_BOOT_AHCI,
};

struct cezanne_dma_policy_entry {
	const void *device;
	uint16_t priority;
	uint32_t arena_pages;
};

struct cezanne_dma_policy {
	struct cezanne_dma_policy_entry entry[CEZANNE_DMA_POLICY_MAX_CONTROLLERS];
	size_t count;
	bool frozen;
};

enum cb_err cezanne_dma_policy_add(struct cezanne_dma_policy *policy,
	const void *device, uint32_t class_code,
	enum cezanne_dma_boot_controller controller, uint16_t priority,
	uint32_t *arena_pages);
bool cezanne_dma_policy_freeze(struct cezanne_dma_policy *policy);
bool cezanne_dma_policy_lookup(const struct cezanne_dma_policy *policy,
	const void *device, uint16_t *priority);

#endif
