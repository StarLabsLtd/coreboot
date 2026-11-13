/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PAYLOAD_MM_CRYPTO_INTERNAL_H
#define PAYLOAD_MM_CRYPTO_INTERNAL_H

#include "payload_mm_cms.h"

enum payload_mm_verify_status payload_mm_crypto_begin(
	struct payload_mm_crypto_owner *owner);
enum payload_mm_verify_status payload_mm_crypto_end(
	struct payload_mm_crypto_owner *owner,
	enum payload_mm_verify_status status);
/*
 * Recovery only at a quiescent boundary after a verifier returned while
 * retaining ownership. Atomics serialize state transitions; they do not make
 * cancellation safe while crypto code still uses an arena allocation.
 */
bool payload_mm_crypto_abort(struct payload_mm_crypto_owner *owner);
bool payload_mm_crypto_idle(void);
bool payload_mm_crypto_owner_is_clean(
	const struct payload_mm_crypto_owner *owner);

enum payload_mm_verify_status payload_mm_sha256(const void *message,
	size_t message_size, uint8_t digest[PAYLOAD_MM_SHA256_SIZE]);
enum payload_mm_verify_status payload_mm_sha256_spans(
	const struct payload_mm_crypto_span *spans, size_t count,
	uint8_t digest[PAYLOAD_MM_SHA256_SIZE]);
size_t payload_mm_hash_digest_size(enum payload_mm_hash_algorithm algorithm);
enum payload_mm_verify_status payload_mm_hash_spans(
	enum payload_mm_hash_algorithm algorithm,
	const struct payload_mm_crypto_span *spans, size_t count,
	uint8_t digest[PAYLOAD_MM_MAX_DIGEST_SIZE]);
enum payload_mm_verify_status payload_mm_rsa_verify(
	const struct payload_mm_crypto_span *certificate,
	enum payload_mm_hash_algorithm algorithm, const uint8_t *digest,
	size_t digest_size, const struct payload_mm_crypto_span *signature);
enum payload_mm_verify_status payload_mm_authvar_x509_chain_verify_detailed(
	const struct payload_mm_crypto_span *leaf,
	const struct payload_mm_crypto_span *intermediates,
	size_t intermediate_count,
	const struct payload_mm_crypto_span *trust_anchor);

#endif
