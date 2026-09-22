/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <device/device.h>
#include <device/mmio.h>
#include <intelblocks/vtd.h>
#include <soc/vtd.h>

#include "dma_diagnostic.h"
#include "payload_resource_policy.h"

struct diagnostic_context {
	uintptr_t vtd_base;
};

static uint32_t diagnostic_read_engine32(void *context, uint32_t offset)
{
	const struct diagnostic_context *diagnostic = context;

	return read32p(diagnostic->vtd_base + offset);
}

static uint16_t diagnostic_read_pci16(void *context, uint8_t bus,
	uint8_t devfn, uint16_t offset)
{
	const uintptr_t address = CONFIG_ECAM_MMCONF_BASE_ADDRESS +
		((uintptr_t)bus << 20) + ((uintptr_t)devfn << 12) + offset;

	(void)context;
	return read16p(address);
}

static void collect_dma_diagnostic(void *unused)
{
	static const enum starbook_mtl_boot_controller kinds[] = {
		STARBOOK_MTL_BOOT_CONTROLLER_NVME,
		STARBOOK_MTL_BOOT_CONTROLLER_PCH_XHCI,
		STARBOOK_MTL_BOOT_CONTROLLER_TCSS_XHCI,
	};
	struct diagnostic_context context = { .vtd_base = soc_vtd_iop_base() };
	const struct starbook_mtl_dma_diagnostic_io io = {
		.context = &context,
		.read_engine32 = diagnostic_read_engine32,
		.read_pci16 = diagnostic_read_pci16,
	};
	struct starbook_mtl_dma_facts facts;
	uint16_t requesters[ARRAY_SIZE(kinds)];
	size_t dma_size;
	void *dma_buffer;

	(void)unused;
	if (!starbook_mtl_boot_controller_inventory()) {
		printk(BIOS_ERR, "StarBook MTL DMA diagnostic: requester inventory invalid\n");
		return;
	}
	for (size_t index = 0; index < ARRAY_SIZE(kinds); index++) {
		const struct device *device =
			starbook_mtl_boot_controller_device(kinds[index]);

		if (!device || !device->upstream ||
		    device->upstream->segment_group != 0 ||
		    device->upstream->secondary > UINT8_MAX ||
		    device->path.pci.devfn > UINT8_MAX) {
			printk(BIOS_ERR,
			       "StarBook MTL DMA diagnostic: requester %zu has no exact BDF\n",
			       index);
			return;
		}
		requesters[index] = (uint16_t)device->upstream->secondary << 8 |
			device->path.pci.devfn;
	}
	dma_buffer = vtd_get_dma_buffer(&dma_size);
	if (!starbook_mtl_dma_collect_facts(&io, CONFIG_ECAM_MMCONF_BUS_NUMBER,
		requesters, (uintptr_t)dma_buffer, dma_size, &facts)) {
		printk(BIOS_ERR, "StarBook MTL DMA diagnostic: fact collection failed\n");
		return;
	}
	printk(BIOS_INFO,
	       "StarBook MTL DMA diagnostic: VTVC0=%#lx VER=%#x CAP=%#llx ECAP=%#llx GSTS=%#x RTADDR=%#llx PMEN=%#x\n",
	       context.vtd_base, facts.version,
	       (unsigned long long)facts.capability,
	       (unsigned long long)facts.extended_capability, facts.status,
	       (unsigned long long)facts.root, facts.protected_memory_enable);
	printk(BIOS_INFO,
	       "StarBook MTL DMA diagnostic: PMR low=%#x-%#x high=%#llx-%#llx FSP-DMA=%#llx/%#llx PCI=%u BME=%u\n",
	       facts.protected_low_base, facts.protected_low_limit,
	       (unsigned long long)facts.protected_high_base,
	       (unsigned long long)facts.protected_high_limit,
	       (unsigned long long)facts.dma_buffer_base,
	       (unsigned long long)facts.dma_buffer_size,
	       facts.pci_function_count, facts.bus_master_count);
	for (size_t index = 0; index < ARRAY_SIZE(kinds); index++)
		printk(BIOS_INFO,
		       "StarBook MTL DMA diagnostic: requester[%zu]=0000:%02x:%02x.%u present=%u BME=%u\n",
		       index, facts.requester_bdf[index] >> 8,
		       (facts.requester_bdf[index] >> 3) & 0x1f,
		       facts.requester_bdf[index] & 7,
		       facts.requester_present[index],
		       facts.requester_bus_master[index]);
	for (size_t index = 0; index < facts.bus_master_count; index++)
		printk(BIOS_INFO,
		       "StarBook MTL DMA diagnostic: BME[%zu]=0000:%02x:%02x.%u\n",
		       index, facts.bus_master_bdf[index] >> 8,
		       (facts.bus_master_bdf[index] >> 3) & 0x1f,
		       facts.bus_master_bdf[index] & 7);
}

BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_ENTRY, collect_dma_diagnostic, NULL);
