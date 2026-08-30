/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <bootstate.h>
#include <device/pci.h>
#include <halt.h>
#include <intelblocks/vtd.h>

static void quiesce_pci_dma(void *unused)
{
	const struct device *dev;

	for (dev = all_devices; dev; dev = dev->next) {
		uint16_t command;

		if (dev->path.type != DEVICE_PATH_PCI ||
		    pci_read_config16(dev, PCI_VENDOR_ID) == 0xffff)
			continue;

		pci_dev_disable_bus_master(dev);
		command = pci_read_config16(dev, PCI_COMMAND);
		if (command & PCI_COMMAND_MASTER)
			die("Failed to disable PCI bus mastering for %s\n", dev_path(dev));
	}
}

BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY, quiesce_pci_dma, NULL);

void lb_board(struct lb_header *header)
{
	struct lb_range *dma;
	size_t dma_size;
	void *dma_buffer;

	dma_buffer = vtd_get_dma_buffer(&dma_size);
	if (!dma_buffer || !dma_size || dma_size > UINT32_MAX)
		die("DMA protection buffer is unavailable\n");

	dma = (struct lb_range *)lb_new_record(header);
	dma->tag = LB_TAG_DMA;
	dma->size = sizeof(*dma);
	dma->range_start = (uintptr_t)dma_buffer;
	dma->range_size = dma_size;
}
