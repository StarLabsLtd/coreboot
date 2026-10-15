/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_STORE_SEMANTICS_H
#define BOOT_PAYLOAD_MM_AUTHVAR_STORE_SEMANTICS_H

#include <boot/payload_mm_authvar_store.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

struct payload_mm_authvar_get_result {
	const struct payload_mm_authvar_store_entry *entry;
	uint32_t required_data_size;
	uint32_t attributes;
};

struct payload_mm_authvar_next_result {
	const struct payload_mm_authvar_store_entry *entry;
	uint32_t required_name_size;
};

struct payload_mm_authvar_store_policy {
	uint32_t maximum_storage;
	uint32_t maximum_record_size;
};

struct payload_mm_authvar_query_result {
	uint64_t maximum_storage;
	uint64_t remaining_storage;
	uint64_t maximum_variable;
};

struct payload_mm_authvar_reclaim_copy {
	uint32_t source_offset;
	uint32_t destination_offset;
	uint32_t size;
	bool promote_transition;
};

enum payload_mm_authvar_space_action {
	PAYLOAD_MM_AUTHVAR_SPACE_APPEND,
	PAYLOAD_MM_AUTHVAR_SPACE_RECLAIM,
	PAYLOAD_MM_AUTHVAR_SPACE_OUT_OF_RESOURCES,
};

struct payload_mm_authvar_new_record {
	uint32_t name_size;
	uint32_t data_size;
};

struct payload_mm_authvar_reclaim_plan {
	enum payload_mm_authvar_space_action action;
	uint32_t record_size;
	uint32_t destination_offset;
	uint32_t compacted_used_size;
	uint32_t reclaimable_size;
	uint32_t copy_count;
	struct payload_mm_authvar_reclaim_copy *copies;
	uint32_t copy_capacity;
};

uint64_t payload_mm_authvar_store_get(
	const struct payload_mm_authvar_store_index *index,
	const uint8_t vendor_guid[16], const void *name, size_t name_size,
	uint32_t data_capacity, bool at_runtime,
	struct payload_mm_authvar_get_result *result);
uint64_t payload_mm_authvar_store_get_next(
	const struct payload_mm_authvar_store_index *index,
	const uint8_t vendor_guid[16], const void *name, size_t name_size,
	uint32_t name_capacity, bool at_runtime,
	struct payload_mm_authvar_next_result *result);
uint64_t payload_mm_authvar_store_query(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_policy *policy, uint32_t attributes,
	bool at_runtime, struct payload_mm_authvar_query_result *result);
enum cb_err payload_mm_authvar_store_reclaim_plan(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *replaced,
	const struct payload_mm_authvar_new_record *new_record, bool at_runtime,
	struct payload_mm_authvar_reclaim_plan *plan);
enum cb_err payload_mm_authvar_store_reclaim_plan_forced(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *replaced,
	const struct payload_mm_authvar_new_record *new_record, bool at_runtime,
	bool force_reclaim, struct payload_mm_authvar_reclaim_plan *plan);

#endif
