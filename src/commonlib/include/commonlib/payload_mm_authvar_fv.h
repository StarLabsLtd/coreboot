/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef COMMONLIB_PAYLOAD_MM_AUTHVAR_FV_H
#define COMMONLIB_PAYLOAD_MM_AUTHVAR_FV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_FV_MIN_BLOCKS 3U
#define PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE 72U
#define PAYLOAD_MM_AUTHVAR_FV_PREFIX_SIZE 100U

struct payload_mm_authvar_fv_geometry {
	uint32_t block_size;
	uint32_t block_count;
	uint32_t variable_offset;
	uint32_t variable_size;
	uint32_t working_offset;
	uint32_t working_size;
	uint32_t spare_offset;
	uint32_t spare_size;
};

bool payload_mm_authvar_fv_geometry(struct payload_mm_authvar_fv_geometry *geometry,
	size_t region_size, size_t block_size);
bool payload_mm_authvar_fv_format(void *region, size_t region_size,
	size_t block_size);
/* Encode only the FV and variable-store headers for the complete geometry. */
bool payload_mm_authvar_fv_format_header(void *header, size_t capacity,
	size_t region_size, size_t block_size);

#endif
