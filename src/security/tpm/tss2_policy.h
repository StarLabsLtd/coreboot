/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TPM_TSS2_POLICY_H
#define SECURITY_TPM_TSS2_POLICY_H

#include <security/tpm/tss_errors.h>
#include <types.h>

#define TLCL2_POLICY_DIGEST_MAX_SIZE 64U
#define TLCL2_POLICY_OR_MAX_DIGESTS 8U
#define TLCL2_POLICY_OPERAND_MAX_SIZE 64U

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

typedef tpm_result_t (*tlcl2_policy_session_fn)(
	const struct tlcl2_policy_session *session, void *context);

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

#endif /* SECURITY_TPM_TSS2_POLICY_H */
