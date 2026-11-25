/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/psp.h>
#include <efi_variable_service.h>
#include <fmap_config.h>

#include "capsule_store.h"
#include "hob.h"

const struct region_device *efi_variable_service_write_device(void)
{
	return psp_rom_armor_smm_device();
}

const struct region_device *efi_variable_service_read_device(void)
{
	return psp_rom_armor_smm_device();
}

int efi_variable_service_bind_spi(const struct efi_variable_service_state *service)
{
	/* The private PSP callbacks need no Intel controller resource. */
	return service->spi_mmio_resource ? -1 : 0;
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
	/* PSP recovery chooses its own partition and has no native capsule trial
	 * protocol. Leave PSP-managed firmware out of its writable policy. */
	static const struct efi_capsule_range regions[] = {
#if CONFIG(VBOOT_SLOTS_RW_AB)
		{ FMAP_SECTION_FW_MAIN_A_START, FMAP_SECTION_FW_MAIN_A_SIZE },
		{ FMAP_SECTION_RW_FWID_A_START, FMAP_SECTION_RW_FWID_A_SIZE },
		{ FMAP_SECTION_VBLOCK_A_START, FMAP_SECTION_VBLOCK_A_SIZE },
		{ FMAP_SECTION_FW_MAIN_B_START, FMAP_SECTION_FW_MAIN_B_SIZE },
		{ FMAP_SECTION_RW_FWID_B_START, FMAP_SECTION_RW_FWID_B_SIZE },
		{ FMAP_SECTION_VBLOCK_B_START, FMAP_SECTION_VBLOCK_B_SIZE },
#elif !CONFIG(PSP_AB_RECOVERY)
		{ FMAP_SECTION_COREBOOT_START, FMAP_SECTION_COREBOOT_SIZE },
#endif
#ifdef FMAP_SECTION_EC_START
		{ FMAP_SECTION_EC_START, FMAP_SECTION_EC_SIZE },
#endif
	};
	static const struct efi_capsule_sector_policy policy = {
		.erase_size = 4 * KiB, .regions = regions, .region_count = ARRAY_SIZE(regions),
		.allow_partial_sectors = CONFIG(VBOOT_SLOTS_RW_AB),
	};

	if (!flash || flash != psp_rom_armor_smm_device())
		return -1;
	/* FMAP and persistent state are absent from this exact-region policy. */
	return efi_capsule_program_sectors(flash, offset, EFI_CAPSULE_BLOCK_SIZE,
			ranges, count, image, readback, &policy, check_only);
}
