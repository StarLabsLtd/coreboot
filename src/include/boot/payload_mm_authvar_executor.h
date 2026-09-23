/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_EXECUTOR_H
#define BOOT_PAYLOAD_MM_AUTHVAR_EXECUTOR_H

#include <boot/payload_mm_authvar_writer.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

struct payload_mm_authvar_executor_limits {
	uint32_t maximum_store_size;
	uint32_t maximum_name_size;
	uint32_t maximum_data_size;
	uint32_t maximum_record_size;
	uint32_t maximum_records;
};

/*
 * Internal SMM-only service. Installation is one-shot and requires mutually
 * disjoint protected-SMRAM arena/limits spans. The copied limits and computed
 * arena layout are sealed; one media session owns the single-flight arena.
 * Inputs are copied before media mutation and expire on return. Known backend
 * failures return their mapped status, while malformed durable state,
 * impossible plans, verification mismatch, or seal mutation permanently
 * fail-close the authenticated-variable media service.
 */
enum cb_err payload_mm_authvar_executor_install(
	void *trusted_smram_arena, size_t arena_size,
	const struct payload_mm_authvar_executor_limits *limits);
uint64_t payload_mm_authvar_executor_recover(void);
uint64_t payload_mm_authvar_executor_apply(
	const struct payload_mm_authvar_record_source *source,
	const struct payload_mm_authvar_write_policy *policy, bool at_runtime);

#endif
