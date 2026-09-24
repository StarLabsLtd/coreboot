/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_authvar_private_binding.h"

#include "payload_mm_crypto/crypto.h"

#include <commonlib/helpers.h>
#include <mbedtls/asn1.h>
#include <mbedtls/oid.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/x509_crt.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define EDK_PRIVATE_CN_CAPACITY 127U
#define LEGACY_CERT_STACK_HEADER_SIZE 5U

static bool range_valid(const void *data, size_t size)
{
	return size == 0U ||
		(data != NULL && (uintptr_t)data <= UINTPTR_MAX - size);
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_address = (uintptr_t)left;
	uintptr_t right_address = (uintptr_t)right;

	if (!range_valid(left, left_size) || !range_valid(right, right_size))
		return true;
	if (!left_size || !right_size)
		return false;
	if (left_address <= right_address)
		return right_address - left_address < left_size;
	return left_address - right_address < right_size;
}

static bool descriptors_valid(const struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_cms_verified_signer *verified,
	const void *extra, size_t extra_size)
{
	const void *descriptors[] = { owner, signed_data, verified, extra };
	const size_t sizes[] = {
		sizeof(*owner), sizeof(*signed_data), sizeof(*verified), extra_size,
	};

	if (!owner || !signed_data || !verified || (extra_size && !extra))
		return false;
	if ((uintptr_t)owner % _Alignof(*owner) ||
	    (uintptr_t)signed_data % _Alignof(*signed_data) ||
	    (uintptr_t)verified % _Alignof(*verified) ||
	    !range_valid(owner, sizeof(*owner)) ||
	    !range_valid(signed_data, sizeof(*signed_data)) ||
	    !range_valid(verified, sizeof(*verified)) ||
	    !range_valid(extra, extra_size))
		return false;
	for (size_t left = 0U; left < ARRAY_SIZE(descriptors); left++)
		for (size_t right = left + 1U; right < ARRAY_SIZE(descriptors); right++)
			if (ranges_overlap(descriptors[left], sizes[left],
				descriptors[right], sizes[right]))
				return false;
	return true;
}

static bool inputs_valid(const struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_cms_verified_signer *verified)
{
	const void *descriptors[] = { owner, signed_data, verified };
	const size_t descriptor_sizes[] = {
		sizeof(*owner), sizeof(*signed_data), sizeof(*verified),
	};
	size_t signer_matches = 0U;
	size_t left;
	size_t right;

	if (!descriptors_valid(owner, signed_data, verified, NULL, 0U))
		return false;
	for (left = 0U; left < ARRAY_SIZE(descriptors); left++)
		if (ranges_overlap(descriptors[left], descriptor_sizes[left],
			signed_data->data, signed_data->size))
			return false;
	if (!range_valid(signed_data->data, signed_data->size) ||
	    !signed_data->size || signed_data->size > PAYLOAD_MM_MAX_CMS_SIZE ||
	    !payload_mm_hash_digest_size(verified->digest_algorithm) ||
	    !verified->certificate_count ||
	    verified->certificate_count > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES)
		return false;
	for (left = 0U; left < verified->certificate_count; left++) {
		uintptr_t signed_address = (uintptr_t)signed_data->data;
		uintptr_t certificate_address =
			(uintptr_t)verified->certificates[left].data;

		if (!range_valid(verified->certificates[left].data,
			verified->certificates[left].size) ||
		    !verified->certificates[left].size ||
		    certificate_address < signed_address ||
		    certificate_address - signed_address > signed_data->size ||
		    verified->certificates[left].size > signed_data->size -
			(certificate_address - signed_address) ||
		    verified->certificates[left].size >
			PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE)
			return false;
		if (verified->signer_certificate.data ==
			verified->certificates[left].data &&
		    verified->signer_certificate.size ==
			verified->certificates[left].size)
			signer_matches++;
		for (right = left + 1U; right < verified->certificate_count; right++)
			if (ranges_overlap(verified->certificates[left].data,
				verified->certificates[left].size,
				verified->certificates[right].data,
				verified->certificates[right].size))
				return false;
	}
	return signer_matches == 1U;
}

static bool append_utf8(uint8_t output[EDK_PRIVATE_CN_CAPACITY], size_t *size,
	uint32_t code_point)
{
	uint8_t encoded[4];
	size_t encoded_size;

	if (code_point <= 0x7fU) {
		encoded[0] = (uint8_t)code_point;
		encoded_size = 1U;
	} else if (code_point <= 0x7ffU) {
		encoded[0] = 0xc0U | (uint8_t)(code_point >> 6);
		encoded[1] = 0x80U | (uint8_t)(code_point & 0x3fU);
		encoded_size = 2U;
	} else if (code_point >= 0xd800U && code_point <= 0xdfffU) {
		return false;
	} else if (code_point <= 0xffffU) {
		encoded[0] = 0xe0U | (uint8_t)(code_point >> 12);
		encoded[1] = 0x80U | (uint8_t)((code_point >> 6) & 0x3fU);
		encoded[2] = 0x80U | (uint8_t)(code_point & 0x3fU);
		encoded_size = 3U;
	} else if (code_point <= 0x10ffffU) {
		encoded[0] = 0xf0U | (uint8_t)(code_point >> 18);
		encoded[1] = 0x80U | (uint8_t)((code_point >> 12) & 0x3fU);
		encoded[2] = 0x80U | (uint8_t)((code_point >> 6) & 0x3fU);
		encoded[3] = 0x80U | (uint8_t)(code_point & 0x3fU);
		encoded_size = 4U;
	} else {
		return false;
	}
	for (size_t index = 0U; index < encoded_size; index++) {
		if (*size < EDK_PRIVATE_CN_CAPACITY)
			output[*size] = encoded[index];
		(*size)++;
	}
	return true;
}

static bool convert_cn(const mbedtls_x509_buf *value,
	uint8_t output[EDK_PRIVATE_CN_CAPACITY], size_t *output_size)
{
	size_t converted_size = 0U;
	size_t offset = 0U;

	if (value->tag == MBEDTLS_ASN1_UTF8_STRING) {
		while (offset < value->len) {
			uint32_t code_point;
			size_t count;
			uint8_t first = value->p[offset];

			if (first <= 0x7fU) {
				code_point = first;
				count = 1U;
			} else if (first >= 0xc2U && first <= 0xdfU) {
				code_point = first & 0x1fU;
				count = 2U;
			} else if (first >= 0xe0U && first <= 0xefU) {
				code_point = first & 0x0fU;
				count = 3U;
			} else if (first >= 0xf0U && first <= 0xf4U) {
				code_point = first & 0x07U;
				count = 4U;
			} else {
				return false;
			}
			if (count > value->len - offset)
				return false;
			for (size_t index = 1U; index < count; index++) {
				uint8_t next = value->p[offset + index];

				if ((next & 0xc0U) != 0x80U)
					return false;
				code_point = (code_point << 6) | (next & 0x3fU);
			}
			if ((count == 2U && code_point < 0x80U) ||
			    (count == 3U && code_point < 0x800U) ||
			    (count == 4U && code_point < 0x10000U) ||
			    !append_utf8(output, &converted_size, code_point))
				return false;
			offset += count;
		}
	} else if (value->tag == MBEDTLS_ASN1_BMP_STRING) {
		if (value->len % 2U)
			return false;
		while (offset < value->len) {
			uint32_t code_point = ((uint32_t)value->p[offset] << 8) |
				value->p[offset + 1U];

			if (!append_utf8(output, &converted_size, code_point))
				return false;
			offset += 2U;
		}
	} else if (value->tag == MBEDTLS_ASN1_UNIVERSAL_STRING) {
		if (value->len % 4U)
			return false;
		while (offset < value->len) {
			uint32_t code_point = ((uint32_t)value->p[offset] << 24) |
				((uint32_t)value->p[offset + 1U] << 16) |
				((uint32_t)value->p[offset + 2U] << 8) |
				value->p[offset + 3U];

			if (!append_utf8(output, &converted_size, code_point))
				return false;
			offset += 4U;
		}
	} else if (value->tag == MBEDTLS_ASN1_PRINTABLE_STRING ||
		   value->tag == MBEDTLS_ASN1_T61_STRING ||
		   value->tag == MBEDTLS_ASN1_IA5_STRING) {
		while (offset < value->len) {
			if (!append_utf8(output, &converted_size, value->p[offset]))
				return false;
			offset++;
		}
	} else {
		return false;
	}
	*output_size = MIN(converted_size, EDK_PRIVATE_CN_CAPACITY);
	return true;
}

static enum payload_mm_verify_status hash_binding(
	enum payload_mm_hash_algorithm algorithm, const mbedtls_x509_buf *common_name,
	const struct payload_mm_crypto_span *tbs,
	uint8_t digest[PAYLOAD_MM_MAX_DIGEST_SIZE])
{
	struct payload_mm_crypto_span spans[2];
	uint8_t converted[EDK_PRIVATE_CN_CAPACITY] = { 0 };
	size_t converted_size;

	if (!convert_cn(common_name, converted, &converted_size))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	for (spans[0].size = 0U; spans[0].size < converted_size; spans[0].size++)
		if (!converted[spans[0].size])
			break;
	spans[0].data = converted;
	spans[1] = *tbs;
	return payload_mm_hash_spans(algorithm, spans, ARRAY_SIZE(spans), digest);
}

static enum payload_mm_verify_status calculate_binding(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_cms_verified_signer *verified,
	struct payload_mm_authvar_private_binding *binding)
{
	struct payload_mm_cms_verified_signer verified_snapshot;
	struct payload_mm_crypto_span signed_data_snapshot;
	struct payload_mm_crypto_span tbs;
	struct payload_mm_authvar_private_binding draft = { 0 };
	mbedtls_x509_crt signer;
	const mbedtls_x509_name *name;
	uint8_t input_digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t check_digest[PAYLOAD_MM_SHA256_SIZE];
	enum payload_mm_hash_algorithm algorithm;
	enum payload_mm_verify_status status;
	int result;

	if (!inputs_valid(owner, signed_data, verified))
		return PAYLOAD_MM_VERIFY_INVALID;
	signed_data_snapshot = *signed_data;
	verified_snapshot = *verified;
	algorithm = verified_snapshot.digest_algorithm;
	status = payload_mm_sha256(signed_data_snapshot.data,
		signed_data_snapshot.size, input_digest);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	status = payload_mm_crypto_begin(owner);
	if (status != PAYLOAD_MM_VERIFY_OK) {
		if (payload_mm_sha256(signed_data_snapshot.data,
			signed_data_snapshot.size, check_digest) != PAYLOAD_MM_VERIFY_OK ||
		    memcmp(input_digest, check_digest, sizeof(input_digest)) ||
		    memcmp(signed_data, &signed_data_snapshot, sizeof(*signed_data)) ||
		    memcmp(verified, &verified_snapshot, sizeof(*verified)))
			status = PAYLOAD_MM_VERIFY_CHANGED;
		goto sealed_out;
	}
	mbedtls_x509_crt_init(&signer);
	if (payload_mm_sha256(signed_data_snapshot.data,
		signed_data_snapshot.size, check_digest) != PAYLOAD_MM_VERIFY_OK ||
	    memcmp(input_digest, check_digest, sizeof(input_digest)) ||
	    memcmp(signed_data, &signed_data_snapshot, sizeof(*signed_data)) ||
	    memcmp(verified, &verified_snapshot, sizeof(*verified))) {
		status = PAYLOAD_MM_VERIFY_CHANGED;
		goto out;
	}
	result = mbedtls_x509_crt_parse_der_nocopy(&signer,
		verified_snapshot.signer_certificate.data,
		verified_snapshot.signer_certificate.size);
	if (result != 0 ||
	    signer.raw.len != verified_snapshot.signer_certificate.size ||
	    signer.next) {
		status = PAYLOAD_MM_VERIFY_MALFORMED;
		goto out;
	}
	for (name = &signer.subject; name != NULL; name = name->next)
		if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &name->oid) == 0)
			break;
	if (!name) {
		status = PAYLOAD_MM_VERIFY_REJECTED;
		goto out;
	}
	tbs.data = signer.tbs.p;
	tbs.size = signer.tbs.len;
	draft.size = payload_mm_hash_digest_size(algorithm);
	status = hash_binding(algorithm, &name->val, &tbs, draft.digest);
out:
	mbedtls_x509_crt_free(&signer);
	status = payload_mm_crypto_end(owner, status);
	if (payload_mm_sha256(signed_data_snapshot.data,
		signed_data_snapshot.size, check_digest) != PAYLOAD_MM_VERIFY_OK ||
	    memcmp(input_digest, check_digest, sizeof(input_digest)) ||
	    memcmp(signed_data, &signed_data_snapshot, sizeof(*signed_data)) ||
	    memcmp(verified, &verified_snapshot, sizeof(*verified)))
		status = PAYLOAD_MM_VERIFY_CHANGED;
	mbedtls_platform_zeroize(check_digest, sizeof(check_digest));
sealed_out:
	mbedtls_platform_zeroize(input_digest, sizeof(input_digest));
	if (status == PAYLOAD_MM_VERIFY_OK)
		*binding = draft;
	mbedtls_platform_zeroize(&draft, sizeof(draft));
	return status;
}

enum payload_mm_verify_status payload_mm_authvar_private_binding_derive(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_cms_verified_signer *verified,
	struct payload_mm_authvar_private_binding *binding)
{
	struct payload_mm_authvar_private_binding draft;
	struct payload_mm_authvar_private_binding original;
	enum payload_mm_verify_status status;

	if (!binding || (uintptr_t)binding % _Alignof(*binding) ||
	    !range_valid(binding, sizeof(*binding)) ||
	    !descriptors_valid(owner, signed_data, verified, binding,
		sizeof(*binding)))
		return PAYLOAD_MM_VERIFY_INVALID;
	if (ranges_overlap(binding, sizeof(*binding), signed_data->data,
		signed_data->size))
		return PAYLOAD_MM_VERIFY_INVALID;
	original = *binding;
	status = calculate_binding(owner, signed_data, verified, &draft);
	if (status != PAYLOAD_MM_VERIFY_OK) {
		if (memcmp(binding, &original, sizeof(original)))
			status = PAYLOAD_MM_VERIFY_CHANGED;
		*binding = original;
		return status;
	}
	if (memcmp(binding, &original, sizeof(original))) {
		*binding = original;
		return PAYLOAD_MM_VERIFY_CHANGED;
	}
	*binding = draft;
	return PAYLOAD_MM_VERIFY_OK;
}

enum payload_mm_verify_status payload_mm_authvar_private_binding_match(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_cms_verified_signer *verified,
	const struct payload_mm_crypto_span *existing)
{
	struct payload_mm_authvar_private_binding derived;
	struct payload_mm_crypto_span existing_snapshot;
	uint8_t existing_digest[PAYLOAD_MM_MAX_DIGEST_SIZE];
	const void *descriptors[] = { owner, signed_data, verified, existing };
	const size_t descriptor_sizes[] = {
		sizeof(*owner), sizeof(*signed_data), sizeof(*verified), sizeof(*existing),
	};
	size_t digest_size;
	uint8_t difference = 0U;
	enum payload_mm_verify_status status;
	size_t index;

	if (!existing || (uintptr_t)existing % _Alignof(*existing) ||
	    !range_valid(existing, sizeof(*existing)) ||
	    !descriptors_valid(owner, signed_data, verified, existing,
		sizeof(*existing)) ||
	    !range_valid(existing->data, existing->size) || !existing->size)
		return PAYLOAD_MM_VERIFY_INVALID;
	for (index = 0U; index < ARRAY_SIZE(descriptors); index++) {
		if (ranges_overlap(existing->data, existing->size,
			descriptors[index], descriptor_sizes[index]))
			return PAYLOAD_MM_VERIFY_INVALID;
	}
	if (ranges_overlap(existing->data, existing->size,
		signed_data->data, signed_data->size))
		return PAYLOAD_MM_VERIFY_INVALID;
	if (ranges_overlap(existing, sizeof(*existing),
		signed_data->data, signed_data->size))
		return PAYLOAD_MM_VERIFY_INVALID;
	if (!inputs_valid(owner, signed_data, verified))
		return PAYLOAD_MM_VERIFY_INVALID;
	digest_size = payload_mm_hash_digest_size(verified->digest_algorithm);
	if (existing->size == digest_size) {
		existing_snapshot = *existing;
		memcpy(existing_digest, existing->data, digest_size);
		status = calculate_binding(owner, signed_data, verified, &derived);
		if (memcmp(existing, &existing_snapshot, sizeof(*existing)) ||
		    memcmp(existing_snapshot.data, existing_digest, digest_size)) {
			mbedtls_platform_zeroize(&derived, sizeof(derived));
			mbedtls_platform_zeroize(existing_digest,
				sizeof(existing_digest));
			return PAYLOAD_MM_VERIFY_CHANGED;
		}
		if (status != PAYLOAD_MM_VERIFY_OK) {
			mbedtls_platform_zeroize(existing_digest,
				sizeof(existing_digest));
			return status;
		}
		for (index = 0U; index < digest_size; index++)
			difference |= existing_digest[index] ^ derived.digest[index];
	} else if (existing->size == LEGACY_CERT_STACK_HEADER_SIZE +
		verified->signer_certificate.size && existing->data[0] == 1U &&
		verified->signer_certificate.size <= UINT32_MAX) {
		uint32_t stored_size = (uint32_t)existing->data[1] |
			((uint32_t)existing->data[2] << 8) |
			((uint32_t)existing->data[3] << 16) |
			((uint32_t)existing->data[4] << 24);

		difference = stored_size != verified->signer_certificate.size;
		for (index = 0U; index < verified->signer_certificate.size; index++)
			difference |= existing->data[LEGACY_CERT_STACK_HEADER_SIZE + index] ^
				verified->signer_certificate.data[index];
	} else {
		difference = 1U;
	}
	mbedtls_platform_zeroize(&derived, sizeof(derived));
	mbedtls_platform_zeroize(existing_digest, sizeof(existing_digest));
	return difference ? PAYLOAD_MM_VERIFY_REJECTED : PAYLOAD_MM_VERIFY_OK;
}

#ifdef PAYLOAD_MM_AUTH_TEST
enum payload_mm_verify_status payload_mm_auth_test_private_binding_convert_cn(
	uint8_t tag, const uint8_t *input, size_t input_size,
	uint8_t output[EDK_PRIVATE_CN_CAPACITY], size_t *output_size)
{
	mbedtls_x509_buf value = {
		.tag = tag,
		.len = input_size,
		.p = (uint8_t *)input,
	};
	uint8_t converted[EDK_PRIVATE_CN_CAPACITY] = { 0 };
	size_t converted_size;

	if (!output || !output_size ||
	    (uintptr_t)output_size % _Alignof(*output_size) ||
	    !range_valid(input, input_size) ||
	    !range_valid(output, EDK_PRIVATE_CN_CAPACITY) ||
	    !range_valid(output_size, sizeof(*output_size)) ||
	    ranges_overlap(output, EDK_PRIVATE_CN_CAPACITY, output_size,
		sizeof(*output_size)) ||
	    ranges_overlap(output, EDK_PRIVATE_CN_CAPACITY, input, input_size) ||
	    ranges_overlap(output_size, sizeof(*output_size), input, input_size) ||
	    !convert_cn(&value, converted, &converted_size))
		return PAYLOAD_MM_VERIFY_INVALID;
	memcpy(output, converted, EDK_PRIVATE_CN_CAPACITY);
	*output_size = converted_size;
	mbedtls_platform_zeroize(converted, sizeof(converted));
	return PAYLOAD_MM_VERIFY_OK;
}

enum payload_mm_verify_status payload_mm_auth_test_private_binding_hash(
	enum payload_mm_hash_algorithm algorithm, uint8_t tag,
	const uint8_t *input, size_t input_size,
	const struct payload_mm_crypto_span *tbs,
	uint8_t digest[PAYLOAD_MM_MAX_DIGEST_SIZE])
{
	mbedtls_x509_buf value = {
		.tag = tag,
		.len = input_size,
		.p = (uint8_t *)input,
	};

	if (!tbs || !digest || !range_valid(input, input_size) ||
	    !range_valid(tbs, sizeof(*tbs)) || !range_valid(tbs->data, tbs->size) ||
	    !tbs->size || !payload_mm_hash_digest_size(algorithm))
		return PAYLOAD_MM_VERIFY_INVALID;
	return hash_binding(algorithm, &value, tbs, digest);
}
#endif
