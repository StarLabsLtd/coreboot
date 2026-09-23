/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_EXECUTOR_H
#define BOOT_PAYLOAD_MM_AUTHVAR_EXECUTOR_H

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

/* Internal protected request/result, not a wire ABI. */
struct payload_mm_authvar_read_request {
	uint32_t operation;
	uint32_t attributes;
	uint8_t vendor_guid[16];
	const void *name;
	size_t name_size;
	void *result_name;
	size_t name_capacity;
	void *result_data;
	size_t data_capacity;
};

struct payload_mm_authvar_read_result {
	uint64_t status;
	uint64_t maximum_storage;
	uint64_t remaining_storage;
	uint64_t maximum_variable;
	uint32_t required_name_size;
	uint32_t required_data_size;
	uint32_t attributes;
	uint8_t vendor_guid[16];
	uint32_t completion;
	uint32_t reserved;
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

/*
 * GET, NEXT and QUERY only. Descriptors, input and bounded output spans must
 * be mutually disjoint protected memory. Output bytes and scalar results are
 * published only after the single media session ends successfully.
 */
uint64_t payload_mm_authvar_read_transaction(
	const struct payload_mm_authvar_read_request *request,
	struct payload_mm_authvar_read_result *result);

#endif
