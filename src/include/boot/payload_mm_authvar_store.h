/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_STORE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_STORE_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE 28U
#define PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE 60U
#define PAYLOAD_MM_AUTHVAR_RECORD_START_ID 0x55aaU
#define PAYLOAD_MM_AUTHVAR_STATE_ERASED 0xffU
#define PAYLOAD_MM_AUTHVAR_STATE_IN_DELETED_TRANSITION 0xfeU
#define PAYLOAD_MM_AUTHVAR_STATE_DELETED 0xfdU
#define PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY 0x7fU
#define PAYLOAD_MM_AUTHVAR_STATE_ADDED 0x3fU
#define PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION \
	(PAYLOAD_MM_AUTHVAR_STATE_ADDED & \
	 PAYLOAD_MM_AUTHVAR_STATE_IN_DELETED_TRANSITION)
#define PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED \
	(PAYLOAD_MM_AUTHVAR_STATE_ADDED & PAYLOAD_MM_AUTHVAR_STATE_DELETED)
#define PAYLOAD_MM_AUTHVAR_STATE_TRANSITION_DELETED \
	(PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION & \
	 PAYLOAD_MM_AUTHVAR_STATE_DELETED)
#define PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_SIZE (16U * 1024U * 1024U)
#define PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE 4096U
#define PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_DATA_SIZE (1024U * 1024U)
#define PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_RECORDS 4096U

struct payload_mm_authvar_store_limits {
	uint32_t maximum_store_size;
	uint32_t maximum_name_size;
	uint32_t maximum_data_size;
	uint32_t maximum_records;
};

struct payload_mm_authvar_store_entry {
	uint32_t record_offset;
	uint32_t name_offset;
	uint32_t name_size;
	uint32_t data_offset;
	uint32_t data_size;
	uint32_t attributes;
	uint8_t vendor_guid[16];
};

struct payload_mm_authvar_store_index {
	const uint8_t *store;
	uint32_t store_size;
	uint32_t used_size;
	uint32_t dirty_tail_offset;
	uint32_t record_count;
	uint32_t entry_count;
	struct payload_mm_authvar_store_entry *entries;
	uint32_t entry_capacity;
	uint32_t maximum_name_size;
	uint32_t maximum_data_size;
	uint32_t maximum_records;
};

enum cb_err payload_mm_authvar_store_scan(
	struct payload_mm_authvar_store_index *index, const void *store,
	size_t buffer_size, const struct payload_mm_authvar_store_limits *limits);
/*
 * Validate the complete store and return the winner for one exact key.
 * The output and key pointers are required. Outputs must not overlap each
 * other or any input. Aliasing is rejected without modifying the input;
 * otherwise outputs are zeroed on error.
 */
enum cb_err payload_mm_authvar_store_find_one(
	struct payload_mm_authvar_store_entry *entry, bool *found,
	const void *store, size_t buffer_size,
	const struct payload_mm_authvar_store_limits *limits,
	const uint8_t vendor_guid[16], const void *name, size_t name_size);
/* Revalidate every scanner/index invariant before policy consumes a snapshot. */
bool payload_mm_authvar_store_index_valid(
	const struct payload_mm_authvar_store_index *index);
const struct payload_mm_authvar_store_entry *payload_mm_authvar_store_find(
	const struct payload_mm_authvar_store_index *index,
	const uint8_t vendor_guid[16], const void *name, size_t name_size);
const struct payload_mm_authvar_store_entry *payload_mm_authvar_store_next(
	const struct payload_mm_authvar_store_index *index, size_t *position);
const void *payload_mm_authvar_store_name(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *entry);
const void *payload_mm_authvar_store_data(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *entry);

#endif
