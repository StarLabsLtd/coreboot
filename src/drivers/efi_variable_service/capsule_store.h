/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EFI_CAPSULE_STORE_H
#define EFI_CAPSULE_STORE_H

#include <commonlib/region.h>

struct efi_capsule_range {
	size_t offset;
	size_t size;
};

/* Private owner interface, never an SMI command. The owner authenticates image
 * and policy before calling. Both buffers are distinct protected block buffers.
 * Only the selected ranges within this block may change. */
int efi_capsule_program_block(const struct region_device *flash, size_t block_offset,
			      size_t block_size, const struct efi_capsule_range *ranges, size_t count,
			      uint8_t *image, uint8_t *readback, bool allow_erase);
#endif
