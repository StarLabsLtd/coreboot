/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pci_def.h>
#include <soc/dma_guard.h>
#include <string.h>

static bool io_valid(const struct cezanne_dma_pci_io *io)
{
	return io && io->read_vendor && io->read_device && io->read_class_revision &&
		io->read_command && io->write_command;
}

static bool read_identity(const struct cezanne_dma_pci_io *io, uint16_t bdf,
	struct cezanne_dma_pci_identity *identity)
{
	const uint16_t vendor = io->read_vendor(io->context, bdf);

	if (vendor == 0 || vendor == UINT16_MAX)
		return false;
	*identity = (struct cezanne_dma_pci_identity) {
		.bdf = bdf,
		.vendor = vendor,
		.device = io->read_device(io->context, bdf),
		.class_revision = io->read_class_revision(io->context, bdf),
	};
	return true;
}

static bool identity_equal(const struct cezanne_dma_pci_identity *first,
	const struct cezanne_dma_pci_identity *second)
{
	return first->bdf == second->bdf && first->vendor == second->vendor &&
		first->device == second->device &&
		first->class_revision == second->class_revision;
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
		struct cezanne_dma_pci_identity identity;
		uint16_t command;

		if (!read_identity(io, bdf, &identity))
			continue;
		if (snapshot->identity_count >= CEZANNE_DMA_PCI_SNAPSHOT_MAX)
			return false;
		snapshot->identity[snapshot->identity_count++] = identity;
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

	size_t index = 0;
	for (uint32_t bdf = 0; bdf < snapshot->function_count; bdf++) {
		struct cezanne_dma_pci_identity identity;

		if (!read_identity(io, bdf, &identity))
			continue;
		if (index >= snapshot->identity_count ||
		    !identity_equal(&identity, &snapshot->identity[index]) ||
		    (io->read_command(io->context, bdf) & PCI_COMMAND_MASTER))
			return false;
		index++;
	}
	return index == snapshot->identity_count;
}
