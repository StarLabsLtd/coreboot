/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PAYLOAD_MM_CMS_H
#define PAYLOAD_MM_CMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <commonlib/bsd/compiler.h>

#define PAYLOAD_MM_SHA256_SIZE 32U
#define PAYLOAD_MM_SHA384_SIZE 48U
#define PAYLOAD_MM_SHA512_SIZE 64U
#define PAYLOAD_MM_MAX_DIGEST_SIZE PAYLOAD_MM_SHA512_SIZE
#define PAYLOAD_MM_SHA256_MAX_SPANS 2U
#define PAYLOAD_MM_HASH_MAX_SPANS 5U
#define PAYLOAD_MM_CRYPTO_MAX_MESSAGE_SIZE (128U * 1024U * 1024U)
#define PAYLOAD_MM_MAX_SIGNED_BODY_SIZE \
	(PAYLOAD_MM_CRYPTO_MAX_MESSAGE_SIZE - sizeof(uint64_t))
#define PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE (64U * 1024U)
#define PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES 8U
#define PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_BYTES (256U * 1024U)
#define PAYLOAD_MM_CRYPTO_MAX_SIGNATURE_SIZE 1024U
#define PAYLOAD_MM_CRYPTO_MIN_RSA_BITS 2048U
#define PAYLOAD_MM_CRYPTO_MAX_RSA_BITS 8192U
#define PAYLOAD_MM_CRYPTO_MAX_ALLOCATION_SIZE (128U * 1024U)
#define PAYLOAD_MM_CRYPTO_MAX_ALLOCATIONS 256U
#define PAYLOAD_MM_CRYPTO_ARENA_SIZE (512U * 1024U)
#define PAYLOAD_MM_MAX_CMS_SIZE (256U * 1024U)
#define PAYLOAD_MM_MAX_CMS_ATTRIBUTES 16U
#define PAYLOAD_MM_MAX_CMS_ATTRIBUTE_SIZE (16U * 1024U)
#define PAYLOAD_MM_MAX_CMS_OID_SIZE 64U
#define PAYLOAD_MM_MAX_DER_DEPTH 12U
#define PAYLOAD_MM_MAX_DER_OBJECTS 512U
#define PAYLOAD_MM_MAX_TRUST_XDR_SIZE (256U * 1024U)
#define PAYLOAD_MM_AUTH_HEADER_SIZE 32U

enum payload_mm_verify_status {
	PAYLOAD_MM_VERIFY_OK = 0,
	PAYLOAD_MM_VERIFY_INVALID,
	PAYLOAD_MM_VERIFY_BUSY,
	PAYLOAD_MM_VERIFY_NO_MEMORY,
	PAYLOAD_MM_VERIFY_MALFORMED,
	PAYLOAD_MM_VERIFY_UNSUPPORTED,
	PAYLOAD_MM_VERIFY_REJECTED,
	PAYLOAD_MM_VERIFY_INTERNAL,
	PAYLOAD_MM_VERIFY_CHANGED,
};

enum payload_mm_hash_algorithm {
	PAYLOAD_MM_HASH_SHA256 = 1,
	PAYLOAD_MM_HASH_SHA384 = 2,
	PAYLOAD_MM_HASH_SHA512 = 3,
};

enum payload_mm_auth_failure_source {
	PAYLOAD_MM_AUTH_FAILURE_NONE = 0,
	PAYLOAD_MM_AUTH_FAILURE_CAPSULE_FORMAT,
	PAYLOAD_MM_AUTH_FAILURE_CAPSULE_SIGNATURE,
	PAYLOAD_MM_AUTH_FAILURE_TRUST,
	PAYLOAD_MM_AUTH_FAILURE_TRANSIENT,
};

struct payload_mm_crypto_span {
	const uint8_t *data;
	size_t size;
};

/*
 * The owner belongs in protected memory. Verification admits one caller at a
 * time, uses no general allocator, and wipes the complete arena on every exit.
 * Zero-initialize it before its first use.
 */
struct payload_mm_crypto_owner {
	uint8_t arena[PAYLOAD_MM_CRYPTO_ARENA_SIZE] __aligned(16);
	size_t arena_used;
	size_t allocation_count;
#ifdef PAYLOAD_MM_AUTH_TEST
	size_t fail_allocation;
#endif
	bool busy;
	bool allocation_failed;
};

struct payload_mm_authenticated_image {
	struct payload_mm_crypto_span payload;
	uint64_t monotonic_count;
	enum payload_mm_auth_failure_source failure_source;
};

/*
 * Signature verification only: these input-backed certificate views are not
 * trust decisions and expire with the caller's immutable CMS buffer.
 */
struct payload_mm_cms_verified_signer {
	enum payload_mm_hash_algorithm digest_algorithm;
	struct payload_mm_crypto_span signer_certificate;
	struct payload_mm_crypto_span certificates[PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES];
	size_t certificate_count;
};

#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
enum payload_mm_verify_status payload_mm_cms_verify_detached_untrusted(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content, size_t content_count,
	struct payload_mm_cms_verified_signer *verified);
#endif

/* The production entry point derives the signed digest from image bytes. */
enum payload_mm_verify_status payload_mm_authenticate_image(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *image,
	const struct payload_mm_crypto_span *trust_xdr,
	struct payload_mm_authenticated_image *authenticated);

#ifdef PAYLOAD_MM_AUTH_TEST
enum payload_mm_verify_status payload_mm_cms_verify(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const uint8_t content_digest[PAYLOAD_MM_SHA256_SIZE],
	const struct payload_mm_crypto_span *trust_xdr);
bool payload_mm_auth_test_time_is_canonical(uint8_t tag,
	const uint8_t *value, size_t value_size);
bool payload_mm_auth_test_der_is_canonical(const uint8_t *der,
	size_t der_size);
bool payload_mm_auth_test_attribute_size_is_valid(size_t size);
#endif

#endif
