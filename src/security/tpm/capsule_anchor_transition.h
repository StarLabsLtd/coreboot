/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TPM_CAPSULE_ANCHOR_TRANSITION_H
#define SECURITY_TPM_CAPSULE_ANCHOR_TRANSITION_H

#include <security/tpm/capsule_anchor_grant.h>
#include <security/tpm/pre_os_lifecycle.h>

#define CAPSULE_TPM_ANCHOR_AUTHORIZATION_REVISION 1U
#define CAPSULE_TPM_ANCHOR_RSA_MAX_SIZE 512U
#define CAPSULE_TPM_ANCHOR_NONCE_SIZE 32U

/* Fixed, caller-owned PolicyAuthorize material. No private key is accepted. */
struct capsule_tpm_anchor_authorization {
	uint32_t revision;
	uint32_t size;
	uint32_t policy_revision;
	uint32_t nv_index;
	uint64_t generation;
	uint64_t transaction;
	struct capsule_tpm_anchor_value current;
	struct capsule_tpm_anchor_value candidate;
	uint8_t approved_policy[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	uint8_t cp_hash[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	uint8_t authority_name[CAPSULE_TPM_ANCHOR_AUTHORITY_NAME_SIZE];
	uint16_t policy_ref_size;
	uint16_t modulus_size;
	uint16_t signature_size;
	uint16_t reserved;
	uint8_t policy_ref[CAPSULE_TPM_ANCHOR_POLICY_REF_MAX_SIZE];
	uint8_t nonce[CAPSULE_TPM_ANCHOR_NONCE_SIZE];
	uint8_t modulus[CAPSULE_TPM_ANCHOR_RSA_MAX_SIZE];
	uint8_t signature[CAPSULE_TPM_ANCHOR_RSA_MAX_SIZE];
	uint32_t reserved2;
} __aligned(8);

_Static_assert(sizeof(struct capsule_tpm_anchor_authorization) == 1312,
	"capsule TPM anchor authorization ABI");

typedef enum cb_err capsule_tpm_anchor_transmit_fn(void *context,
	const uint8_t *request, size_t request_size, uint8_t *response,
	size_t *response_size);

struct capsule_tpm_anchor_transition_provider {
	uint32_t revision;
	uint32_t size;
	/* Independently read and verify the exact durable PREPARED record. */
	enum cb_err (*prepared)(const void *context,
		const struct capsule_tpm_anchor_grant *grant);
	/* Read the protected NV value using only the supplied transport. */
	enum cb_err (*read)(const void *context,
		capsule_tpm_anchor_transmit_fn *transmit, void *transmit_context,
		const struct capsule_tpm_anchor_binding *binding,
		struct capsule_tpm_anchor_value *value);
	/* Verify PolicyAuthorize and attempt exactly one replacement write. */
	enum cb_err (*advance)(const void *context,
		capsule_tpm_anchor_transmit_fn *transmit, void *transmit_context,
		const struct capsule_tpm_anchor_binding *binding,
		const struct capsule_tpm_anchor_authorization *authorization);
	/* Copy the final grant and binding into protected SMM storage once. */
	enum cb_err (*install)(const void *context,
		const struct capsule_tpm_anchor_grant *grant,
		const struct capsule_tpm_anchor_binding *binding);
	const void *context;
	size_t context_size;
};

#define CAPSULE_TPM_ANCHOR_TRANSITION_PROVIDER_REVISION 1U

struct capsule_tpm_anchor_transition {
	uint32_t control;
};

/*
 * Run once before any payload or option ROM. Every callback is synchronous.
 * The provider is deliberately absent from the tree until a platform can
 * independently prove the durable PREPARED record.
 */
enum cb_err capsule_tpm_anchor_transition_run(
	struct capsule_tpm_anchor_transition *transition,
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct capsule_tpm_anchor_binding *binding,
	const struct capsule_tpm_anchor_authorization *authorization,
	const struct capsule_tpm_anchor_transition_provider *provider);

#endif /* SECURITY_TPM_CAPSULE_ANCHOR_TRANSITION_H */
