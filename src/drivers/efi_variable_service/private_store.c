/* SPDX-License-Identifier: GPL-2.0-only */

#include "private_store.h"

#include <limits.h>
#include <string.h>

int efi_private_store_init(struct efi_private_store *store,
			   const struct region_device *device, size_t block_size,
			   uintptr_t buffer_start, size_t buffer_size)
{
	if (!store)
		return -1;
	memset(store, 0, sizeof(*store));
	if (!device || !block_size || block_size > INT_MAX ||
	    !region_device_sz(device) || region_device_sz(device) % block_size ||
	    !buffer_start || !buffer_size || buffer_size - 1 > UINTPTR_MAX - buffer_start)
		return -1;
	store->device = device;
	store->buffer_start = buffer_start;
	store->buffer_size = buffer_size;
	store->block_size = block_size;
	return 0;
}

static int valid_range(const struct efi_private_store *store, size_t offset, size_t size)
{
	if (!store || !store->device || !store->block_size || !size ||
	    offset >= region_device_sz(store->device) ||
	    size > region_device_sz(store->device) - offset)
		return 0;
	return 1;
}

static int valid_buffer(const struct efi_private_store *store, const void *buffer, size_t size)
{
	uintptr_t address = (uintptr_t)buffer;

	if (!buffer || address < store->buffer_start ||
	    address - store->buffer_start >= store->buffer_size)
		return 0;
	return size <= store->buffer_size - (address - store->buffer_start);
}

int efi_private_store_read(const struct efi_private_store *store, size_t offset,
			   void *buffer, size_t size)
{
	if (!valid_range(store, offset, size) || !valid_buffer(store, buffer, size) ||
	    size > store->block_size - offset % store->block_size)
		return -1;
	return rdev_readat(store->device, buffer, offset, size) == (ssize_t)size ? 0 : -1;
}

int efi_private_store_write(const struct efi_private_store *store, size_t offset,
			    const void *buffer, size_t size)
{
	if (!valid_range(store, offset, size) || !valid_buffer(store, buffer, size) ||
	    size > store->block_size - offset % store->block_size)
		return -1;
	return rdev_writeat(store->device, buffer, offset, size) == (ssize_t)size ? 0 : -1;
}

int efi_private_store_erase(const struct efi_private_store *store, size_t offset, size_t size)
{
	if (!valid_range(store, offset, size) || offset % store->block_size ||
	    size != store->block_size)
		return -1;
	return rdev_eraseat(store->device, offset, size) == (ssize_t)size ? 0 : -1;
}
