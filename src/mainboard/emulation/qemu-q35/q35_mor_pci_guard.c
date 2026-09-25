/* SPDX-License-Identifier: GPL-2.0-only */

#include "q35_mor_pci_guard.h"

#include <device/pci_def.h>
#include <stdbool.h>

static bool valid(const struct q35_mor_pci_io *io, uintptr_t base,
	uint32_t buses)
{
	return io && io->read16 && io->write16 && base && buses && buses <= 256U &&
		buses <= (~(uintptr_t)0 - base) / (1U << 20);
}

static bool overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (!first || !second || !first_size || !second_size ||
	    first_base > ~(uintptr_t)0 - (first_size - 1U) ||
	    second_base > ~(uintptr_t)0 - (second_size - 1U))
		return true;
	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

enum identity_status {
	IDENTITY_ABSENT,
	IDENTITY_VALID,
	IDENTITY_INVALID,
};

static enum identity_status read_identity(const struct q35_mor_pci_io *io,
	uintptr_t config, uint16_t bdf, struct q35_mor_pci_requester *identity)
{
	const uint16_t vendor = io->read16(io->context, config + PCI_VENDOR_ID);
	const uint16_t vendor_repeated = io->read16(io->context,
		config + PCI_VENDOR_ID);
	const uint16_t device = io->read16(io->context, config + PCI_DEVICE_ID);
	const uint16_t device_repeated = io->read16(io->context,
		config + PCI_DEVICE_ID);

	*identity = (struct q35_mor_pci_requester) {
		.bdf = bdf,
		.vendor_id = vendor,
		.device_id = device,
	};
	if (vendor == UINT16_MAX && vendor_repeated == UINT16_MAX)
		return IDENTITY_ABSENT;
	if (vendor == UINT16_MAX || vendor != vendor_repeated ||
	    device != device_repeated)
		return IDENTITY_INVALID;
	return IDENTITY_VALID;
}

static bool identity_equal(const struct q35_mor_pci_requester *first,
	const struct q35_mor_pci_requester *second)
{
	return first->bdf == second->bdf &&
		first->vendor_id == second->vendor_id &&
		first->device_id == second->device_id;
}

enum cb_err q35_mor_pci_guard_capture(const struct q35_mor_pci_io *io,
	uintptr_t ecam_base, uint32_t buses, struct q35_mor_pci_requester *requesters,
	size_t capacity, size_t *requester_count)
{
	size_t count = 0;
	size_t seen = 0;
	bool safe = true;

	if (!valid(io, ecam_base, buses) || !requesters || !capacity ||
	    capacity > SIZE_MAX / sizeof(*requesters) || !requester_count ||
	    overlap(io, sizeof(*io), requesters, capacity * sizeof(*requesters)) ||
	    overlap(io, sizeof(*io), requester_count, sizeof(*requester_count)) ||
	    overlap(requesters, capacity * sizeof(*requesters), requester_count,
		sizeof(*requester_count)))
		return CB_ERR_ARG;
	*requester_count = 0;
	/* Never let an early readback failure or inventory overflow stop quiesce. */
	for (uint32_t bus = 0; bus < buses; bus++) {
		for (uint32_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
			const uintptr_t config = ecam_base + (bus << 20) + (devfn << 12);
			struct q35_mor_pci_requester identity;
			enum identity_status identity_status;
			uint16_t command;
			uint16_t repeated;
			uint16_t preserved;
			bool stable;

			identity_status = read_identity(io, config,
				(uint16_t)((bus << 8) | devfn), &identity);
			if (identity_status == IDENTITY_ABSENT)
				continue;
			stable = identity_status == IDENTITY_VALID;
			if (!stable)
				safe = false;
			command = io->read16(io->context, config + PCI_COMMAND);
			repeated = io->read16(io->context, config + PCI_COMMAND);
			if (command == UINT16_MAX || repeated == UINT16_MAX ||
			    command != repeated) {
				safe = false;
				stable = false;
			}
			preserved = command != UINT16_MAX ? command :
				(repeated != UINT16_MAX ? repeated : 0);
			io->write16(io->context, config + PCI_COMMAND,
				(uint16_t)(preserved & (uint16_t)~PCI_COMMAND_MASTER));
			command = io->read16(io->context, config + PCI_COMMAND);
			if (command == UINT16_MAX || (command & PCI_COMMAND_MASTER)) {
				safe = false;
				stable = false;
			}
			if (stable && count < capacity)
				requesters[count] = identity;
			else if (stable)
				safe = false;
			if (stable)
				count++;
		}
	}
	for (uint32_t bus = 0; bus < buses; bus++) {
		for (uint32_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
			const uintptr_t config = ecam_base + (bus << 20) + (devfn << 12);
			struct q35_mor_pci_requester identity;
			const enum identity_status identity_status = read_identity(io, config,
				(uint16_t)((bus << 8) | devfn), &identity);
			const uint16_t command = io->read16(io->context,
				config + PCI_COMMAND);
			const uint16_t command_repeated = io->read16(io->context,
				config + PCI_COMMAND);

			if (identity_status == IDENTITY_ABSENT)
				continue;
			if (identity_status != IDENTITY_VALID || command == UINT16_MAX ||
			    command != command_repeated ||
			    (command & PCI_COMMAND_MASTER))
				safe = false;
			if (seen >= count || seen >= capacity ||
			    !identity_equal(&requesters[seen], &identity))
				safe = false;
			seen++;
		}
	}
	if (!safe || !count || count > capacity || seen != count)
		return CB_ERR;
	*requester_count = count;
	return CB_SUCCESS;
}

enum cb_err q35_mor_pci_guard_validate(const struct q35_mor_pci_io *io,
	uintptr_t ecam_base, uint32_t buses,
	const struct q35_mor_pci_requester *requesters,
	size_t requester_count)
{
	size_t count = 0;
	bool unchanged = requester_count != 0;

	if (!valid(io, ecam_base, buses) || !requesters || !requester_count ||
	    requester_count > SIZE_MAX / sizeof(*requesters) ||
	    overlap(io, sizeof(*io), requesters,
		requester_count * sizeof(*requesters)))
		return CB_ERR_ARG;
	for (uint32_t bus = 0; bus < buses; bus++) {
		for (uint32_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
			const uintptr_t config = ecam_base + (bus << 20) + (devfn << 12);
			struct q35_mor_pci_requester identity;
			const enum identity_status identity_status = read_identity(io, config,
				(uint16_t)((bus << 8) | devfn), &identity);
			const uint16_t command = io->read16(io->context,
				config + PCI_COMMAND);
			const uint16_t command_repeated = io->read16(io->context,
				config + PCI_COMMAND);

			if (identity_status == IDENTITY_ABSENT)
				continue;
			if (identity_status != IDENTITY_VALID || command == UINT16_MAX ||
			    command != command_repeated || (command & PCI_COMMAND_MASTER) ||
			    count >= requester_count ||
			    !identity_equal(&requesters[count], &identity))
				unchanged = false;
			count++;
		}
	}
	return unchanged && count == requester_count ? CB_SUCCESS : CB_ERR;
}

static enum cb_err topology_matches(const struct q35_mor_pci_io *io,
	uintptr_t ecam_base, uint32_t buses,
	const struct q35_mor_pci_requester *requesters,
	size_t requester_count, bool require_quiesced)
{
	size_t count = 0;
	bool unchanged = requester_count != 0;

	for (uint32_t bus = 0; bus < buses; bus++) {
		for (uint32_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
			const uintptr_t config = ecam_base + (bus << 20) + (devfn << 12);
			struct q35_mor_pci_requester identity;
			const enum identity_status identity_status = read_identity(io, config,
				(uint16_t)((bus << 8) | devfn), &identity);
			const uint16_t command = io->read16(io->context,
				config + PCI_COMMAND);
			const uint16_t command_repeated = io->read16(io->context,
				config + PCI_COMMAND);

			if (identity_status == IDENTITY_ABSENT)
				continue;
			if (identity_status != IDENTITY_VALID || command == UINT16_MAX ||
			    command != command_repeated ||
			    (require_quiesced && (command & PCI_COMMAND_MASTER)) ||
			    count >= requester_count ||
			    !identity_equal(&requesters[count], &identity))
				unchanged = false;
			count++;
		}
	}
	return unchanged && count == requester_count ? CB_SUCCESS : CB_ERR;
}

enum cb_err q35_mor_pci_guard_requiesce(const struct q35_mor_pci_io *io,
	uintptr_t ecam_base, uint32_t buses,
	const struct q35_mor_pci_requester *requesters,
	size_t requester_count)
{
	bool safe = true;

	if (!valid(io, ecam_base, buses) || !requesters || !requester_count ||
	    requester_count > SIZE_MAX / sizeof(*requesters) ||
	    overlap(io, sizeof(*io), requesters,
		requester_count * sizeof(*requesters)))
		return CB_ERR_ARG;
	/* Do not write until a stable scan proves this is the retained topology. */
	if (topology_matches(io, ecam_base, buses, requesters, requester_count,
		false) != CB_SUCCESS)
		return CB_ERR;
	for (size_t index = 0; index < requester_count; index++) {
		const uint32_t bus = requesters[index].bdf >> 8;
		const uint32_t devfn = requesters[index].bdf & UINT8_MAX;
		const uintptr_t config = ecam_base + (bus << 20) + (devfn << 12);
		struct q35_mor_pci_requester identity;
		const enum identity_status identity_status = read_identity(io, config,
			requesters[index].bdf, &identity);
		const uint16_t command = io->read16(io->context,
			config + PCI_COMMAND);
		const uint16_t command_repeated = io->read16(io->context,
			config + PCI_COMMAND);
		uint16_t command_after;
		uint16_t command_after_repeated;
		uint16_t preserved = command;

		if (bus >= buses || identity_status != IDENTITY_VALID ||
		    !identity_equal(&requesters[index], &identity) ||
		    command == UINT16_MAX || command_repeated == UINT16_MAX ||
		    command != command_repeated)
			safe = false;
		if (preserved == UINT16_MAX)
			preserved = command_repeated != UINT16_MAX ?
				command_repeated : 0;
		io->write16(io->context, config + PCI_COMMAND,
			(uint16_t)(preserved & (uint16_t)~PCI_COMMAND_MASTER));
		command_after = io->read16(io->context, config + PCI_COMMAND);
		command_after_repeated = io->read16(io->context,
			config + PCI_COMMAND);
		if (command_after == UINT16_MAX ||
		    command_after != command_after_repeated ||
		    (command_after & PCI_COMMAND_MASTER))
			safe = false;
	}
	if (topology_matches(io, ecam_base, buses, requesters, requester_count,
		true) != CB_SUCCESS)
		safe = false;
	return safe ? CB_SUCCESS : CB_ERR;
}
