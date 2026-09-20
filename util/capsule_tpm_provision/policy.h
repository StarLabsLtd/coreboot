/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CAPSULE_TPM_PROVISION_POLICY_H
#define CAPSULE_TPM_PROVISION_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAPSULE_TPM_PROVISION_DIGEST_SIZE 32U
#define CAPSULE_TPM_PROVISION_NAME_SIZE 34U
#define CAPSULE_TPM_PROVISION_POLICY_REF_MAX_SIZE 32U
#define CAPSULE_TPM_PROVISION_RSA_MAX_SIZE 512U
#define CAPSULE_TPM_PROVISION_ANCHOR_SIZE 40U
#define CAPSULE_TPM_PROVISION_DESCRIPTOR_SIZE 132U
#define CAPSULE_TPM_PROVISION_NV_PUBLIC_SIZE 48U
#define CAPSULE_TPM_PROVISION_RSA_PUBLIC_MAX_SIZE 538U

struct capsule_tpm_provision_input {
	uint32_t nv_index;
	uint64_t epoch;
	uint8_t manifest_digest[CAPSULE_TPM_PROVISION_DIGEST_SIZE];
	uint16_t rsa_modulus_size;
	uint8_t rsa_modulus[CAPSULE_TPM_PROVISION_RSA_MAX_SIZE];
	uint16_t policy_ref_size;
	uint8_t policy_ref[CAPSULE_TPM_PROVISION_POLICY_REF_MAX_SIZE];
};

struct capsule_tpm_provision_artifacts {
	uint8_t descriptor[CAPSULE_TPM_PROVISION_DESCRIPTOR_SIZE];
	uint8_t anchor[CAPSULE_TPM_PROVISION_ANCHOR_SIZE];
	uint16_t rsa_public_size;
	uint8_t rsa_public[CAPSULE_TPM_PROVISION_RSA_PUBLIC_MAX_SIZE];
	uint8_t authority_name[CAPSULE_TPM_PROVISION_NAME_SIZE];
	uint8_t nv_public[CAPSULE_TPM_PROVISION_NV_PUBLIC_SIZE];
	uint8_t nv_name[CAPSULE_TPM_PROVISION_NAME_SIZE];
	uint8_t written_nv_name[CAPSULE_TPM_PROVISION_NAME_SIZE];
	uint8_t write_branch[CAPSULE_TPM_PROVISION_DIGEST_SIZE];
	uint8_t lock_branch[CAPSULE_TPM_PROVISION_DIGEST_SIZE];
	uint8_t policy_or_digests[2 * CAPSULE_TPM_PROVISION_DIGEST_SIZE];
	uint8_t auth_policy[CAPSULE_TPM_PROVISION_DIGEST_SIZE];
	uint8_t write_cp_hash[CAPSULE_TPM_PROVISION_DIGEST_SIZE];
	uint8_t write_approved_policy[CAPSULE_TPM_PROVISION_DIGEST_SIZE];
	uint8_t write_authorization_hash[CAPSULE_TPM_PROVISION_DIGEST_SIZE];
	uint8_t lock_cp_hash[CAPSULE_TPM_PROVISION_DIGEST_SIZE];
	uint8_t lock_approved_policy[CAPSULE_TPM_PROVISION_DIGEST_SIZE];
	uint8_t lock_authorization_hash[CAPSULE_TPM_PROVISION_DIGEST_SIZE];
};

bool capsule_tpm_provision_generate(
	const struct capsule_tpm_provision_input *input,
	struct capsule_tpm_provision_artifacts *artifacts);

#endif /* CAPSULE_TPM_PROVISION_POLICY_H */
