/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TPM_PLATFORM_AUTH_H
#define SECURITY_TPM_PLATFORM_AUTH_H

#include <security/tpm/pre_os_lifecycle.h>

#define TPM2_PLATFORM_AUTH_NV_VALUE_SIZE 40U

struct tpm2_platform_auth_result {
	uint32_t response_code;
	uint32_t startup_clear;
	uint8_t delivered;
	uint8_t startup_clear_valid;
	uint8_t reserved[2];
};

/*
 * These dormant codecs require an already acquired exclusive lifecycle token.
 * CB_SUCCESS means a complete TPM success response passed strict validation.
 * A well-formed TPM error returns CB_ERR with delivered set and its response
 * code preserved. Transport and malformed responses return a cleared result.
 */
enum cb_err tpm2_platform_auth_nv_write(
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token, uint32_t nv_index,
	const uint8_t value[TPM2_PLATFORM_AUTH_NV_VALUE_SIZE],
	struct tpm2_platform_auth_result *result);
enum cb_err tpm2_platform_auth_nv_write_lock(
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token, uint32_t nv_index,
	struct tpm2_platform_auth_result *result);
enum cb_err tpm2_platform_auth_close_ph_enable(
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token,
	struct tpm2_platform_auth_result *result);
enum cb_err tpm2_platform_auth_verify_startup_clear(
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token,
	struct tpm2_platform_auth_result *result);

#endif /* SECURITY_TPM_PLATFORM_AUTH_H */
