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
	PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED,
	PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE,
	PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD,
	PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE,
	PAYLOAD_MM_AUTHVAR_FTW_COMPLETE_NEW,
	PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE,
};

enum payload_mm_authvar_ftw_workspace {
	PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_NONE = 0,
	PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING,
	PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE,
};

enum payload_mm_authvar_ftw_queue_disposition {
	PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE = 0,
	PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY,
	PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD,
};

struct payload_mm_authvar_ftw_plan {
	enum payload_mm_authvar_ftw_action action;
	/* All geometry offsets are relative to the complete SMMSTORE region. */
	struct payload_mm_authvar_ftw_geometry geometry;
	/* These sizes come only from the decoder-validated active/spare FV. */
	uint32_t fv_header_size;
	uint32_t variable_store_size;
	/* Queue coordinates are relative to, and span only, the workspace below. */
	uint32_t queue_offset;
	uint32_t queue_entry_size;
	/*
	 * DISCARD_UNCOMMITTED erases this complete geometry working/spare range;
	 * no queue, FV, SMMSTORE, or media-absolute offset may replace it.
	 */
	enum payload_mm_authvar_ftw_workspace workspace;
	enum payload_mm_authvar_ftw_queue_disposition queue_disposition;
};

enum cb_err payload_mm_authvar_ftw_geometry(
	struct payload_mm_authvar_ftw_geometry *geometry, size_t region_size,
	size_t block_size);
enum cb_err payload_mm_authvar_ftw_plan(const void *region, size_t region_size,
	size_t block_size, struct payload_mm_authvar_ftw_plan *plan);

#endif
