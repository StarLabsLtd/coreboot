/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TPM_CAPSULE_ANCHOR_PLATFORM_H
#define SECURITY_TPM_CAPSULE_ANCHOR_PLATFORM_H

#include <security/tpm/capsule_anchor_grant.h>

#define CAPSULE_TPM_ANCHOR_PLATFORM_REQUEST_REVISION 1U

struct capsule_tpm_anchor_platform_request {
	uint32_t revision;
	uint32_t size;
	uint32_t policy_revision;
	uint32_t nv_index;
	uint64_t generation;
	uint64_t transaction;
	struct capsule_tpm_anchor_value current;
	struct capsule_tpm_anchor_value candidate;
} __aligned(8);

_Static_assert(sizeof(struct capsule_tpm_anchor_platform_request) == 112,
	"capsule TPM platform request layout");
_Static_assert(offsetof(struct capsule_tpm_anchor_platform_request,
	generation) == 16 &&
	offsetof(struct capsule_tpm_anchor_platform_request, current) == 32 &&
	offsetof(struct capsule_tpm_anchor_platform_request, candidate) == 72,
	"capsule TPM platform request offsets");

bool capsule_tpm_anchor_platform_request_valid(
	const struct capsule_tpm_anchor_platform_request *request,
	const struct capsule_tpm_anchor_binding *binding);
enum cb_err capsule_tpm_anchor_platform_grant_validate(
	const struct capsule_tpm_anchor_grant *grant,
	const struct capsule_tpm_anchor_binding *binding);

#if ENV_SMM || ENV_TEST
enum cb_err capsule_tpm_anchor_platform_grant_install(
	const struct capsule_tpm_anchor_grant *trusted_grant,
	const struct capsule_tpm_anchor_binding *trusted_binding,
	capsule_tpm_anchor_grant_protected_storage storage_is_protected,
	void *context);
enum cb_err capsule_tpm_anchor_platform_grant_consume(uint64_t generation,
	uint64_t transaction, const struct capsule_tpm_anchor_value *current,
	const struct capsule_tpm_anchor_value *candidate);
bool capsule_tpm_anchor_platform_grant_ready(void);
#endif

#endif /* SECURITY_TPM_CAPSULE_ANCHOR_PLATFORM_H */
