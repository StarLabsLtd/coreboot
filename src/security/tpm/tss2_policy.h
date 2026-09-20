/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TPM_TSS2_POLICY_H
#define SECURITY_TPM_TSS2_POLICY_H

#include <security/tpm/tss_errors.h>
#include <types.h>

#define TLCL2_POLICY_DIGEST_MAX_SIZE 64U
#define TLCL2_POLICY_OR_MAX_DIGESTS 8U
#define TLCL2_POLICY_OPERAND_MAX_SIZE 64U
#define TLCL2_RSA_MODULUS_MAX_SIZE 512U
#define TLCL2_RSA_NAME_SIZE 34U

struct tlcl2_policy_session {
	uint32_t handle;
	uint16_t hash_algorithm;
	uint16_t nonce_size;
	uint8_t nonce[TLCL2_POLICY_DIGEST_MAX_SIZE];
};

struct tlcl2_policy_authorization {
	uint32_t handle;
	uint16_t nonce_size;
	uint8_t nonce[TLCL2_POLICY_DIGEST_MAX_SIZE];
	uint8_t attributes;
	uint16_t auth_size;
	uint8_t auth[TLCL2_POLICY_DIGEST_MAX_SIZE];
};

struct tlcl2_rsa_public_key {
	uint16_t modulus_size;
	uint8_t modulus[TLCL2_RSA_MODULUS_MAX_SIZE];
};

struct tlcl2_external_object {
	uint32_t handle;
	uint16_t name_size;
	uint8_t name[TLCL2_RSA_NAME_SIZE];
	uint16_t rsa_modulus_size;
};

struct tlcl2_verified_ticket {
	uint16_t tag;
	uint32_t hierarchy;
	uint16_t digest_size;
	uint8_t digest[TLCL2_POLICY_DIGEST_MAX_SIZE];
};

typedef tpm_result_t (*tlcl2_policy_session_fn)(
	const struct tlcl2_policy_session *session, void *context);
typedef tpm_result_t (*tlcl2_external_object_fn)(
	const struct tlcl2_external_object *object, void *context);

tpm_result_t tlcl2_policy_session_start(uint16_t hash_algorithm,
	const uint8_t *caller_nonce, size_t caller_nonce_size,
	struct tlcl2_policy_session *session);
tpm_result_t tlcl2_policy_session_flush(
	struct tlcl2_policy_session *session);
tpm_result_t tlcl2_policy_session_run(uint16_t hash_algorithm,
	const uint8_t *caller_nonce, size_t caller_nonce_size,
	tlcl2_policy_session_fn run, void *context);

tpm_result_t tlcl2_policy_nv_written(
	const struct tlcl2_policy_session *session, bool written);
tpm_result_t tlcl2_policy_nv(const struct tlcl2_policy_session *session,
	uint32_t nv_index, const struct tlcl2_policy_authorization *authorization,
	const uint8_t *operand, size_t operand_size, uint16_t offset,
	uint16_t operation);
tpm_result_t tlcl2_policy_cp_hash(
	const struct tlcl2_policy_session *session, const uint8_t *digest,
	size_t digest_size);
tpm_result_t tlcl2_policy_command_code(
	const struct tlcl2_policy_session *session, uint32_t command_code);
tpm_result_t tlcl2_policy_or(const struct tlcl2_policy_session *session,
	const uint8_t *digests, size_t digest_count, size_t digest_size);

tpm_result_t tlcl2_load_external_rsa(
	const struct tlcl2_rsa_public_key *public_key,
	struct tlcl2_external_object *object);
tpm_result_t tlcl2_external_object_flush(
	struct tlcl2_external_object *object);
tpm_result_t tlcl2_external_object_run(
	const struct tlcl2_rsa_public_key *public_key,
	tlcl2_external_object_fn run, void *context);
tpm_result_t tlcl2_verify_rsa_signature(
	const struct tlcl2_external_object *object, const uint8_t *digest,
	size_t digest_size, const uint8_t *signature, size_t signature_size,
	struct tlcl2_verified_ticket *ticket);
tpm_result_t tlcl2_policy_authorize(
	const struct tlcl2_policy_session *session,
	const uint8_t *approved_policy, size_t approved_policy_size,
	const uint8_t *policy_ref, size_t policy_ref_size,
	const uint8_t *key_name, size_t key_name_size,
	const struct tlcl2_verified_ticket *ticket);
tpm_result_t tlcl2_policy_nv_write(
	const struct tlcl2_policy_session *session, uint32_t nv_index,
	const uint8_t *data, size_t data_size, uint16_t offset);
tpm_result_t tlcl2_policy_nv_write_lock(
	const struct tlcl2_policy_session *session, uint32_t nv_index);

#endif /* SECURITY_TPM_TSS2_POLICY_H */
