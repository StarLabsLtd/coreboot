/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TPM_CAPSULE_ANCHOR_H
#define SECURITY_TPM_CAPSULE_ANCHOR_H

#include <types.h>

#define CAPSULE_TPM_ANCHOR_DESCRIPTOR_REVISION 1U
#define CAPSULE_TPM_ANCHOR_POLICY_REVISION 1U
#define CAPSULE_TPM_ANCHOR_PLATFORM_POLICY_REVISION 3U
#define CAPSULE_TPM_ANCHOR_SIZE 40U
#define CAPSULE_TPM_ANCHOR_POLICY_SIZE 32U
#define CAPSULE_TPM_ANCHOR_AUTHORITY_NAME_SIZE 34U
#define CAPSULE_TPM_ANCHOR_POLICY_REF_MAX_SIZE 32U

#define CAPSULE_TPM_ANCHOR_ATTRIBUTES \
	(BIT(3) | BIT(12) | BIT(14) | BIT(18) | BIT(25) | BIT(30))
#define CAPSULE_TPM_ANCHOR_PLATFORM_ATTRIBUTES \
	(BIT(0) | BIT(12) | BIT(14) | BIT(18) | BIT(25) | BIT(30))
#define CAPSULE_TPM_ANCHOR_WRITELOCKED BIT(11)
#define CAPSULE_TPM_ANCHOR_WRITTEN BIT(29)

/* Immutable platform policy describing one externally provisioned NV index. */
struct capsule_tpm_anchor_descriptor {
	uint32_t revision;
	uint32_t size;
	uint32_t policy_revision;
	uint32_t nv_index;
	uint32_t attributes;
	uint16_t name_algorithm;
	uint16_t data_size;
	uint16_t auth_policy_size;
	uint16_t authority_name_size;
	uint16_t policy_ref_size;
	uint16_t reserved;
	uint8_t auth_policy[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	uint8_t authority_name[CAPSULE_TPM_ANCHOR_AUTHORITY_NAME_SIZE];
	uint8_t policy_ref[CAPSULE_TPM_ANCHOR_POLICY_REF_MAX_SIZE];
	uint16_t reserved2;
};

/* Copied only after the TPM public area and descriptor match exactly. */
struct capsule_tpm_anchor_binding {
	uint32_t policy_revision;
	uint32_t nv_index;
	uint8_t authority_name[CAPSULE_TPM_ANCHOR_AUTHORITY_NAME_SIZE];
	uint16_t policy_ref_size;
	uint8_t policy_ref[CAPSULE_TPM_ANCHOR_POLICY_REF_MAX_SIZE];
	uint8_t write_locked;
	uint8_t reserved[3];
};

_Static_assert(sizeof(struct capsule_tpm_anchor_descriptor) == 132,
	"capsule TPM anchor descriptor ABI");
_Static_assert(sizeof(struct capsule_tpm_anchor_binding) == 80,
	"capsule TPM anchor binding ABI");

enum cb_err capsule_tpm_anchor_validate(
	const struct capsule_tpm_anchor_descriptor *descriptor,
	struct capsule_tpm_anchor_binding *binding);

/* Strict policy-revision-3 validator; never falls back to revision 1. */
enum cb_err capsule_tpm_anchor_platform_validate(
	const struct capsule_tpm_anchor_descriptor *descriptor,
	struct capsule_tpm_anchor_binding *binding);

bool capsule_tpm_anchor_platform_binding_valid(
	const struct capsule_tpm_anchor_binding *binding);

#endif /* SECURITY_TPM_CAPSULE_ANCHOR_H */
