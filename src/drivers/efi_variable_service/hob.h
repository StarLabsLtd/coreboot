/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EFI_VARIABLE_SERVICE_HOB_H
#define EFI_VARIABLE_SERVICE_HOB_H

#include <stddef.h>
#include <types.h>

#include "ipl.h"

#define HOB_RESOURCE_DESCRIPTOR 0x0003
#define RESOURCE_MEMORY_MAPPED_IO 1

struct efi_guid {
	uint32_t data1;
	uint16_t data2;
	uint16_t data3;
	uint8_t data4[8];
} __packed;

struct hob_header {
	uint16_t type;
	uint16_t length;
	uint32_t reserved;
} __packed;

struct hob_resource {
	struct hob_header header;
	struct efi_guid owner;
	uint32_t type;
	uint32_t attributes;
	uint64_t base;
	uint64_t size;
} __packed;

struct service_info_hob {
	uint16_t revision;
	uint16_t size;
	uint32_t reserved;
	uint64_t buffer;
	uint32_t buffer_size;
	uint32_t register_mm_entry;
} __packed;

struct efi_variable_service_state;

int efi_variable_service_build_hobs(uintptr_t hob_base, uintptr_t region_end,
				    uintptr_t fv_base, size_t fv_size,
				    const struct efi_variable_service_image *image,
				    uintptr_t buffer, size_t buffer_size,
				    struct efi_variable_service_state *state);

#endif
