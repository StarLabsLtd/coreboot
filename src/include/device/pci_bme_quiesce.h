/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef DEVICE_PCI_BME_QUIESCE_H
#define DEVICE_PCI_BME_QUIESCE_H

#include <commonlib/bsd/cb_err.h>
#include <stdint.h>

#define PCI_BME_QUIESCE_MAX_FUNCTIONS 512U

struct pci_bme_quiesce_io {
	void *context;
	uint32_t (*read32)(void *context, uint8_t bus, uint8_t devfn, uint16_t offset);
	void (*write16)(void *context, uint8_t bus, uint8_t devfn, uint16_t offset,
		uint16_t value);
};
struct pci_bme_quiesce_function {
	uint16_t bdf;
	uint16_t vendor;
	uint16_t device;
	uint16_t command;
	uint32_t class;
};
struct pci_bme_quiesce_snapshot {
	uint16_t bus_count;
	uint16_t count;
	uint32_t failed;
	struct pci_bme_quiesce_function functions[PCI_BME_QUIESCE_MAX_FUNCTIONS];
};
enum cb_err pci_bme_quiesce(const struct pci_bme_quiesce_io *io,
	uint16_t bus_count, struct pci_bme_quiesce_snapshot *snapshot,
	struct pci_bme_quiesce_snapshot *workspace);
enum cb_err pci_bme_quiesce_revalidate(const struct pci_bme_quiesce_io *io,
	struct pci_bme_quiesce_snapshot *snapshot,
	struct pci_bme_quiesce_snapshot *workspace);
void pci_bme_quiesce_terminal(const struct pci_bme_quiesce_io *io,
	uint16_t bus_count);

#endif /* DEVICE_PCI_BME_QUIESCE_H */
