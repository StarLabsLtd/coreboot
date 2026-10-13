/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EFI_VARIABLE_SERVICE_IPL_H
#define EFI_VARIABLE_SERVICE_IPL_H

#include <stddef.h>
#include <types.h>

struct efi_variable_service_image {
	void *base;
	size_t size;
	void (*entry)(const void *context);
	uint8_t file_guid[16];
};

int efi_variable_service_load_mm_core(const void *fv, size_t fv_size,
				      void *destination, size_t destination_size,
				      struct efi_variable_service_image *image);

#endif
