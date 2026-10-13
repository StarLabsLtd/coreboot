/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EFI_VARIABLE_PRIVATE_STORE_H
#define EFI_VARIABLE_PRIVATE_STORE_H

#include <commonlib/region.h>
#include <stdint.h>

/* The device and buffer range are supplied by the trusted SMM initializer. */
struct efi_private_store {
	const struct region_device *device;
	uintptr_t buffer_start;
	size_t buffer_size;
	size_t block_size;
};

int efi_private_store_init(struct efi_private_store *store,
			   const struct region_device *device, size_t block_size,
			   uintptr_t buffer_start, size_t buffer_size);
int efi_private_store_read(const struct efi_private_store *store, size_t offset,
			   void *buffer, size_t size);
int efi_private_store_write(const struct efi_private_store *store, size_t offset,
			    const void *buffer, size_t size);
int efi_private_store_erase(const struct efi_private_store *store, size_t offset,
			    size_t size);

#endif
