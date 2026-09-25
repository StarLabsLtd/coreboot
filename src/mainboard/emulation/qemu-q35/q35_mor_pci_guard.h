/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_EMULATION_QEMU_Q35_MOR_PCI_GUARD_H
#define MAINBOARD_EMULATION_QEMU_Q35_MOR_PCI_GUARD_H

#include <commonlib/bsd/cb_err.h>
#include <stddef.h>
#include <stdint.h>

struct q35_mor_pci_io {
	void *context;
	uint16_t (*read16)(void *context, uintptr_t address);
	void (*write16)(void *context, uintptr_t address, uint16_t value);
};

struct q35_mor_pci_requester {
	uint16_t bdf;
	uint16_t vendor_id;
	uint16_t device_id;
};

enum cb_err q35_mor_pci_guard_capture(const struct q35_mor_pci_io *io,
	uintptr_t ecam_base, uint32_t buses, struct q35_mor_pci_requester *requesters,
	size_t capacity, size_t *requester_count);

enum cb_err q35_mor_pci_guard_validate(const struct q35_mor_pci_io *io,
	uintptr_t ecam_base, uint32_t buses,
	const struct q35_mor_pci_requester *requesters,
	size_t requester_count);

enum cb_err q35_mor_pci_guard_requiesce(const struct q35_mor_pci_io *io,
	uintptr_t ecam_base, uint32_t buses,
	const struct q35_mor_pci_requester *requesters,
	size_t requester_count);

#endif
