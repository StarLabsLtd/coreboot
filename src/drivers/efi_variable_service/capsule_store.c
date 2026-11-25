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

static bool capsule_plan_allowed(const struct efi_capsule_sector_policy *policy,
				 const struct efi_capsule_range *ranges, size_t count,
				 size_t flash_size, size_t transfer_offset, size_t transfer_size)
{
	bool intersects = false;

	for (size_t i = 0; i < policy->region_count; i++) {
		const struct efi_capsule_range *region = &policy->regions[i];

		if (!region->size || region->offset >= flash_size ||
		    region->size > flash_size - region->offset ||
		    (!policy->allow_partial_sectors &&
		     (region->offset % policy->erase_size || region->size % policy->erase_size)) ||
		    region->size == flash_size)
			return false;
	}

	for (size_t i = 0; i < count; i++) {
		size_t j;

		for (j = 0; j < policy->region_count; j++) {
			if (ranges[i].offset == policy->regions[j].offset &&
			    ranges[i].size == policy->regions[j].size)
				break;
		}
		if (j == policy->region_count)
			return false;
		if (ranges[i].offset < transfer_offset + transfer_size &&
		    transfer_offset < ranges[i].offset + ranges[i].size)
			intersects = true;
	}

	return intersects;
}

int efi_capsule_program_sectors(const struct region_device *flash, size_t transfer_offset,
				size_t transfer_size, const struct efi_capsule_range *ranges,
				size_t count, const uint8_t *image, uint8_t *readback,
				const struct efi_capsule_sector_policy *policy, bool check_only)
{
	struct region image_span, readback_span, plan_span;
	const struct region_device *root;
	size_t erase_size, flash_size, flash_offset;

	if (!flash || !policy || !policy->regions || !policy->region_count ||
	    !ranges || !count || count > 32 || !image || !readback || !transfer_size)
		return -1;

	root = flash->root ? flash->root : flash;
	if (!root->ops || !root->ops->readat || !root->ops->writeat || !root->ops->eraseat)
		return -1;

	erase_size = policy->erase_size;
	flash_size = region_device_sz(flash);
	flash_offset = region_device_offset(flash);
	if (!erase_size || erase_size > INT_MAX || erase_size > transfer_size / 2 ||
	    transfer_size % erase_size || transfer_offset % transfer_size ||
	    flash_size % transfer_size ||
	    flash_offset % erase_size || flash_size > SIZE_MAX - flash_offset ||
	    transfer_offset >= flash_size || transfer_size > flash_size - transfer_offset ||
	    region_create_untrusted(&image_span, (uintptr_t)image, transfer_size) ||
	    region_create_untrusted(&readback_span, (uintptr_t)readback, transfer_size) ||
	    region_create_untrusted(&plan_span, (uintptr_t)ranges, count * sizeof(*ranges)) ||
	    region_overlap(&image_span, &readback_span) || region_overlap(&plan_span, &readback_span))
		return -1;

	/* Validate the complete policy and selection before touching any sector. */
	if (!capsule_plan_allowed(policy, ranges, count, flash_size, transfer_offset, transfer_size))
		return -1;
	if (check_only)
		return 0;

	for (size_t offset = 0; offset < transfer_size; offset += erase_size) {
		const struct efi_capsule_range sector = { transfer_offset + offset, erase_size };

		for (size_t i = 0; i < count; i++) {
			if (sector.offset >= ranges[i].offset + ranges[i].size ||
			    ranges[i].offset >= sector.offset + erase_size)
				continue;

			/* The block verifier may overwrite its input during failed readback. */
			memcpy(readback, image + offset, erase_size);
			if (efi_capsule_program_block(flash, sector.offset, erase_size, ranges, count,
						      readback, readback + erase_size, true))
				return -1;
			break;
		}
	}

	return 0;
}
