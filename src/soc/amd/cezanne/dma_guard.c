/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pci_def.h>
#include <soc/dma_guard.h>
#include <string.h>

static bool io_valid(const struct cezanne_dma_pci_io *io)
{
	return io && io->read_vendor && io->read_command && io->write_command;
}

static bool function_present(const struct cezanne_dma_pci_io *io, uint16_t bdf)
{
	const uint16_t vendor = io->read_vendor(io->context, bdf);

	return vendor != 0 && vendor != UINT16_MAX;
}

static void set_present(struct cezanne_dma_pci_snapshot *snapshot, uint16_t bdf)
{
	snapshot->present[bdf / 8U] |= 1U << (bdf % 8U);
}

static bool was_present(const struct cezanne_dma_pci_snapshot *snapshot, uint16_t bdf)
{
	return snapshot->present[bdf / 8U] & (1U << (bdf % 8U));
}

bool cezanne_dma_pci_quiesce(const struct cezanne_dma_pci_io *io,
	struct cezanne_dma_pci_snapshot *snapshot, uint32_t function_count)
{
	if (!io_valid(io) || !snapshot || !function_count ||
	    function_count > CEZANNE_DMA_PCI_FUNCTIONS)
		return false;

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->function_count = function_count;
	for (uint32_t bdf = 0; bdf < function_count; bdf++) {
		uint16_t command;

		if (!function_present(io, bdf))
			continue;
		set_present(snapshot, bdf);
		command = io->read_command(io->context, bdf);
		io->write_command(io->context, bdf, command & ~PCI_COMMAND_MASTER);
		if (io->read_command(io->context, bdf) & PCI_COMMAND_MASTER)
			return false;
	}
	snapshot->valid = true;
	return cezanne_dma_pci_quiescence_held(io, snapshot);
}

bool cezanne_dma_pci_quiescence_held(const struct cezanne_dma_pci_io *io,
	const struct cezanne_dma_pci_snapshot *snapshot)
{
	if (!io_valid(io) || !snapshot || !snapshot->valid ||
	    !snapshot->function_count ||
	    snapshot->function_count > CEZANNE_DMA_PCI_FUNCTIONS)
		return false;

	for (uint32_t bdf = 0; bdf < snapshot->function_count; bdf++) {
		const bool present = function_present(io, bdf);

		if (present != was_present(snapshot, bdf) ||
		    (present && (io->read_command(io->context, bdf) & PCI_COMMAND_MASTER)))
			return false;
	}
	return true;
}
