/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_H

#include <boot/payload_mm_authvar_presence_transaction.h>
#include <commonlib/bsd/cb_err.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_POLICY_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX 128U

/* Protected SMM composition input; this is not a wire or table ABI. */
struct payload_mm_authvar_presence_lifecycle_close_policy {
	uint32_t revision;
	uint32_t size;
	uint64_t generation;
	uint64_t predecessor_invocation_generation;
	uint32_t initiator_cpu;
	uint32_t maximum_cpus;
	struct payload_mm_authvar_presence_transaction_slot *transaction_slot;
	payload_mm_authvar_presence_transaction_claim_fn claim_invocation;
	payload_mm_authvar_presence_transaction_complete_fn complete_invocation;
	void *context;
	size_t context_size;
};

#if ENV_SMM || ENV_TEST
enum cb_err payload_mm_authvar_presence_lifecycle_close_provision(
	const struct payload_mm_authvar_presence_lifecycle_close_policy *policy,
	payload_mm_authvar_protected_storage storage_is_protected,
	void *storage_context);

/* Exact source-owned operations. Success means exact-generation CLOSED. */
enum cb_err payload_mm_authvar_presence_pre_external_image_close(void);
enum cb_err payload_mm_authvar_presence_payload_failure_or_return_close(void);
enum cb_err payload_mm_authvar_presence_warm_reset_close(void);
enum cb_err payload_mm_authvar_presence_s3_resume_close(void);
enum cb_err payload_mm_authvar_presence_closed_reproof(void);
#endif

#if ENV_TEST
typedef void (*payload_mm_authvar_presence_lifecycle_close_test_hook_fn)(void);

void payload_mm_authvar_presence_lifecycle_close_reset_test(void);
const void *payload_mm_authvar_presence_lifecycle_close_state_test(size_t *size);
void payload_mm_authvar_presence_lifecycle_close_after_claim_test_hook(
	payload_mm_authvar_presence_lifecycle_close_test_hook_fn hook);
void payload_mm_authvar_presence_lifecycle_close_after_restrict_test_hook(
	payload_mm_authvar_presence_lifecycle_close_test_hook_fn hook);
void payload_mm_authvar_presence_lifecycle_close_after_proof_test_hook(
	payload_mm_authvar_presence_lifecycle_close_test_hook_fn hook);
void payload_mm_authvar_presence_lifecycle_close_after_complete_test_hook(
	payload_mm_authvar_presence_lifecycle_close_test_hook_fn hook);
uint64_t payload_mm_authvar_presence_lifecycle_close_value_test(uint64_t source,
	uint64_t generation,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation);
#endif

#endif
