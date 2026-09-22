/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_FTW_H
#define BOOT_PAYLOAD_MM_AUTHVAR_FTW_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define PAYLOAD_MM_AUTHVAR_FTW_MIN_BLOCKS 3U

struct payload_mm_authvar_ftw_geometry {
	uint32_t block_size;
	uint32_t block_count;
	uint32_t variable_offset;
	uint32_t variable_size;
	uint32_t working_offset;
	uint32_t working_size;
	uint32_t spare_offset;
	uint32_t spare_size;
};

enum payload_mm_authvar_ftw_action {
	PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED = 0,
	PAYLOAD_MM_AUTHVAR_FTW_CLEAN,
	PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE,
	PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD,
	PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE,
	PAYLOAD_MM_AUTHVAR_FTW_COMPLETE_NEW,
	PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE,
};

struct payload_mm_authvar_ftw_plan {
	enum payload_mm_authvar_ftw_action action;
	struct payload_mm_authvar_ftw_geometry geometry;
	uint32_t fv_header_size;
	uint32_t variable_store_size;
	uint32_t queue_offset;
	uint32_t queue_entry_size;
};

enum cb_err payload_mm_authvar_ftw_geometry(
	struct payload_mm_authvar_ftw_geometry *geometry, size_t region_size,
	size_t block_size);
enum cb_err payload_mm_authvar_ftw_plan(const void *region, size_t region_size,
	size_t block_size, struct payload_mm_authvar_ftw_plan *plan);

#endif
