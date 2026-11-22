/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EFI_CAPSULE_STORE_H
#define EFI_CAPSULE_STORE_H

#include <commonlib/region.h>

struct efi_capsule_range {
	size_t offset;
	size_t size;
};

struct efi_capsule_sector_policy {
	size_t erase_size;
	const struct efi_capsule_range *regions;
	size_t region_count;
};

/* Private owner interface, never an SMI command. The owner authenticates image
 * and policy before calling. Both buffers are distinct protected block buffers.
 * Only the selected ranges within this block may change. */
int efi_capsule_program_block(const struct region_device *flash, size_t block_offset,
			      size_t block_size, const struct efi_capsule_range *ranges, size_t count,
			      uint8_t *image, uint8_t *readback, bool allow_erase);

/* The two buffers span the full transfer and must be disjoint. The readback
 * buffer provides at least two erase sectors of scratch. Every selected range
 * must match a complete policy region. The device, policy and approved-region
 * table are trusted persistent objects outside writable scratch. Check-only
 * performs no flash I/O. */
int efi_capsule_program_sectors(const struct region_device *flash, size_t transfer_offset,
				size_t transfer_size, const struct efi_capsule_range *ranges,
				size_t count, const uint8_t *image, uint8_t *readback,
				const struct efi_capsule_sector_policy *policy, bool check_only);
int efi_variable_service_program_capsule(const struct region_device *flash, uint32_t offset,
			const struct efi_capsule_range *ranges, size_t count,
			uint8_t *image, uint8_t *readback, bool check_only);

#endif
