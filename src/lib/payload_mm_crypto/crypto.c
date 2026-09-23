/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_cms.h"
#include "payload_mm_crypto_platform.h"

#if CONFIG(PAYLOAD_MM_AUTHVAR_TRUST_ANCHOR)
#include <commonlib/helpers.h>
#endif
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
#include <mbedtls/sha512.h>
#endif
#include <mbedtls/x509_crt.h>

#include "crypto.h"

static struct payload_mm_crypto_owner *active_owner;

static bool allocation_request_valid(
	struct payload_mm_crypto_owner *owner, size_t count, size_t size,
	size_t *bytes)
{
	size_t aligned;

	if (owner == NULL || count == 0U || size == 0U ||
	    count > SIZE_MAX / size)
		return false;

	*bytes = count * size;
	if (*bytes > PAYLOAD_MM_CRYPTO_MAX_ALLOCATION_SIZE ||
	    owner->allocation_count >= PAYLOAD_MM_CRYPTO_MAX_ALLOCATIONS ||
	    *bytes > SIZE_MAX - 15U)
		return false;
	aligned = (*bytes + 15U) & ~(size_t)15U;
	return owner->arena_used <= sizeof(owner->arena) &&
		aligned <= sizeof(owner->arena) - owner->arena_used;
}

int payload_mm_crypto_snprintf(char *buffer, size_t size,
	const char *format, ...)
{
	(void)buffer;
	(void)size;
	(void)format;
	return -1;
}

void mbedtls_platform_zeroize(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size != 0U) {
		*bytes++ = 0U;
		size--;
	}
}

void *payload_mm_crypto_calloc(size_t count, size_t size)
{
	struct payload_mm_crypto_owner *owner = __atomic_load_n(&active_owner,
		__ATOMIC_RELAXED);
	size_t aligned;
	size_t bytes;
	void *allocation;

	if (!allocation_request_valid(owner, count, size, &bytes)) {
		if (owner != NULL)
			owner->allocation_failed = true;
		return NULL;
	}

	owner->allocation_count++;
#ifdef PAYLOAD_MM_AUTH_TEST
	if (owner->fail_allocation != 0U &&
	    owner->allocation_count == owner->fail_allocation) {
		owner->allocation_failed = true;
		return NULL;
	}
#endif
	aligned = (bytes + 15U) & ~(size_t)15U;
	allocation = owner->arena + owner->arena_used;
	memset(allocation, 0, aligned);
	owner->arena_used += aligned;
	return allocation;
}

void payload_mm_crypto_free(void *allocation)
{
	/* The complete protected arena is wiped when verification ends. */
	(void)allocation;
}

enum payload_mm_verify_status payload_mm_crypto_begin(
	struct payload_mm_crypto_owner *owner)
{
	struct payload_mm_crypto_owner *expected = NULL;

	if (owner == NULL)
		return PAYLOAD_MM_VERIFY_INVALID;
	if (owner->busy || !__atomic_compare_exchange_n(&active_owner, &expected,
		owner, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return PAYLOAD_MM_VERIFY_BUSY;

	owner->busy = true;
	owner->allocation_count = 0U;
	owner->arena_used = 0U;
	owner->allocation_failed = false;
	mbedtls_platform_zeroize(owner->arena, sizeof(owner->arena));
	return PAYLOAD_MM_VERIFY_OK;
}

enum payload_mm_verify_status payload_mm_crypto_end(
	struct payload_mm_crypto_owner *owner,
	enum payload_mm_verify_status status)
{
	if (owner->allocation_failed)
		status = PAYLOAD_MM_VERIFY_NO_MEMORY;
	mbedtls_platform_zeroize(owner->arena, sizeof(owner->arena));
	owner->arena_used = 0U;
	owner->allocation_failed = false;
	owner->busy = false;
	__atomic_store_n(&active_owner, NULL, __ATOMIC_RELEASE);
	return status;
}

static bool valid_span(const struct payload_mm_crypto_span *span,
	size_t maximum)
{
	return span != NULL && span->data != NULL && span->size != 0U &&
		span->size <= maximum;
}

static bool valid_certificate_set(
	const struct payload_mm_crypto_span *certificates, size_t count,
	size_t *total)
{
	size_t index;

	if (count > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES ||
	    (count != 0U && certificates == NULL))
		return false;
	for (index = 0U; index < count; index++) {
		if (!valid_span(&certificates[index],
			PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE) ||
		    certificates[index].size >
			PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_BYTES - *total)
			return false;
		*total += certificates[index].size;
	}
	return true;
}

static int parse_certificates(mbedtls_x509_crt *chain,
	const struct payload_mm_crypto_span *certificates, size_t count)
{
	mbedtls_x509_crt *parsed;
	size_t index;
	int result;

	for (index = 0U; index < count; index++) {
		result = mbedtls_x509_crt_parse_der_nocopy(chain,
			certificates[index].data, certificates[index].size);
		if (result != 0)
			return result;
		parsed = chain;
		while (parsed->next != NULL)
			parsed = parsed->next;
		if (parsed->raw.len != certificates[index].size)
			return MBEDTLS_ERR_X509_INVALID_FORMAT;
	}
	return 0;
}

static enum payload_mm_verify_status validate_rsa_certificates(const mbedtls_x509_crt *chain)
{
	const mbedtls_x509_crt *certificate;
	size_t bits;

	for (certificate = chain; certificate != NULL;
	     certificate = certificate->next) {
		if (!mbedtls_pk_can_do(&certificate->pk, MBEDTLS_PK_RSA))
			return PAYLOAD_MM_VERIFY_REJECTED;
		bits = mbedtls_pk_get_bitlen(&certificate->pk);
		if (bits < PAYLOAD_MM_CRYPTO_MIN_RSA_BITS ||
		    bits > PAYLOAD_MM_CRYPTO_MAX_RSA_BITS)
			return PAYLOAD_MM_VERIFY_REJECTED;
	}
	return PAYLOAD_MM_VERIFY_OK;
}

enum payload_mm_verify_status payload_mm_sha256(const void *message, size_t message_size,
	uint8_t digest[PAYLOAD_MM_SHA256_SIZE])
{
	const struct payload_mm_crypto_span span = {
		.data = message,
		.size = message_size,
	};

	return payload_mm_sha256_spans(&span, 1U, digest);
}

enum payload_mm_verify_status payload_mm_sha256_spans(
	const struct payload_mm_crypto_span *spans, size_t count,
	uint8_t digest[PAYLOAD_MM_SHA256_SIZE])
{
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	uint8_t full_digest[PAYLOAD_MM_MAX_DIGEST_SIZE];
	enum payload_mm_verify_status status;

	if (count > PAYLOAD_MM_SHA256_MAX_SPANS || digest == NULL)
		return PAYLOAD_MM_VERIFY_INVALID;
	status = payload_mm_hash_spans(PAYLOAD_MM_HASH_SHA256, spans, count,
		full_digest);
	if (status == PAYLOAD_MM_VERIFY_OK)
		memcpy(digest, full_digest, PAYLOAD_MM_SHA256_SIZE);
	mbedtls_platform_zeroize(full_digest, sizeof(full_digest));
	return status;
#else
	mbedtls_sha256_context context;
	size_t total = 0U;
	size_t index;
	int result;

	if (spans == NULL || count == 0U ||
	    count > PAYLOAD_MM_SHA256_MAX_SPANS || digest == NULL)
		return PAYLOAD_MM_VERIFY_INVALID;
	for (index = 0U; index < count; index++) {
		if ((spans[index].size != 0U && spans[index].data == NULL) ||
		    spans[index].size >
			PAYLOAD_MM_CRYPTO_MAX_MESSAGE_SIZE - total)
			return PAYLOAD_MM_VERIFY_INVALID;
		total += spans[index].size;
	}

	mbedtls_sha256_init(&context);
	result = mbedtls_sha256_starts(&context, 0);
	for (index = 0U; result == 0 && index < count; index++)
		result = mbedtls_sha256_update(&context, spans[index].data,
			spans[index].size);
	if (result == 0)
		result = mbedtls_sha256_finish(&context, digest);
	mbedtls_sha256_free(&context);
	return result == 0 ? PAYLOAD_MM_VERIFY_OK : PAYLOAD_MM_VERIFY_INTERNAL;
#endif
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
size_t payload_mm_hash_digest_size(enum payload_mm_hash_algorithm algorithm)
{
	switch (algorithm) {
	case PAYLOAD_MM_HASH_SHA256:
		return PAYLOAD_MM_SHA256_SIZE;
	case PAYLOAD_MM_HASH_SHA384:
		return PAYLOAD_MM_SHA384_SIZE;
	case PAYLOAD_MM_HASH_SHA512:
		return PAYLOAD_MM_SHA512_SIZE;
	default:
		return 0U;
	}
}

enum payload_mm_verify_status payload_mm_hash_spans(
	enum payload_mm_hash_algorithm algorithm,
	const struct payload_mm_crypto_span *spans, size_t count,
	uint8_t digest[PAYLOAD_MM_MAX_DIGEST_SIZE])
{
	mbedtls_sha256_context sha256;
	mbedtls_sha512_context sha512;
	size_t total = 0U;
	size_t index;
	int result;

	if (!payload_mm_hash_digest_size(algorithm) || spans == NULL || count == 0U ||
	    count > PAYLOAD_MM_HASH_MAX_SPANS || digest == NULL)
		return PAYLOAD_MM_VERIFY_INVALID;
	memset(digest, 0, PAYLOAD_MM_MAX_DIGEST_SIZE);
	for (index = 0U; index < count; index++) {
		if ((spans[index].size != 0U && spans[index].data == NULL) ||
		    (spans[index].size != 0U &&
		     (uintptr_t)spans[index].data >
			UINTPTR_MAX - spans[index].size) ||
		    spans[index].size > PAYLOAD_MM_CRYPTO_MAX_MESSAGE_SIZE - total)
			return PAYLOAD_MM_VERIFY_INVALID;
		total += spans[index].size;
	}
	if (algorithm == PAYLOAD_MM_HASH_SHA256) {
		mbedtls_sha256_init(&sha256);
		result = mbedtls_sha256_starts(&sha256, 0);
		for (index = 0U; result == 0 && index < count; index++)
			result = mbedtls_sha256_update(&sha256, spans[index].data,
				spans[index].size);
		if (result == 0)
			result = mbedtls_sha256_finish(&sha256, digest);
		mbedtls_sha256_free(&sha256);
	} else {
		mbedtls_sha512_init(&sha512);
		result = mbedtls_sha512_starts(&sha512,
			algorithm == PAYLOAD_MM_HASH_SHA384);
		for (index = 0U; result == 0 && index < count; index++)
			result = mbedtls_sha512_update(&sha512, spans[index].data,
				spans[index].size);
		if (result == 0)
			result = mbedtls_sha512_finish(&sha512, digest);
		mbedtls_sha512_free(&sha512);
	}
	if (result != 0)
		memset(digest, 0, PAYLOAD_MM_MAX_DIGEST_SIZE);
	return result == 0 ? PAYLOAD_MM_VERIFY_OK : PAYLOAD_MM_VERIFY_INTERNAL;
}

enum payload_mm_verify_status payload_mm_rsa_verify(
	const struct payload_mm_crypto_span *certificate,
	enum payload_mm_hash_algorithm algorithm, const uint8_t *digest,
	size_t digest_size,
	const struct payload_mm_crypto_span *signature)
{
	mbedtls_x509_crt parsed;
	enum payload_mm_verify_status status;
	mbedtls_md_type_t mbedtls_algorithm;
	size_t expected_digest_size;
	int result;

	expected_digest_size = payload_mm_hash_digest_size(algorithm);
	switch (algorithm) {
	case PAYLOAD_MM_HASH_SHA256:
		mbedtls_algorithm = MBEDTLS_MD_SHA256;
		break;
	case PAYLOAD_MM_HASH_SHA384:
		mbedtls_algorithm = MBEDTLS_MD_SHA384;
		break;
	case PAYLOAD_MM_HASH_SHA512:
		mbedtls_algorithm = MBEDTLS_MD_SHA512;
		break;
	default:
		return PAYLOAD_MM_VERIFY_INVALID;
	}
	if (!valid_span(certificate,
		PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE) || digest == NULL ||
	    digest_size != expected_digest_size ||
	    !valid_span(signature, PAYLOAD_MM_CRYPTO_MAX_SIGNATURE_SIZE))
		return PAYLOAD_MM_VERIFY_INVALID;

	mbedtls_x509_crt_init(&parsed);
	result = mbedtls_x509_crt_parse_der_nocopy(&parsed, certificate->data,
		certificate->size);
	if (result != 0 || parsed.raw.len != certificate->size) {
		status = PAYLOAD_MM_VERIFY_MALFORMED;
	} else if (!mbedtls_pk_can_do(&parsed.pk, MBEDTLS_PK_RSA)) {
		status = PAYLOAD_MM_VERIFY_UNSUPPORTED;
	} else if (mbedtls_pk_get_bitlen(&parsed.pk) <
		   PAYLOAD_MM_CRYPTO_MIN_RSA_BITS ||
		   mbedtls_pk_get_bitlen(&parsed.pk) >
		   PAYLOAD_MM_CRYPTO_MAX_RSA_BITS ||
		   mbedtls_pk_get_len(&parsed.pk) != signature->size) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
	} else if (mbedtls_pk_verify(&parsed.pk, mbedtls_algorithm, digest,
		digest_size, signature->data,
		signature->size) != 0) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
	} else {
		status = PAYLOAD_MM_VERIFY_OK;
	}
	mbedtls_x509_crt_free(&parsed);
	return status;
}
#endif

enum payload_mm_verify_status payload_mm_rsa_sha256_verify(
	const struct payload_mm_crypto_span *certificate,
	const uint8_t digest[PAYLOAD_MM_SHA256_SIZE],
	const struct payload_mm_crypto_span *signature)
{
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	return payload_mm_rsa_verify(certificate, PAYLOAD_MM_HASH_SHA256, digest,
		PAYLOAD_MM_SHA256_SIZE, signature);
#else
	mbedtls_x509_crt parsed;
	enum payload_mm_verify_status status;
	int result;

	if (!valid_span(certificate,
		PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE) || digest == NULL ||
	    !valid_span(signature, PAYLOAD_MM_CRYPTO_MAX_SIGNATURE_SIZE))
		return PAYLOAD_MM_VERIFY_INVALID;

	mbedtls_x509_crt_init(&parsed);
	result = mbedtls_x509_crt_parse_der_nocopy(&parsed, certificate->data,
		certificate->size);
	if (result != 0 || parsed.raw.len != certificate->size) {
		status = PAYLOAD_MM_VERIFY_MALFORMED;
	} else if (!mbedtls_pk_can_do(&parsed.pk, MBEDTLS_PK_RSA)) {
		status = PAYLOAD_MM_VERIFY_UNSUPPORTED;
	} else if (mbedtls_pk_get_bitlen(&parsed.pk) <
		   PAYLOAD_MM_CRYPTO_MIN_RSA_BITS ||
		   mbedtls_pk_get_bitlen(&parsed.pk) >
		   PAYLOAD_MM_CRYPTO_MAX_RSA_BITS ||
		   mbedtls_pk_get_len(&parsed.pk) != signature->size) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
	} else if (mbedtls_pk_verify(&parsed.pk, MBEDTLS_MD_SHA256, digest,
		PAYLOAD_MM_SHA256_SIZE, signature->data,
		signature->size) != 0) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
	} else {
		status = PAYLOAD_MM_VERIFY_OK;
	}
	mbedtls_x509_crt_free(&parsed);
	return status;
#endif
}

enum payload_mm_verify_status payload_mm_x509_chain_verify_detailed(
	const struct payload_mm_crypto_span *leaf,
	const struct payload_mm_crypto_span *intermediates,
	size_t intermediate_count,
	const struct payload_mm_crypto_span *trust_anchors,
	size_t trust_anchor_count,
	enum payload_mm_crypto_failure_source *failure_source)
{
	mbedtls_x509_crt chain;
	mbedtls_x509_crt trust;
	struct payload_mm_crypto_span leaf_set[1];
	enum payload_mm_verify_status status;
	size_t certificate_count;
	size_t total = 0U;
	uint32_t flags = 0U;
	int result;

	if (failure_source == NULL)
		return PAYLOAD_MM_VERIFY_INVALID;
	*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_TRANSIENT;
	if (!valid_span(leaf, PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE) ||
	    intermediate_count > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES - 1U ||
	    trust_anchor_count == 0U ||
	    trust_anchor_count > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES)
		return PAYLOAD_MM_VERIFY_INVALID;
	leaf_set[0] = *leaf;
	if (!valid_certificate_set(leaf_set, 1U, &total) ||
	    !valid_certificate_set(intermediates, intermediate_count, &total) ||
	    !valid_certificate_set(trust_anchors, trust_anchor_count, &total))
		return PAYLOAD_MM_VERIFY_INVALID;
	certificate_count = 1U + intermediate_count + trust_anchor_count;
	if (certificate_count > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES)
		return PAYLOAD_MM_VERIFY_INVALID;

	mbedtls_x509_crt_init(&chain);
	mbedtls_x509_crt_init(&trust);
	result = parse_certificates(&chain, leaf_set, 1U);
	if (result == 0)
		result = parse_certificates(&chain, intermediates,
			intermediate_count);
	if (result != 0) {
		status = PAYLOAD_MM_VERIFY_MALFORMED;
		*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_CAPSULE_FORMAT;
	} else if (validate_rsa_certificates(&chain) != PAYLOAD_MM_VERIFY_OK) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
		*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_CAPSULE_FORMAT;
	} else if (parse_certificates(&trust, trust_anchors,
		trust_anchor_count) != 0) {
		status = PAYLOAD_MM_VERIFY_MALFORMED;
		*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_TRUST;
	} else if (validate_rsa_certificates(&trust) != PAYLOAD_MM_VERIFY_OK) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
		*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_TRUST;
	} else if (mbedtls_x509_crt_verify(&chain, &trust, NULL, NULL, &flags,
		NULL, NULL) != 0 || flags != 0U) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
		*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_CAPSULE_SIGNATURE;
	} else {
		status = PAYLOAD_MM_VERIFY_OK;
	}
	mbedtls_x509_crt_free(&trust);
	mbedtls_x509_crt_free(&chain);
	return status;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_TRUST_ANCHOR)
static enum payload_mm_verify_status authvar_x509_chain_verify_detailed(
	const struct payload_mm_crypto_span *leaf,
	const struct payload_mm_crypto_span *intermediates,
	size_t intermediate_count,
	const struct payload_mm_crypto_span *trust_anchor,
	enum payload_mm_crypto_failure_source *failure_source)
{
	mbedtls_x509_crt chain;
	mbedtls_x509_crt trust;
	struct payload_mm_crypto_span leaf_set[1];
	enum payload_mm_verify_status status;
	size_t certificate_count;
	size_t total = 0U;
	uint32_t flags = 0U;
	int result;

	if (failure_source == NULL)
		return PAYLOAD_MM_VERIFY_INVALID;
	*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_TRANSIENT;
	if (!valid_span(leaf, PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE) ||
	    intermediate_count > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES - 1U ||
	    !valid_span(trust_anchor, PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE))
		return PAYLOAD_MM_VERIFY_INVALID;
	leaf_set[0] = *leaf;
	if (!valid_certificate_set(leaf_set, 1U, &total) ||
	    !valid_certificate_set(intermediates, intermediate_count, &total) ||
	    !valid_certificate_set(trust_anchor, 1U, &total))
		return PAYLOAD_MM_VERIFY_INVALID;
	certificate_count = 2U + intermediate_count;
	if (certificate_count > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES + 1U)
		return PAYLOAD_MM_VERIFY_INVALID;

	mbedtls_x509_crt_init(&chain);
	mbedtls_x509_crt_init(&trust);
	result = parse_certificates(&chain, leaf_set, 1U);
	if (result == 0)
		result = parse_certificates(&chain, intermediates,
			intermediate_count);
	if (result != 0) {
		status = PAYLOAD_MM_VERIFY_MALFORMED;
		*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_CAPSULE_FORMAT;
	} else if (validate_rsa_certificates(&chain) != PAYLOAD_MM_VERIFY_OK) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
		*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_CAPSULE_FORMAT;
	} else if (parse_certificates(&trust, trust_anchor, 1U) != 0) {
		status = PAYLOAD_MM_VERIFY_MALFORMED;
		*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_TRUST;
	} else if (validate_rsa_certificates(&trust) != PAYLOAD_MM_VERIFY_OK) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
		*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_TRUST;
	} else if (mbedtls_x509_crt_verify(&chain, &trust, NULL, NULL, &flags,
		NULL, NULL) != 0 || flags != 0U) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
		*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_CAPSULE_SIGNATURE;
	} else {
		status = PAYLOAD_MM_VERIFY_OK;
	}
	mbedtls_x509_crt_free(&trust);
	mbedtls_x509_crt_free(&chain);
	return status;
}

static uint8_t ascii_lower(uint8_t character)
{
	return character >= 'A' && character <= 'Z' ? character + 'a' - 'A' :
		character;
}

static bool x509_string_equal(const mbedtls_x509_buf *left,
	const mbedtls_x509_buf *right)
{
	size_t index;

	if (left->tag == right->tag && left->len == right->len &&
	    !memcmp(left->p, right->p, left->len))
		return true;
	if ((left->tag != MBEDTLS_ASN1_UTF8_STRING &&
	     left->tag != MBEDTLS_ASN1_PRINTABLE_STRING) ||
	    (right->tag != MBEDTLS_ASN1_UTF8_STRING &&
	     right->tag != MBEDTLS_ASN1_PRINTABLE_STRING) ||
	    left->len != right->len)
		return false;
	for (index = 0U; index < left->len; index++)
		if (ascii_lower(left->p[index]) != ascii_lower(right->p[index]))
			return false;
	return true;
}

static bool x509_name_equal(const mbedtls_x509_name *left,
	const mbedtls_x509_name *right)
{
	while (left != NULL || right != NULL) {
		if (left == NULL || right == NULL ||
		    left->oid.tag != right->oid.tag ||
		    left->oid.len != right->oid.len ||
		    memcmp(left->oid.p, right->oid.p, left->oid.len) ||
		    !x509_string_equal(&left->val, &right->val) ||
		    left->MBEDTLS_PRIVATE(next_merged) !=
			right->MBEDTLS_PRIVATE(next_merged))
			return false;
		left = left->next;
		right = right->next;
	}
	return true;
}

enum payload_mm_verify_status payload_mm_authvar_x509_chain_verify_detailed(
	const struct payload_mm_crypto_span *leaf,
	const struct payload_mm_crypto_span *intermediates,
	size_t intermediate_count,
	const struct payload_mm_crypto_span *trust_anchor,
	enum payload_mm_crypto_failure_source *failure_source)
{
	mbedtls_x509_crt parsed[PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES - 1U];
	struct payload_mm_crypto_span ordered[
		PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES - 1U];
	mbedtls_x509_crt parsed_anchor;
	mbedtls_x509_crt parsed_leaf;
	const mbedtls_x509_crt *child;
	bool used[PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES - 1U] = { false };
	enum payload_mm_verify_status status = PAYLOAD_MM_VERIFY_MALFORMED;
	size_t ordered_count = 0U;
	size_t match;
	size_t matches;
	size_t index;
	int result;

	if (failure_source == NULL ||
	    !valid_span(leaf, PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE) ||
	    !valid_span(trust_anchor, PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE) ||
	    intermediate_count > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES - 1U ||
	    (intermediate_count != 0U && intermediates == NULL))
		return PAYLOAD_MM_VERIFY_INVALID;

	*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_TRUST;
	mbedtls_x509_crt_init(&parsed_leaf);
	mbedtls_x509_crt_init(&parsed_anchor);
	for (index = 0U; index < ARRAY_SIZE(parsed); index++)
		mbedtls_x509_crt_init(&parsed[index]);
	result = mbedtls_x509_crt_parse_der_nocopy(&parsed_leaf, leaf->data,
		leaf->size);
	if (result == 0 && (parsed_leaf.raw.len != leaf->size || parsed_leaf.next))
		result = MBEDTLS_ERR_X509_INVALID_FORMAT;
	if (result == 0)
		result = mbedtls_x509_crt_parse_der_nocopy(&parsed_anchor,
			trust_anchor->data, trust_anchor->size);
	if (result == 0 && (parsed_anchor.raw.len != trust_anchor->size ||
	    parsed_anchor.next))
		result = MBEDTLS_ERR_X509_INVALID_FORMAT;
	for (index = 0U; result == 0 && index < intermediate_count; index++) {
		result = mbedtls_x509_crt_parse_der_nocopy(&parsed[index],
			intermediates[index].data, intermediates[index].size);
		if (result == 0 &&
		    (parsed[index].raw.len != intermediates[index].size ||
		     parsed[index].next))
			result = MBEDTLS_ERR_X509_INVALID_FORMAT;
	}
	if (result != 0)
		goto out;

	child = &parsed_leaf;
	while (!x509_name_equal(&child->issuer, &parsed_anchor.subject)) {
		matches = 0U;
		match = 0U;
		for (index = 0U; index < intermediate_count; index++) {
			if (!used[index] && x509_name_equal(&child->issuer,
				&parsed[index].subject)) {
				match = index;
				matches++;
			}
		}
		if (matches != 1U) {
			status = PAYLOAD_MM_VERIFY_REJECTED;
			goto out;
		}
		used[match] = true;
		ordered[ordered_count++] = intermediates[match];
		child = &parsed[match];
	}
	status = authvar_x509_chain_verify_detailed(leaf, ordered, ordered_count,
		trust_anchor, failure_source);
out:
	for (index = 0U; index < ARRAY_SIZE(parsed); index++)
		mbedtls_x509_crt_free(&parsed[index]);
	mbedtls_x509_crt_free(&parsed_anchor);
	mbedtls_x509_crt_free(&parsed_leaf);
	return status;
}
#endif
