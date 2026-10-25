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
#endif

#endif
