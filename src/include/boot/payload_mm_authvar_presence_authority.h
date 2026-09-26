/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY_H

#include <boot/payload_mm_authvar.h>
#include <boot/payload_mm_authvar_presence.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_POLICY_REVISION 2U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_CONTEXT_MAX 128U

typedef enum cb_err (*payload_mm_authvar_presence_provision_fn)(void *context,
	uint64_t generation,
	uint8_t capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE]);
typedef bool (*payload_mm_authvar_presence_range_proof_fn)(void *context,
	uint64_t base, uint64_t size);
typedef bool (*payload_mm_authvar_presence_state_proof_fn)(void *context);
typedef void (*payload_mm_authvar_presence_cold_reset_fn)(void *context);
typedef void (*payload_mm_authvar_presence_fail_stop_fn)(void *context)
	__noreturn;

/* Protected SMM installation policy; this is not a shared-memory ABI. */
struct payload_mm_authvar_presence_policy {
	uint32_t revision;
	uint32_t size;
	struct lb_authvar_presence_endpoint endpoint;
	struct payload_mm_authvar_presence_backing backing;
	payload_mm_authvar_presence_provision_fn provision;
	payload_mm_authvar_presence_range_proof_fn dma_protected;
	payload_mm_authvar_presence_state_proof_fn cpu_rendezvous_active;
	payload_mm_authvar_presence_cold_reset_fn cold_reset;
	payload_mm_authvar_presence_fail_stop_fn fail_stop;
	void *context;
	size_t context_size;
};

enum cb_err payload_mm_authvar_presence_authority_install(
	const struct payload_mm_authvar_presence_policy *trusted_policy,
	payload_mm_authvar_protected_storage storage_is_protected,
	void *storage_context);

/*
 * Irreversibly restrict an authority installed for exactly generation.
 * Restricting the open matching generation closes it and scrubs its private
 * capability and context. Repeating the same generation after closure is an
 * idempotent success. Zero, stale, mutated, or otherwise invalid authority
 * state fails closed.
 */
enum cb_err payload_mm_authvar_presence_authority_restrict(
	uint64_t generation);

/* Compatibility wrapper for existing internal lifecycle callers. */
void payload_mm_authvar_presence_authority_close(void);

/* Exact fixed-mailbox dispatch and optional platform APM route. */
enum cb_err payload_mm_authvar_presence_authority_dispatch(void);
enum cb_err payload_mm_authvar_presence_smi_dispatch(uint16_t port,
	uint8_t value);

#if ENV_TEST
typedef void (*payload_mm_authvar_presence_restrict_test_hook_fn)(void);

void payload_mm_authvar_presence_authority_reset_test(void);
const void *payload_mm_authvar_presence_authority_test_state(size_t *size);
void payload_mm_authvar_presence_authority_restrict_test_hook(
	payload_mm_authvar_presence_restrict_test_hook_fn hook);
void payload_mm_authvar_presence_authority_restrict_claim_test_hook(
	payload_mm_authvar_presence_restrict_test_hook_fn hook);
void payload_mm_authvar_presence_authority_dispatch_finish_test_hook(
	payload_mm_authvar_presence_restrict_test_hook_fn hook);
void payload_mm_authvar_presence_authority_cleanup_test_hook(
	payload_mm_authvar_presence_restrict_test_hook_fn hook);
#endif

#endif
