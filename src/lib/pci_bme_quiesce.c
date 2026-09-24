/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pci_bme_quiesce.h>
#include <stdbool.h>
#include <string.h>

#define PCI_VENDOR_DEVICE 0x00U
#define PCI_COMMAND 0x04U
#define PCI_CLASS_REVISION 0x08U
#define PCI_COMMAND_MASTER (1U << 2)

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && !(base % alignment) && base <= (uintptr_t)-1 - (size - 1U);
}

static bool objects_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

static bool io_valid(const struct pci_bme_quiesce_io *io)
{
	return io && io->read32 && io->write16;
}

static bool snapshot_is_initial(const struct pci_bme_quiesce_snapshot *snapshot)
{
	if (snapshot->bus_count || snapshot->count || snapshot->failed != 1)
		return false;
	for (size_t index = 0; index < PCI_BME_QUIESCE_MAX_FUNCTIONS; index++)
		if (snapshot->functions[index].bdf ||
		    snapshot->functions[index].vendor ||
		    snapshot->functions[index].device ||
		    snapshot->functions[index].command ||
		    snapshot->functions[index].class)
			return false;
	return true;
}

static enum cb_err observe(const struct pci_bme_quiesce_io *io,
	uint16_t bus_count, struct pci_bme_quiesce_snapshot *snapshot)
{
	size_t count = 0;
	bool failed = false;

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->failed = 1;
	for (uint16_t bus = 0; bus < bus_count; bus++)
		for (uint16_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
			const uint32_t id = io->read32(io->context, bus, devfn,
				PCI_VENDOR_DEVICE);
			uint32_t class;
			uint16_t command;
			uint16_t cleared_command;

			if ((uint16_t)id == UINT16_MAX)
				continue;
			class = io->read32(io->context, bus, devfn,
				PCI_CLASS_REVISION) >> 8;
			command = io->read32(io->context, bus, devfn, PCI_COMMAND);
			cleared_command = command & ~PCI_COMMAND_MASTER;
			if (command & PCI_COMMAND_MASTER) {
				io->write16(io->context, bus, devfn, PCI_COMMAND,
					cleared_command);
				if ((uint16_t)io->read32(io->context, bus, devfn,
					PCI_COMMAND) != cleared_command)
					failed = true;
			}
			if (count < PCI_BME_QUIESCE_MAX_FUNCTIONS)
				snapshot->functions[count] =
					(struct pci_bme_quiesce_function) {
						.bdf = bus << 8 | devfn,
						.vendor = id,
						.device = id >> 16,
						.command = cleared_command,
						.class = class,
					};
			else
				failed = true;
			count++;
		}
	if (!count || count > PCI_BME_QUIESCE_MAX_FUNCTIONS)
		failed = true;
	snapshot->bus_count = bus_count;
	snapshot->count = count <= UINT16_MAX ? count : UINT16_MAX;
	snapshot->failed = failed;
	return failed ? CB_ERR : CB_SUCCESS;
}

static enum cb_err compare_live(const struct pci_bme_quiesce_io *io,
	const struct pci_bme_quiesce_snapshot *expected)
{
	const uint16_t bus_count = expected->bus_count;
	const uint16_t expected_count = expected->count;
	size_t count = 0;
	bool failed = false;

	for (uint16_t bus = 0; bus < bus_count; bus++)
		for (uint16_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
			struct pci_bme_quiesce_function function = { 0 };
			const uint16_t bdf = bus << 8 | devfn;
			const bool expected_here = count < expected_count &&
				expected->functions[count].bdf == bdf;
			const struct pci_bme_quiesce_function saved = expected_here ?
				expected->functions[count] : function;
			const uint32_t id = io->read32(io->context, bus, devfn,
				PCI_VENDOR_DEVICE);

			if ((uint16_t)id == UINT16_MAX) {
				if (expected_here)
					failed = true;
				continue;
			}
			function = (struct pci_bme_quiesce_function) {
				.bdf = bdf,
				.vendor = id,
				.device = id >> 16,
				.command = io->read32(io->context, bus, devfn,
					PCI_COMMAND),
				.class = io->read32(io->context, bus, devfn,
					PCI_CLASS_REVISION) >> 8,
			};
			if (!expected_here || memcmp(&function, &saved, sizeof(function)))
				failed = true;
			count++;
		}
	if (count != expected_count)
		failed = true;
	return failed ? CB_ERR : CB_SUCCESS;
}

static bool arguments_valid(const struct pci_bme_quiesce_io *io,
	const struct pci_bme_quiesce_snapshot *snapshot,
	const struct pci_bme_quiesce_snapshot *workspace)
{
	return object_valid(io, sizeof(*io), _Alignof(*io)) &&
		object_valid(workspace, sizeof(*workspace), _Alignof(*workspace)) &&
		!objects_overlap(io, sizeof(*io), snapshot, sizeof(*snapshot)) &&
		!objects_overlap(io, sizeof(*io), workspace, sizeof(*workspace)) &&
		!objects_overlap(snapshot, sizeof(*snapshot), workspace,
			sizeof(*workspace)) &&
		(!io->context ||
		 (!objects_overlap(io->context, 1, snapshot, sizeof(*snapshot)) &&
		  !objects_overlap(io->context, 1, workspace, sizeof(*workspace)))) &&
		io_valid(io);
}

enum cb_err pci_bme_quiesce(const struct pci_bme_quiesce_io *io,
	uint16_t bus_count, struct pci_bme_quiesce_snapshot *snapshot,
	struct pci_bme_quiesce_snapshot *workspace)
{
	struct pci_bme_quiesce_io ops;

	if (!object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)))
		return CB_ERR_ARG;
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->failed = 1;
	if (!arguments_valid(io, snapshot, workspace) ||
	    !bus_count || bus_count > 256U)
		return CB_ERR_ARG;
	ops = *io;
	if (observe(&ops, bus_count, workspace) != CB_SUCCESS ||
	    !snapshot_is_initial(snapshot) || memcmp(&ops, io, sizeof(ops)))
		return CB_ERR;
	memcpy(snapshot, workspace, sizeof(*snapshot));
	return pci_bme_quiesce_revalidate(io, snapshot, workspace);
}

enum cb_err pci_bme_quiesce_revalidate(const struct pci_bme_quiesce_io *io,
	struct pci_bme_quiesce_snapshot *snapshot,
	struct pci_bme_quiesce_snapshot *workspace)
{
	struct pci_bme_quiesce_io ops;
	enum cb_err result;

	if (!object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)))
		return CB_ERR_ARG;
	if (!arguments_valid(io, snapshot, workspace)) {
		snapshot->failed = 1;
		return CB_ERR_ARG;
	}
	if (snapshot->failed || !snapshot->bus_count ||
	    snapshot->bus_count > 256U || !snapshot->count ||
	    snapshot->count > PCI_BME_QUIESCE_MAX_FUNCTIONS) {
		snapshot->failed = 1;
		return CB_ERR_ARG;
	}
	memcpy(workspace, snapshot, sizeof(*workspace));
	ops = *io;
	result = compare_live(&ops, workspace);
	if (result != CB_SUCCESS ||
	    memcmp(snapshot, workspace, sizeof(*snapshot)) ||
	    memcmp(&ops, io, sizeof(ops))) {
		snapshot->failed = 1;
		return CB_ERR;
	}
	return CB_SUCCESS;
}

void pci_bme_quiesce_terminal(const struct pci_bme_quiesce_io *io,
	uint16_t bus_count)
{
	struct pci_bme_quiesce_io ops;

	if (!object_valid(io, sizeof(*io), _Alignof(*io)) || !io_valid(io) ||
	    !bus_count || bus_count > 256U)
		return;
	ops = *io;
	/* Retry once so a requester which ignored the first clear stays disabled. */
	for (unsigned int pass = 0; pass < 2; pass++)
		for (uint16_t bus = 0; bus < bus_count; bus++)
			for (uint16_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
				const uint32_t id = ops.read32(ops.context, bus, devfn,
					PCI_VENDOR_DEVICE);
				uint16_t command;

				if ((uint16_t)id == UINT16_MAX)
					continue;
				command = ops.read32(ops.context, bus, devfn, PCI_COMMAND);
				ops.write16(ops.context, bus, devfn, PCI_COMMAND,
					command & ~PCI_COMMAND_MASTER);
				(void)ops.read32(ops.context, bus, devfn, PCI_COMMAND);
			}
}
