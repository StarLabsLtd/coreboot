/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_WRITER_H
#define BOOT_PAYLOAD_MM_AUTHVAR_WRITER_H

#include <boot/payload_mm_authvar_store_semantics.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define PAYLOAD_MM_AUTHVAR_WRITE_MAX_STEPS 6U

enum payload_mm_authvar_write_action {
	PAYLOAD_MM_AUTHVAR_WRITE_NOOP,
	PAYLOAD_MM_AUTHVAR_WRITE_DELETE,
	PAYLOAD_MM_AUTHVAR_WRITE_TAIL_APPEND,
	PAYLOAD_MM_AUTHVAR_WRITE_RECLAIM,
	PAYLOAD_MM_AUTHVAR_WRITE_OUT_OF_RESOURCES,
};

enum payload_mm_authvar_write_step_kind {
	PAYLOAD_MM_AUTHVAR_WRITE_STATE,
	PAYLOAD_MM_AUTHVAR_WRITE_RECORD,
};

struct payload_mm_authvar_record_source {
	uint8_t vendor_guid[16];
	const void *name;
	size_t name_size;
	const void *data;
	size_t data_size;
	uint32_t attributes;
	uint8_t timestamp[16];
};

struct payload_mm_authvar_write_policy {
	uint32_t maximum_name_size;
	uint32_t maximum_record_size;
	uint32_t maximum_data_size;
	uint32_t maximum_records;
};

struct payload_mm_authvar_write_step {
	enum payload_mm_authvar_write_step_kind kind;
	/* Authenticated-variable-store-relative, never FV/SMMSTORE-relative. */
	uint32_t offset;
	uint32_t size;
	uint8_t expected_state;
	uint8_t new_state;
};

struct payload_mm_authvar_write_plan {
	enum payload_mm_authvar_write_action action;
	/* Authenticated-variable-store-relative, never FV/SMMSTORE-relative. */
	uint32_t record_offset;
	uint32_t record_size;
	uint32_t step_count;
	struct payload_mm_authvar_write_step steps[PAYLOAD_MM_AUTHVAR_WRITE_MAX_STEPS];
};

/*
 * Internal, non-SMI helper over a trusted immutable snapshot. Before calling,
 * the later executor must bind the snapshot to one active media generation and
 * session and place mutually disjoint index/store/source and output objects in
 * protected SMRAM. The snapshot, record, reclaim copies and plan expire at
 * media_end(). RECORD requires the exact offset/size destination span to
 * compare erased, programs exactly the canonical record while its state remains
 * erased, then requires durable byte-for-byte readback before the next step.
 * Any compare/program/readback failure stops the plan. STATE steps are ordered
 * exact compares followed by NOR-safe compare-and-clear, durable program and
 * readback. RECLAIM deliberately emits no direct steps; its
 * canonical record and reclaim fields form the later FTW input. This interface
 * performs no media operation. A NULL source requests deletion. Every output
 * is invalid on a non-SUCCESS status and Gate G must status-gate before reading
 * or executing any of it; alias failures cannot safely clear an overlapping
 * caller object.
 */
uint64_t payload_mm_authvar_write_plan_build(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *replaced,
	const struct payload_mm_authvar_record_source *source,
	const struct payload_mm_authvar_write_policy *policy, bool at_runtime,
	void *record, size_t record_capacity,
	struct payload_mm_authvar_reclaim_plan *reclaim,
	struct payload_mm_authvar_write_plan *plan);

#endif
