/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <cpu/x86/smm.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <efi_variable_service.h>
#include <intelblocks/fast_spi.h>
#include <soc/pci_devs.h>

#include "hob.h"

static uintptr_t sealed_spi_bar;

const struct region_device *efi_variable_service_write_device(void)
{
	if (!fast_spi_wpd_status())
		return NULL;
	boot_device_init();
	return boot_device_rw();
}

bool efi_variable_service_spi_bar_valid(uintptr_t bar)
{
	/* Before trusted finalization, PCI resource assignment may still change BAR0. */
	return !sealed_spi_bar || bar == sealed_spi_bar;
}

int efi_variable_service_bind_spi(const struct efi_variable_service_state *service)
{
	struct hob_resource *resource;
	uintptr_t smram_base;
	size_t smram_size;
	uint32_t bar;
	uint32_t offset;

	if (service->spi_mmio_resource < service->hob_base)
		return -1;
	offset = service->spi_mmio_resource - service->hob_base;
	if (offset > service->hob_size || service->hob_size - offset < sizeof(*resource))
		return -1;

	resource = (void *)(uintptr_t)service->spi_mmio_resource;
	if (resource->header.type != HOB_RESOURCE_DESCRIPTOR ||
	    resource->header.length != sizeof(*resource) ||
	    resource->type != RESOURCE_MEMORY_MAPPED_IO || resource->size != 0x1000)
		return -1;

	bar = pci_read_config32(PCH_DEV_SPI, PCI_BASE_ADDRESS_0);
	if (bar & PCI_BASE_ADDRESS_SPACE_IO)
		return -1;
	bar &= ~0xfff;
	if (!bar)
		return -1;
	smm_region(&smram_base, &smram_size);
	const struct region spi_region = region_create(bar, 0x1000);
	const struct region smram_region = region_create(smram_base, smram_size);
	if (region_overlap(&spi_region, &smram_region))
		return -1;
	if (smm_efi_variable_service_is_s3_resume() && resource->base != bar)
		return -1;

	sealed_spi_bar = bar;
	resource->base = bar;
	return 0;
}

struct region efi_variable_service_bios_region(void)
{
	size_t size = 0;
	size_t offset = fast_spi_get_bios_region(&size);

	if (!size || offset >= CONFIG_ROM_SIZE || size > CONFIG_ROM_SIZE - offset)
		return (struct region) { 0 };
	return region_create(offset, size);
}
