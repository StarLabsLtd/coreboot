/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PAYLOAD_MM_AUTHVAR_PRIVATE_BINDING_H
#define PAYLOAD_MM_AUTHVAR_PRIVATE_BINDING_H

#include <payload_mm_cms.h>

struct payload_mm_authvar_private_binding {
	size_t size;
	uint8_t digest[PAYLOAD_MM_MAX_DIGEST_SIZE];
};

enum payload_mm_verify_status payload_mm_authvar_private_binding_derive(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_cms_verified_signer *verified,
	struct payload_mm_authvar_private_binding *binding);

enum payload_mm_verify_status payload_mm_authvar_private_binding_match(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_cms_verified_signer *verified,
	const struct payload_mm_crypto_span *existing);

#ifdef PAYLOAD_MM_AUTH_TEST
enum payload_mm_verify_status payload_mm_auth_test_private_binding_convert_cn(
	uint8_t tag, const uint8_t *input, size_t input_size,
	uint8_t output[127], size_t *output_size);
enum payload_mm_verify_status payload_mm_auth_test_private_binding_hash(
	enum payload_mm_hash_algorithm algorithm, uint8_t tag,
	const uint8_t *input, size_t input_size,
	const struct payload_mm_crypto_span *tbs,
	uint8_t digest[PAYLOAD_MM_MAX_DIGEST_SIZE]);
#endif

#endif
