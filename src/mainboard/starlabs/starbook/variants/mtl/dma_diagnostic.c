/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_diagnostic.h"

#define VTD_VERSION 0x00U
#define VTD_CAPABILITY 0x08U
#define VTD_EXTENDED_CAPABILITY 0x10U
#define VTD_GLOBAL_STATUS 0x1cU
#define VTD_ROOT_ADDRESS 0x20U
#define VTD_PROTECTED_MEMORY_ENABLE 0x64U
#define VTD_PROTECTED_LOW_BASE 0x68U
#define VTD_PROTECTED_LOW_LIMIT 0x6cU
#define VTD_PROTECTED_HIGH_BASE 0x70U
#define VTD_PROTECTED_HIGH_LIMIT 0x78U
#define PCI_VENDOR_ID 0x00U
#define PCI_COMMAND 0x04U
#define PCI_COMMAND_MASTER (1U << 2)

static uint64_t read_engine64(const struct starbook_mtl_dma_diagnostic_io *io,
	uint32_t offset)
{
	return io->read_engine32(io->context, offset) |
		((uint64_t)io->read_engine32(io->context, offset + 4U) << 32);
}

bool starbook_mtl_dma_collect_facts(
	const struct starbook_mtl_dma_diagnostic_io *io, uint16_t bus_count,
	const uint16_t requesters[STARBOOK_MTL_DMA_REQUESTER_COUNT],
	uint64_t dma_buffer_base, uint64_t dma_buffer_size,
	struct starbook_mtl_dma_facts *facts)
{
	if (!io || !io->read_engine32 || !io->read_pci16 || !bus_count ||
	    bus_count > 256U || !requesters || !facts ||
	    (dma_buffer_size && dma_buffer_base > UINT64_MAX - dma_buffer_size))
		return false;
	*facts = (struct starbook_mtl_dma_facts) {
		.version = io->read_engine32(io->context, VTD_VERSION),
		.capability = read_engine64(io, VTD_CAPABILITY),
		.extended_capability = read_engine64(io, VTD_EXTENDED_CAPABILITY),
		.status = io->read_engine32(io->context, VTD_GLOBAL_STATUS),
		.root = read_engine64(io, VTD_ROOT_ADDRESS),
		.protected_memory_enable = io->read_engine32(io->context,
			VTD_PROTECTED_MEMORY_ENABLE),
		.protected_low_base = io->read_engine32(io->context,
			VTD_PROTECTED_LOW_BASE),
		.protected_low_limit = io->read_engine32(io->context,
			VTD_PROTECTED_LOW_LIMIT),
		.protected_high_base = read_engine64(io, VTD_PROTECTED_HIGH_BASE),
		.protected_high_limit = read_engine64(io, VTD_PROTECTED_HIGH_LIMIT),
		.dma_buffer_base = dma_buffer_base,
		.dma_buffer_size = dma_buffer_size,
	};
	for (size_t index = 0; index < STARBOOK_MTL_DMA_REQUESTER_COUNT; index++) {
		if ((requesters[index] >> 8) >= bus_count)
			return false;
		for (size_t prior = 0; prior < index; prior++)
			if (requesters[index] == requesters[prior])
				return false;
		facts->requester_bdf[index] = requesters[index];
	}

	for (uint16_t bus = 0; bus < bus_count; bus++) {
		for (uint16_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
			const uint16_t vendor = io->read_pci16(io->context, bus, devfn,
				PCI_VENDOR_ID);
			uint16_t command;
			uint16_t bdf;

			if (vendor == UINT16_MAX)
				continue;
			command = io->read_pci16(io->context, bus, devfn, PCI_COMMAND);
			bdf = bus << 8 | devfn;
			facts->pci_function_count++;
			if (command & PCI_COMMAND_MASTER) {
				if (facts->bus_master_count >= STARBOOK_MTL_DMA_BME_MAX)
					return false;
				facts->bus_master_bdf[facts->bus_master_count++] = bdf;
			}
			for (size_t index = 0;
			     index < STARBOOK_MTL_DMA_REQUESTER_COUNT; index++) {
				if (facts->requester_bdf[index] != bdf)
					continue;
				facts->requester_present[index] = true;
				facts->requester_bus_master[index] =
					!!(command & PCI_COMMAND_MASTER);
			}
		}
	}
	return true;
}
