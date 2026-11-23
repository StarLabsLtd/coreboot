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

	while (length) {
		/* Do not span the separate ROM2 and ROM3 mapping windows. */
		size_t size = MIN(length, 4 * KiB - (offset & (4 * KiB - 1)));
		void *mapping = rdev_mmap(boot_device_ro(), offset, size);
		if (mapping) {
			clflush_region((uintptr_t)mapping, size);
			rdev_munmap(boot_device_ro(), mapping);
		}
		offset += size;
		length -= size;
	}
	asm volatile ("mfence" ::: "memory");
}
