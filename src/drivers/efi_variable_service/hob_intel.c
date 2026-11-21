/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbmem.h>
#include <cpu/x86/smm.h>
#include <device/pci_def.h>
#include <device/pci_mmio_cfg.h>
#include <device/pci_ops.h>
#include <soc/iomap.h>
#include <soc/pci_devs.h>
#include <soc/pm.h>

#include "hob.h"

int efi_variable_service_get_platform_info(struct efi_variable_service_platform *info)
{
	uintptr_t smram_base;
	size_t smram_size;
	uintptr_t end = (uintptr_t)cbmem_top();

	/* SMM setup precedes PCI resource enumeration. */
	smm_region(&smram_base, &smram_size);
	if (!smram_size || end > smram_base || end <= 1 * MiB)
		return -1;
	*info = (struct efi_variable_service_platform) {
		.runtime_ram = region_create(1 * MiB, end - 1 * MiB),
		.spi_config = (uintptr_t)pci_mmio_config32_addr(PCI_BDF(PCH_DEV_SPI), 0),
		.spi_flags = CONFIG(HAVE_INSMM_STS) ? SPI_DISABLE_SMM_WRITE_PROTECT : 0,
		.pm_timer = ACPI_BASE_ADDRESS + PM1_TMR,
	};
	return 0;
}
