/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TPM_CAPSULE_ANCHOR_POLICY_H
#define SECURITY_TPM_CAPSULE_ANCHOR_POLICY_H

#include <security/tpm/capsule_anchor_transition.h>

#define CAPSULE_TPM_ANCHOR_POLICY_PROVIDER_REVISION 2U

struct capsule_tpm_anchor_policy_callbacks {
	enum cb_err (*prepared)(const void *context,
		const struct capsule_tpm_anchor_grant *grant);
	enum cb_err (*install)(const void *context,
		const struct capsule_tpm_anchor_grant *grant,
		const struct capsule_tpm_anchor_binding *binding);
	const void *context;
	size_t context_size;
};

struct capsule_tpm_anchor_policy_provider {
	uint32_t revision;
	uint32_t size;
	uint32_t control;
	uint32_t write_attempted;
	uint32_t lock_attempted;
	struct capsule_tpm_anchor_policy_callbacks callbacks;
	struct capsule_tpm_anchor_policy_callbacks sealed_callbacks;
};

/* State must be zero-initialized and is consumed by the first call. */
enum cb_err capsule_tpm_anchor_policy_provider_init(
	struct capsule_tpm_anchor_policy_provider *state,
	const struct capsule_tpm_anchor_policy_callbacks *callbacks,
	struct capsule_tpm_anchor_transition_provider *provider);

#endif /* SECURITY_TPM_CAPSULE_ANCHOR_POLICY_H */
