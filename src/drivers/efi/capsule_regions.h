/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EFI_CAPSULE_REGIONS_H
#define EFI_CAPSULE_REGIONS_H

#include <fmap_config.h>

static inline const char *efi_capsule_region_names(void)
{
	/* Publish the installed policy even when producing a standalone EC capsule. */
#if CONFIG(VBOOT_SLOTS_RW_AB)
#define SLOT_REGIONS "FW_MAIN_A RW_FWID_A VBLOCK_A FW_MAIN_B RW_FWID_B VBLOCK_B"
#if defined(FMAP_SECTION_EC_SIZE) && FMAP_SECTION_EC_SIZE
	return SLOT_REGIONS " EC";
#else
	return SLOT_REGIONS;
#endif
#undef SLOT_REGIONS
#else
	return CONFIG_DRIVERS_EFI_CAPSULE_REGIONS;
#endif
}

#endif
