/* SPDX-License-Identifier: GPL-2.0-only */
#include "capsule_store.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

int efi_capsule_program_block(const struct region_device *flash, size_t block_offset,
			      size_t block_size, const struct efi_capsule_range *ranges, size_t count,
			      uint8_t *image, uint8_t *readback, bool allow_erase)
{
	size_t start, end;
	bool changed = false;
	bool selected = false;
	uintptr_t first = (uintptr_t)image;
	uintptr_t second = (uintptr_t)readback;

	if (!flash || !block_size || block_size > INT_MAX || !image || !readback ||
	    block_offset % block_size || block_offset >= region_device_sz(flash) ||
	    block_size > region_device_sz(flash) - block_offset ||
	    !ranges || !count || count > 32 ||
	    block_size - 1 > UINTPTR_MAX - first ||
	    block_size - 1 > UINTPTR_MAX - second ||
	    (first <= second ? second - first < block_size : first - second < block_size))
		return -1;

	/* Validate the complete plan before reading or changing flash. */
	for (size_t i = 0; i < count; i++) {
		if (!ranges[i].size || ranges[i].offset >= region_device_sz(flash) ||
		    ranges[i].size > region_device_sz(flash) - ranges[i].offset)
			return -1;
		if (ranges[i].offset < block_offset + block_size &&
		    block_offset < ranges[i].offset + ranges[i].size)
			selected = true;
	}
	if (!selected ||
	    rdev_readat(flash, readback, block_offset, block_size) != (ssize_t)block_size)
		return -1;

	/* Merge all selected parts in one erase, preserving their neighbours. */
	for (size_t i = 0; i < count; i++) {
		start = ranges[i].offset > block_offset ? ranges[i].offset : block_offset;
		end = ranges[i].offset + ranges[i].size;
		if (end > block_offset + block_size)
			end = block_offset + block_size;
		if (start >= end)
			continue;
		start -= block_offset;
		end -= block_offset;
		if (memcmp(readback + start, image + start, end - start))
			changed = true;
		memcpy(readback + start, image + start, end - start);
	}
	if (!changed)
		return 0;
	if (!allow_erase)
		return -1;

	/* The caller has already authenticated the private image snapshot. */
	/* Keep the original merged block across the existing four-attempt budget.
	 * A retry must not reconstruct neighbours from a partly erased block. */
	for (unsigned int attempt = 0; attempt < 4; attempt++) {
		if (rdev_eraseat(flash, block_offset, block_size) == (ssize_t)block_size &&
		    rdev_writeat(flash, readback, block_offset, block_size) == (ssize_t)block_size &&
		    rdev_readat(flash, image, block_offset, block_size) == (ssize_t)block_size &&
		    !memcmp(image, readback, block_size))
			return 0;
	}
	return -1;
}
