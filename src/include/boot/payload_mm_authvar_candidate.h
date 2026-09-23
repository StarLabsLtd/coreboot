/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_CANDIDATE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_CANDIDATE_H

#include <boot/payload_mm_authvar_bundle.h>
#include <boot/payload_mm_authvar_writer.h>
#include <payload_mm_cms.h>

struct payload_mm_authvar_candidate_binding {
	u64 generation;
	u64 token;
	u8 source_volatile_modes;
	u8 at_runtime;
	u8 reserved[6];
};

struct payload_mm_authvar_candidate_result {
	struct payload_mm_authvar_candidate_binding binding;
	struct payload_mm_authvar_write_policy policy;
	u32 source_used_size;
	u32 candidate_used_size;
	u32 candidate_record_count;
	u8 volatile_modes;
	u8 reserved[3];
	u8 source_digest[PAYLOAD_MM_SHA256_SIZE];
	u8 candidate_digest[PAYLOAD_MM_SHA256_SIZE];
};

/*
 * Build a complete canonical replacement image in protected memory. The
 * source is independently rescanned into scan_entries before it is trusted.
 * This pure stage performs no media access and publishes no endpoint. On
 * failure result is zero and candidate contents are invalid. If result cannot
 * first be proven writable and disjoint from every input/output range, it is
 * left untouched and INVALID_PARAMETER is returned.
 */
uint64_t payload_mm_authvar_candidate_build(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_bundle_plan *bundle,
	const struct payload_mm_authvar_write_policy *policy,
	const struct payload_mm_authvar_candidate_binding *binding,
	void *candidate, size_t candidate_capacity,
	struct payload_mm_authvar_store_entry *scan_entries,
	size_t scan_entry_capacity,
	struct payload_mm_authvar_candidate_result *result);

#endif
