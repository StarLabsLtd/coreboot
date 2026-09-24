/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_EXECUTOR_H
#define BOOT_PAYLOAD_MM_AUTHVAR_EXECUTOR_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

#if ENV_TEST && CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
#include <boot/payload_mm_authvar_candidate.h>
#endif
#if ENV_TEST && CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
#include <boot/payload_mm_authvar_authority.h>
#endif

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

#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
/* Private SMM composition point; no wire command or dispatcher is provided. */
uint64_t payload_mm_authvar_mor_control_clear_transaction(void);
#if ENV_TEST
enum payload_mm_authvar_mor_seal_test_mutation {
	PAYLOAD_MM_AUTHVAR_MOR_SEAL_TEST_RECORD_PAIR = 1,
	PAYLOAD_MM_AUTHVAR_MOR_SEAL_TEST_COPIES_PAIR,
	PAYLOAD_MM_AUTHVAR_MOR_SEAL_TEST_CANONICAL_PAIR,
};
bool payload_mm_authvar_executor_test_mutate_mor_seal(
	enum payload_mm_authvar_mor_seal_test_mutation mutation);
#endif
#endif

/*
 * GET, NEXT and QUERY only. Descriptors, input and bounded output spans must
 * be mutually disjoint protected memory. Output bytes and scalar results are
 * published only after the single media session ends successfully.
 */
uint64_t payload_mm_authvar_read_transaction(
	const struct payload_mm_authvar_read_request *request,
	struct payload_mm_authvar_read_result *result);

#if ENV_TEST && CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
typedef uint64_t (*payload_mm_authvar_candidate_prepare_test_fn)(
	const struct payload_mm_authvar_store_index *source,
	const struct payload_mm_authvar_candidate_binding *binding,
	void *candidate, size_t candidate_capacity,
	struct payload_mm_authvar_store_entry *scan_entries,
	size_t scan_entry_capacity,
	struct payload_mm_authvar_candidate_result *result, void *context);

/* Test-only in-session admission hook; no production endpoint is emitted. */
uint64_t payload_mm_authvar_executor_test_commit_candidate(
	payload_mm_authvar_candidate_prepare_test_fn prepare, void *context,
	u8 source_volatile_modes, u8 *published_volatile_modes);
void payload_mm_authvar_executor_test_corrupt_policy(bool corrupt);
#endif

#if ENV_TEST && CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
#define PAYLOAD_MM_AUTHVAR_COORDINATOR_TEST_REVISION 1U

struct payload_mm_authvar_coordinator_test_request {
	uint32_t revision;
	uint32_t size;
	const struct payload_mm_authvar_policy_request *request;
	struct payload_mm_crypto_owner *owner;
	payload_mm_authvar_authority_verify_fn *verify;
	void *verify_context;
	size_t verify_context_size;
	uint8_t trusted_physical_presence;
	uint8_t reserved[7];
};

struct payload_mm_authvar_coordinator_test_result {
	uint64_t status;
	uint32_t outcome;
	uint8_t volatile_modes;
	uint8_t reserved[3];
	uint32_t completion;
};
void payload_mm_authvar_executor_test_corrupt_coordinate_policy(void);

/* Test-only proof hook; the production coordinator remains private. */
uint64_t payload_mm_authvar_executor_test_coordinate(
	const struct payload_mm_authvar_coordinator_test_request *request,
	struct payload_mm_authvar_coordinator_test_result *result);
bool payload_mm_authvar_executor_test_coordinator_spans(
	const void *const parts[7], const size_t sizes[7]);
bool payload_mm_authvar_executor_test_coordinator_lengths(size_t name_size,
	size_t data_size, size_t context_size);
#endif

#endif
