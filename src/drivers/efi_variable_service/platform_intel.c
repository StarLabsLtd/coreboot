/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <cpu/x86/smm.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <efi_variable_service.h>
#include <fmap_config.h>
#include <intelblocks/fast_spi.h>
#include <soc/pci_devs.h>

#include "capsule_store.h"

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

int efi_variable_service_bind_spi(struct efi_variable_service_state *service)
{
	uintptr_t smram_base;
	size_t smram_size;
	uint32_t bar;

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
	if (smm_efi_variable_service_is_s3_resume() && service->spi_bar != bar)
		return -1;

	sealed_spi_bar = bar;
	service->spi_bar = bar;
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

int efi_variable_service_program_capsule(const struct region_device *flash, uint32_t offset,
			const struct efi_capsule_range *ranges, size_t count,
			uint8_t *image, uint8_t *readback, bool check_only)
{
	bool allow_erase = true;

	_Static_assert(FMAP_SECTION_SMMSTORE_START % EFI_CAPSULE_BLOCK_SIZE == 0 &&
		       FMAP_SECTION_SMMSTORE_SIZE % EFI_CAPSULE_BLOCK_SIZE == 0,
		       "Authenticated variable store must occupy whole capsule erase blocks");
	/* Neither a signed BIOS fallback nor a public command may replace live keys/FTW. */
	if (offset >= FMAP_SECTION_SMMSTORE_START &&
	    offset < FMAP_SECTION_SMMSTORE_START + FMAP_SECTION_SMMSTORE_SIZE)
		return 0;
#if CONFIG(SMMSTORE_SRK_PIN)
	const size_t fmap_start = ALIGN_DOWN(FMAP_SECTION_FMAP_START, EFI_CAPSULE_BLOCK_SIZE);
	const size_t fmap_end = ALIGN_UP(FMAP_SECTION_FMAP_START + FMAP_SECTION_FMAP_SIZE,
				       EFI_CAPSULE_BLOCK_SIZE);
	/* The enrolled pin is platform state, never capsule-owned image data. */
	if (offset < FMAP_SECTION_SRK_PIN_V1_START + FMAP_SECTION_SRK_PIN_V1_SIZE &&
	    FMAP_SECTION_SRK_PIN_V1_START < (uint64_t)offset + EFI_CAPSULE_BLOCK_SIZE)
		return 0;
	if (offset < fmap_end && fmap_start < (uint64_t)offset + EFI_CAPSULE_BLOCK_SIZE)
		allow_erase = false;
#endif
	/* Preflight immutable blocks before the first destructive operation. */
	if (check_only && allow_erase)
		return 0;
	return efi_capsule_program_block(flash, offset, EFI_CAPSULE_BLOCK_SIZE,
					ranges, count, image, readback, allow_erase) ?
		-1 : 0;
}
