/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef TESTS_SOC_P2SB_PCI_MOCK_H
#define TESTS_SOC_P2SB_PCI_MOCK_H

#include <device/pci_type.h>
#include <stdint.h>

#define PCI_OPS_H
#define ENV_PCI_SIMPLE_DEVICE 1

uint8_t pci_s_read_config8(pci_devfn_t dev, uint16_t reg);
uint16_t pci_s_read_config16(pci_devfn_t dev, uint16_t reg);
uint32_t pci_s_read_config32(pci_devfn_t dev, uint16_t reg);
void pci_s_write_config8(pci_devfn_t dev, uint16_t reg, uint8_t value);
void pci_s_write_config16(pci_devfn_t dev, uint16_t reg, uint16_t value);
void pci_s_write_config32(pci_devfn_t dev, uint16_t reg, uint32_t value);

#define pci_read_config8   pci_s_read_config8
#define pci_read_config16  pci_s_read_config16
#define pci_read_config32  pci_s_read_config32
#define pci_write_config8  pci_s_write_config8
#define pci_write_config16 pci_s_write_config16
#define pci_write_config32 pci_s_write_config32

static inline void pci_or_config16(pci_devfn_t dev, uint16_t reg, uint16_t value)
{
	pci_write_config16(dev, reg, pci_read_config16(dev, reg) | value);
}

#endif
