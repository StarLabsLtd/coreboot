/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TPM_CAPSULE_ANCHOR_GRANT_H
#define SECURITY_TPM_CAPSULE_ANCHOR_GRANT_H

#include <security/tpm/capsule_anchor.h>

#define CAPSULE_TPM_ANCHOR_GRANT_REVISION 1U
#define CAPSULE_TPM_ANCHOR_GRANT_MEDIA_PREPARED BIT(0)
#define CAPSULE_TPM_ANCHOR_GRANT_TPM_ADVANCED BIT(1)
#define CAPSULE_TPM_ANCHOR_GRANT_TPM_READBACK_VERIFIED BIT(2)
#define CAPSULE_TPM_ANCHOR_GRANT_TPM_RELEASED BIT(3)
#define CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS \
	(CAPSULE_TPM_ANCHOR_GRANT_MEDIA_PREPARED | \
	 CAPSULE_TPM_ANCHOR_GRANT_TPM_ADVANCED | \
	 CAPSULE_TPM_ANCHOR_GRANT_TPM_READBACK_VERIFIED | \
	 CAPSULE_TPM_ANCHOR_GRANT_TPM_RELEASED)

struct capsule_tpm_anchor_value {
	uint64_t epoch;
	uint8_t digest[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
} __aligned(8);

/* Fixed pre-OS-to-SMM fact; it contains no address or callable object. */
struct capsule_tpm_anchor_grant {
	uint32_t revision;
	uint32_t size;
	uint32_t policy_revision;
	uint32_t nv_index;
	uint64_t generation;
	uint64_t transaction;
	uint32_t flags;
	uint32_t reserved;
	struct capsule_tpm_anchor_value current;
	struct capsule_tpm_anchor_value candidate;
} __aligned(8);

_Static_assert(sizeof(struct capsule_tpm_anchor_value) == 40,
	"capsule TPM anchor value ABI");
_Static_assert(sizeof(struct capsule_tpm_anchor_grant) == 120,
	"capsule TPM anchor grant ABI");
_Static_assert(offsetof(struct capsule_tpm_anchor_grant, generation) == 16 &&
	offsetof(struct capsule_tpm_anchor_grant, current) == 40 &&
	offsetof(struct capsule_tpm_anchor_grant, candidate) == 80,
	"capsule TPM anchor grant layout");

typedef bool (*capsule_tpm_anchor_grant_protected_storage)(void *context,
	const void *storage, size_t size);

enum cb_err capsule_tpm_anchor_grant_validate(
	const struct capsule_tpm_anchor_grant *grant,
	const struct capsule_tpm_anchor_binding *binding);

#if ENV_SMM || ENV_TEST
/* SMM copies one trusted grant exactly once into protected private storage. */
enum cb_err capsule_tpm_anchor_grant_install(
	const struct capsule_tpm_anchor_grant *trusted_grant,
	const struct capsule_tpm_anchor_binding *trusted_binding,
	capsule_tpm_anchor_grant_protected_storage storage_is_protected,
	void *context);

/* The first consume attempt is terminal, including a mismatched attempt. */
enum cb_err capsule_tpm_anchor_grant_consume(uint64_t generation,
	uint64_t transaction, const struct capsule_tpm_anchor_value *current,
	const struct capsule_tpm_anchor_value *candidate);

bool capsule_tpm_anchor_grant_ready(void);
#endif

#endif /* SECURITY_TPM_CAPSULE_ANCHOR_GRANT_H */
