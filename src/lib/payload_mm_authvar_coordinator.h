/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef PAYLOAD_MM_AUTHVAR_COORDINATOR_H
#define PAYLOAD_MM_AUTHVAR_COORDINATOR_H

#include <boot/payload_mm_authvar_authority.h>
#include <boot/payload_mm_authvar_candidate.h>

#define PAYLOAD_MM_AUTHVAR_COORDINATOR_CONTEXT_MAX 256U

struct payload_mm_authvar_coordinator_policy {
	const struct payload_mm_authvar_policy_request *request;
	struct payload_mm_crypto_owner *owner;
	payload_mm_authvar_authority_verify_fn *verify;
	void *verify_context;
	size_t verify_context_size;
	bool trusted_physical_presence;
};

struct payload_mm_authvar_coordinator_result {
	struct payload_mm_authvar_candidate_result candidate;
	enum payload_mm_authvar_authority_outcome outcome;
	u8 volatile_modes;
};

#if ENV_TEST
enum payload_mm_authvar_coordinator_test_fault {
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_NONE,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_INVALID,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_INTERNAL,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_CHANGED,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_UNSUPPORTED,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_REJECTED,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_OUTCOME_NONE,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_OUTCOME_RANGE,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_OUTCOME_NONE,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_NOT_FOUND_SUCCESS,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_NOOP_NOT_FOUND,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_MUTATION_NOT_FOUND,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_CANDIDATE_INVALID,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_CANDIDATE_SECURITY,
	PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_CANDIDATE_NO_MEMORY,
};
void payload_mm_authvar_coordinator_test_set_fault(
	enum payload_mm_authvar_coordinator_test_fault fault);
#endif

uint64_t payload_mm_authvar_coordinator_prepare(
	const struct payload_mm_authvar_coordinator_policy *coordinator,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_write_policy *policy,
	const struct payload_mm_authvar_candidate_binding *binding,
	bool ready_to_boot, void *append_workspace, size_t append_workspace_size,
	void *candidate, size_t candidate_capacity,
	struct payload_mm_authvar_store_entry *scan_entries,
	size_t scan_entry_capacity,
	struct payload_mm_authvar_coordinator_result *result,
	bool *invariant_failure);

/* Fixed physical-presence PK deletion; no public SetVariable input exists. */
uint64_t payload_mm_authvar_presence_prepare(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_write_policy *policy,
	const struct payload_mm_authvar_candidate_binding *binding,
	bool ready_to_boot, void *candidate, size_t candidate_capacity,
	struct payload_mm_authvar_store_entry *scan_entries,
	size_t scan_entry_capacity,
	struct payload_mm_authvar_coordinator_result *result,
	bool *invariant_failure);

#endif
