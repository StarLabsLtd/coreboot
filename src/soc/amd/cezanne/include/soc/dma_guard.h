/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_AMD_CEZANNE_DMA_GUARD_H
#define SOC_AMD_CEZANNE_DMA_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CEZANNE_DMA_PCI_FUNCTIONS (1U << 16)
#define CEZANNE_DMA_PCI_SNAPSHOT_MAX 512U

struct cezanne_dma_pci_identity {
	uint16_t bdf;
	uint16_t vendor;
	uint16_t device;
	uint32_t class_revision;
};

struct cezanne_dma_pci_io {
	void *context;
	uint16_t (*read_vendor)(void *context, uint16_t bdf);
	uint16_t (*read_device)(void *context, uint16_t bdf);
	uint32_t (*read_class_revision)(void *context, uint16_t bdf);
	uint16_t (*read_command)(void *context, uint16_t bdf);
	void (*write_command)(void *context, uint16_t bdf, uint16_t command);
};

struct cezanne_dma_pci_snapshot {
	struct cezanne_dma_pci_identity identity[CEZANNE_DMA_PCI_SNAPSHOT_MAX];
	uint32_t function_count;
	size_t identity_count;
	bool valid;
};

bool cezanne_dma_pci_quiesce(const struct cezanne_dma_pci_io *io,
	struct cezanne_dma_pci_snapshot *snapshot, uint32_t function_count);
bool cezanne_dma_pci_quiescence_held(const struct cezanne_dma_pci_io *io,
	const struct cezanne_dma_pci_snapshot *snapshot);

#endif
