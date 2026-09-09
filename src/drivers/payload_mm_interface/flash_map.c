/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <payload_mm_interface.h>

bool payload_mm_flash_region_is_valid(const struct region *region, size_t media_size,
				      size_t block_size)
{
	return block_size && region_offset(region) <= media_size &&
		region_sz(region) <= media_size - region_offset(region) &&
		region_offset(region) % block_size == 0 &&
		region_sz(region) % block_size == 0 && region_sz(region) / block_size >= 3;
}

bool payload_mm_map_flash_region(const struct region *region, uintptr_t *mapping)
{
	struct region_device rdev;
	void *base;

	if (boot_device_ro_subregion(region, &rdev))
		return false;

	base = rdev_mmap_full(&rdev);
	if (!base)
		return false;

	/* Payload MM requires this memory-mapped flash address for its lifetime. */
	*mapping = (uintptr_t)base;
	return true;
}
