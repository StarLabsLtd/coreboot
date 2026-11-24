/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <cpu/x86/cache.h>
#include "psp_rom_armor_apmc.h"

/* Each caller invalidates its own stage's mapped flash aliases. */
void psp_rom_armor_invalidate_mmap(size_t offset, size_t length)
{
	if (!CONFIG(SOC_AMD_COMMON_BLOCK_PSP_ROM_ARMOR3) ||
	    length > CONFIG_ROM_SIZE || offset > CONFIG_ROM_SIZE - length)
		return;

	const struct region_device *flash = boot_device_ro();

	while (length) {
		/* Do not span the separate ROM2 and ROM3 mapping windows. */
		size_t size = MIN(length, 4 * KiB - (offset & (4 * KiB - 1)));
		void *mapping = rdev_mmap(flash, offset, size);
		if (mapping) {
			const uintptr_t start = (uintptr_t)mapping;

			/* The ROM2 window can end at 4 GiB on 32-bit builds. */
			if (size > UINTPTR_MAX - start) {
				clflush_region(start, size - 1);
				clflush((void *)(start + (size - 1)));
			} else {
				clflush_region(start, size);
			}
			rdev_munmap(flash, mapping);
		}
		offset += size;
		length -= size;
	}
	asm volatile ("mfence" ::: "memory");
}
