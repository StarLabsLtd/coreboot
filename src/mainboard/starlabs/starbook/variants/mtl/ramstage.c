/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <bootstate.h>
#include <common/fsp_params.h>
#include <common/pin_mux.h>
#include <console/console.h>
#include <device/pci.h>
#include <intelblocks/vtd.h>
#include <option.h>
#include <soc/pci_devs.h>
#include <soc/ramstage.h>

static void quiesce_pci_dma(void *unused)
{
	const struct device *dev;

	if (!CONFIG(ENABLE_EARLY_DMA_PROTECTION) || !get_uint_option("vtd", 1))
		return;

	for (dev = all_devices; dev; dev = dev->next) {
		if (dev->path.type != DEVICE_PATH_PCI || !is_dev_enabled(dev))
			continue;

		pci_dev_disable_bus_master(dev);
	}
}

BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY, quiesce_pci_dma, NULL);

void lb_board(struct lb_header *header)
{
	struct lb_range *dma;
	size_t dma_size;
	void *dma_buffer;

	if (!CONFIG(ENABLE_EARLY_DMA_PROTECTION) || !get_uint_option("vtd", 1))
		return;

	dma_buffer = vtd_get_dma_buffer(&dma_size);
	if (!dma_buffer || !dma_size || dma_size > UINT32_MAX) {
		printk(BIOS_ERR, "DMA protection buffer is unavailable\n");
		return;
	}

	dma = (struct lb_range *)lb_new_record(header);
	dma->tag = LB_TAG_DMA;
	dma->size = sizeof(*dma);
	dma->range_start = (uintptr_t)dma_buffer;
	dma->range_size = dma_size;
}

void mainboard_silicon_init_params(FSP_S_CONFIG *supd)
{
	configure_pin_mux(supd);
	starlabs_update_fsp_s_policy(supd);
	supd->TcNotifyIgd = 2; // Auto

	if (!CONFIG(ENABLE_EARLY_DMA_PROTECTION) || !get_uint_option("vtd", 1))
		return;

	/* Make ACS visible on every PCH root port that FSP exposes. */
	for (size_t i = 0; i < ARRAY_SIZE(supd->PcieRpAcsEnabled); i++)
		supd->PcieRpAcsEnabled[i] = 1;

	/* Keep every USB4 PCIe ingress visible for payload ACS validation. */
	supd->ITbtPcieTunnelingForUsb4 = 1;
	for (size_t i = 0; i < ARRAY_SIZE(supd->ITbtPcieRootPortEn); i++)
		supd->ITbtPcieRootPortEn[i] = 1;
}
