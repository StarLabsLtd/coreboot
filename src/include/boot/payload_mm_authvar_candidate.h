/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_CANDIDATE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_CANDIDATE_H

#include <boot/payload_mm_authvar_bundle.h>
#include <boot/payload_mm_authvar_writer.h>

struct payload_mm_authvar_candidate_binding {
	u64 generation;
	u64 token;
	u8 source_volatile_modes;
	u8 at_runtime;
	u8 reserved[6];
};

/*
 * Build a complete canonical replacement image in caller-owned memory. The
 * source is independently rescanned into scan_entries before it is trusted.
 * The caller keeps the snapshot immutable and owns its isolation; SMM callers
 * keep these objects in SMRAM. This pure stage performs no media access and
 * publishes no endpoint. On failure volatile_modes is zero and candidate
 * contents are invalid. If volatile_modes cannot first be proven writable and
 * disjoint from every input/output range, it is left untouched and
 * INVALID_PARAMETER is returned.
 */
uint64_t payload_mm_authvar_candidate_build(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_bundle_plan *bundle,
	const struct payload_mm_authvar_write_policy *policy,
	const struct payload_mm_authvar_candidate_binding *binding,
	void *candidate, size_t candidate_capacity,
	struct payload_mm_authvar_store_entry *scan_entries,
	size_t scan_entry_capacity,
	u8 *volatile_modes);

#endif
