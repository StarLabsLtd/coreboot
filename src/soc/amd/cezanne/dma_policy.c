/* SPDX-License-Identifier: GPL-2.0-only */

#include <soc/dma_policy.h>

#define CEZANNE_NVME_ARENA_PAGES 32U
#define CEZANNE_XHCI_ARENA_PAGES 128U
#define CEZANNE_AHCI_ARENA_PAGES 32U

#define PCI_CLASS_NVME 0x010802U
#define PCI_CLASS_XHCI 0x0c0330U
#define PCI_CLASS_AHCI 0x010601U

static bool controller_arena(uint32_t class_code,
	enum cezanne_dma_boot_controller controller, uint32_t *arena_pages)
{
	if (!arena_pages)
		return false;

	switch (controller) {
	case CEZANNE_DMA_BOOT_NVME:
		*arena_pages = CEZANNE_NVME_ARENA_PAGES;
		return class_code == PCI_CLASS_NVME;
	case CEZANNE_DMA_BOOT_XHCI:
		*arena_pages = CEZANNE_XHCI_ARENA_PAGES;
		return class_code == PCI_CLASS_XHCI;
	case CEZANNE_DMA_BOOT_AHCI:
		*arena_pages = CEZANNE_AHCI_ARENA_PAGES;
		return class_code == PCI_CLASS_AHCI;
	case CEZANNE_DMA_BOOT_NONE:
	default:
		return false;
	}
}

enum cb_err cezanne_dma_policy_add(struct cezanne_dma_policy *policy,
	const void *device, uint32_t class_code,
	enum cezanne_dma_boot_controller controller, uint16_t priority,
	uint32_t *arena_pages)
{
	uint32_t pages;

	if (!policy || policy->frozen || !device || !priority ||
	    policy->count >= CEZANNE_DMA_POLICY_MAX_CONTROLLERS ||
	    !controller_arena(class_code, controller, &pages))
		return CB_ERR_ARG;
	for (size_t index = 0; index < policy->count; index++) {
		if (policy->entry[index].device == device ||
		    policy->entry[index].priority == priority)
			return CB_ERR_ARG;
	}
	policy->entry[policy->count++] = (struct cezanne_dma_policy_entry) {
		.device = device,
		.priority = priority,
		.arena_pages = pages,
	};
	if (arena_pages)
		*arena_pages = pages;
	return CB_SUCCESS;
}

bool cezanne_dma_policy_freeze(struct cezanne_dma_policy *policy)
{
	if (!policy || policy->frozen || !policy->count)
		return false;
	policy->frozen = true;
	return true;
}

bool cezanne_dma_policy_lookup(const struct cezanne_dma_policy *policy,
	const void *device, uint16_t *priority)
{
	if (!policy || !policy->frozen || !device || !priority)
		return false;
	for (size_t index = 0; index < policy->count; index++) {
		if (policy->entry[index].device == device) {
			*priority = policy->entry[index].priority;
			return true;
		}
	}
	return false;
}
