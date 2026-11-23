/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/psp.h>
#include <efi_variable_service.h>
#include <fmap_config.h>

#include "capsule_store.h"

const struct region_device *efi_variable_service_write_device(void)
{
	return psp_rom_armor_smm_device();
}

int efi_variable_service_bind_spi(struct efi_variable_service_state *service)
{
	/* The private PSP callbacks need no Intel controller resource. */
	return service->spi_bar ? -1 : 0;
}

struct region efi_variable_service_bios_region(void)
{
	/* Geometry does not authorize a whole-flash fallback update. */
	return region_create(0, CONFIG_ROM_SIZE);
}

int efi_variable_service_program_capsule(const struct region_device *flash, uint32_t offset,
			const struct efi_capsule_range *ranges, size_t count,
			uint8_t *image, uint8_t *readback, bool check_only)
{
	static const struct efi_capsule_range regions[] = {
		{ FMAP_SECTION_COREBOOT_START, FMAP_SECTION_COREBOOT_SIZE },
#ifdef FMAP_SECTION_AMDFW_START
		{ FMAP_SECTION_AMDFW_START, FMAP_SECTION_AMDFW_SIZE },
#endif
#ifdef FMAP_SECTION_EC_START
		{ FMAP_SECTION_EC_START, FMAP_SECTION_EC_SIZE },
#endif
	};
	static const struct efi_capsule_sector_policy policy = {
		.erase_size = 4 * KiB, .regions = regions, .region_count = ARRAY_SIZE(regions),
	};

	if (!flash || flash != psp_rom_armor_smm_device())
		return -1;
	/* FMAP and persistent state are absent from this exact-region policy. */
	return efi_capsule_program_sectors(flash, offset, EFI_CAPSULE_BLOCK_SIZE,
			ranges, count, image, readback, &policy, check_only);
}
