/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_authvar_private_binding.h"
#include "payload_mm_crypto/crypto.h"

#include <commonlib/helpers.h>
#include <mbedtls/oid.h>
#include <mbedtls/x509_crt.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct buffer {
	uint8_t *data;
	size_t size;
};

enum attack_kind {
	ATTACK_NONE,
	ATTACK_ALGORITHM,
	ATTACK_SIGNER_BYTE,
	ATTACK_NONSIGNER_BYTE,
	ATTACK_SIGNER_SPAN,
	ATTACK_CERTIFICATE_SPAN,
	ATTACK_CERTIFICATE_COUNT,
	ATTACK_SIGNED_DATA,
	ATTACK_OUTPUT,
	ATTACK_EXISTING,
	ATTACK_EXISTING_DESCRIPTOR,
};

static enum attack_kind attack;
static struct payload_mm_crypto_span *attack_signed_data;
static struct payload_mm_cms_verified_signer *attack_verified;
static struct payload_mm_authvar_private_binding *attack_output;
static struct payload_mm_crypto_span *attack_existing;

void payload_mm_crypto_test_begin_claimed(struct payload_mm_crypto_owner *owner)
{
	(void)owner;
	switch (attack) {
	case ATTACK_ALGORITHM:
		attack_verified->digest_algorithm = PAYLOAD_MM_HASH_SHA512;
		break;
	case ATTACK_SIGNER_BYTE:
		((uint8_t *)attack_verified->signer_certificate.data)[0] ^= 1U;
		break;
	case ATTACK_NONSIGNER_BYTE:
		((uint8_t *)attack_verified->certificates[0].data)[0] ^= 1U;
		break;
	case ATTACK_SIGNER_SPAN:
		attack_verified->signer_certificate.size--;
		break;
	case ATTACK_CERTIFICATE_SPAN:
		attack_verified->certificates[0].size--;
		break;
	case ATTACK_CERTIFICATE_COUNT:
		attack_verified->certificate_count--;
		break;
	case ATTACK_SIGNED_DATA:
		attack_signed_data->size--;
		break;
	case ATTACK_OUTPUT:
		memset(attack_output, 0x3c, sizeof(*attack_output));
		break;
	case ATTACK_EXISTING:
		((uint8_t *)attack_existing->data)[0] ^= 1U;
		break;
	case ATTACK_EXISTING_DESCRIPTOR:
		attack_existing->size--;
		break;
	case ATTACK_NONE:
		break;
	}
	attack = ATTACK_NONE;
}

static struct buffer read_file(const char *path)
{
	struct buffer buffer = { 0 };
	FILE *file = fopen(path, "rb");
	long length;

	assert(file != NULL);
	assert(fseek(file, 0L, SEEK_END) == 0);
	length = ftell(file);
	assert(length > 0);
	assert(fseek(file, 0L, SEEK_SET) == 0);
	buffer.size = (size_t)length;
	buffer.data = malloc(buffer.size);
	assert(buffer.data != NULL);
	assert(fread(buffer.data, 1U, buffer.size, file) == buffer.size);
	assert(fclose(file) == 0);
	return buffer;
}

static void decode_hex(const char *hex, uint8_t *bytes, size_t size)
{
	for (size_t index = 0U; index < size; index++) {
		unsigned int value;

		assert(sscanf(hex + 2U * index, "%2x", &value) == 1);
		bytes[index] = (uint8_t)value;
	}
	assert(hex[2U * size] == '\0');
}

static void expect_owner_clean(const struct payload_mm_crypto_owner *owner)
{
	assert(payload_mm_crypto_idle());
	assert(payload_mm_crypto_owner_is_clean(owner));
}

static void setup_verified(struct payload_mm_cms_verified_signer *verified,
	struct payload_mm_crypto_span *signed_data, uint8_t *storage,
	const struct buffer *intermediate, const struct buffer *signer)
{
	memcpy(storage, intermediate->data, intermediate->size);
	memcpy(storage + intermediate->size, signer->data, signer->size);
	*signed_data = (struct payload_mm_crypto_span) {
		.data = storage,
		.size = intermediate->size + signer->size,
	};
	*verified = (struct payload_mm_cms_verified_signer) {
		.digest_algorithm = PAYLOAD_MM_HASH_SHA256,
		.signer_certificate = {
			.data = storage + intermediate->size,
			.size = signer->size,
		},
		.certificates = {
			{ .data = storage, .size = intermediate->size },
			{ .data = storage + intermediate->size, .size = signer->size },
		},
		.certificate_count = 2U,
	};
}

static void check_goldens(struct payload_mm_crypto_owner *owner,
	struct payload_mm_crypto_span *signed_data,
	struct payload_mm_cms_verified_signer *verified)
{
	static const struct {
		enum payload_mm_hash_algorithm algorithm;
		size_t size;
		const char *hex;
	} vectors[] = {
		{ PAYLOAD_MM_HASH_SHA256, 32U,
		  "8ac5cd79167342d2c96e1c45fc47083ca61325eeacf219e37eec5bd66d2b4901" },
		{ PAYLOAD_MM_HASH_SHA384, 48U,
		  "796d471d06f56988aeff114e3bf226bd41205f7fb5a37d4981c766e19c101399"
		  "6574f4bbeb3e881395e0b9d6432d2245" },
		{ PAYLOAD_MM_HASH_SHA512, 64U,
		  "04e0b9e64b39493a0cda5e3b6d78b3811e5665fb05df3582eb164aaec29f90f1"
		  "19bbbf773a4e49fdb877dc807c662a3cd7ad77cb123c325774f1caf0b8d36780" },
	};

	for (size_t index = 0U; index < ARRAY_SIZE(vectors); index++) {
		struct payload_mm_authvar_private_binding binding;
		uint8_t expected[PAYLOAD_MM_MAX_DIGEST_SIZE] = { 0 };

		memset(&binding, 0xa5, sizeof(binding));
		verified->digest_algorithm = vectors[index].algorithm;
		decode_hex(vectors[index].hex, expected, vectors[index].size);
		assert(payload_mm_authvar_private_binding_derive(owner, signed_data,
			verified, &binding) == PAYLOAD_MM_VERIFY_OK);
		assert(binding.size == vectors[index].size);
		assert(!memcmp(binding.digest, expected, vectors[index].size));
		for (size_t tail = vectors[index].size; tail < sizeof(binding.digest);
		     tail++)
			assert(binding.digest[tail] == 0U);
		expect_owner_clean(owner);
	}
	verified->digest_algorithm = PAYLOAD_MM_HASH_SHA256;
	{
		struct payload_mm_crypto_span swap = verified->certificates[0];
		struct payload_mm_authvar_private_binding binding;
		uint8_t expected[32];

		verified->certificates[0] = verified->certificates[1];
		verified->certificates[1] = swap;
		decode_hex(vectors[0].hex, expected, sizeof(expected));
		assert(payload_mm_authvar_private_binding_derive(owner, signed_data,
			verified, &binding) == PAYLOAD_MM_VERIFY_OK);
		assert(!memcmp(binding.digest, expected, sizeof(expected)));
		decode_hex("06d87f5b07c6edcff563f4556ef049be4376522f25c0cc5a44c984016d49433c",
			expected, sizeof(expected));
		assert(memcmp(binding.digest, expected, sizeof(expected)));
		swap = verified->certificates[0];
		verified->certificates[0] = verified->certificates[1];
		verified->certificates[1] = swap;
	}
}

static void check_duplicate_cn(struct payload_mm_crypto_owner *owner,
	const struct buffer *certificate)
{
	struct payload_mm_crypto_span signed_data = {
		.data = certificate->data,
		.size = certificate->size,
	};
	struct payload_mm_cms_verified_signer verified = {
		.digest_algorithm = PAYLOAD_MM_HASH_SHA256,
		.signer_certificate = signed_data,
		.certificates = { signed_data },
		.certificate_count = 1U,
	};
	struct payload_mm_authvar_private_binding first;
	mbedtls_x509_crt parsed;
	const mbedtls_x509_name *name;
	const mbedtls_x509_buf *names[2];
	struct payload_mm_crypto_span tbs;
	uint8_t expected[PAYLOAD_MM_MAX_DIGEST_SIZE];
	uint8_t rejected[PAYLOAD_MM_MAX_DIGEST_SIZE];
	size_t count = 0U;

	assert(payload_mm_crypto_begin(owner) == PAYLOAD_MM_VERIFY_OK);
	mbedtls_x509_crt_init(&parsed);
	assert(mbedtls_x509_crt_parse_der_nocopy(&parsed, certificate->data,
		certificate->size) == 0);
	for (name = &parsed.subject; name != NULL; name = name->next)
		if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &name->oid) == 0) {
			assert(count < ARRAY_SIZE(names));
			names[count++] = &name->val;
		}
	assert(count == ARRAY_SIZE(names));
	tbs = (struct payload_mm_crypto_span) { parsed.tbs.p, parsed.tbs.len };
	assert(payload_mm_auth_test_private_binding_hash(PAYLOAD_MM_HASH_SHA256,
		names[0]->tag, names[0]->p, names[0]->len, &tbs, expected) ==
		PAYLOAD_MM_VERIFY_OK);
	assert(payload_mm_auth_test_private_binding_hash(PAYLOAD_MM_HASH_SHA256,
		names[1]->tag, names[1]->p, names[1]->len, &tbs, rejected) ==
		PAYLOAD_MM_VERIFY_OK);
	mbedtls_x509_crt_free(&parsed);
	assert(payload_mm_crypto_end(owner, PAYLOAD_MM_VERIFY_OK) ==
		PAYLOAD_MM_VERIFY_OK);
	assert(payload_mm_authvar_private_binding_derive(owner, &signed_data,
		&verified, &first) == PAYLOAD_MM_VERIFY_OK);
	assert(!memcmp(first.digest, expected, PAYLOAD_MM_SHA256_SIZE));
	assert(memcmp(first.digest, rejected, PAYLOAD_MM_SHA256_SIZE));
	expect_owner_clean(owner);
}

static void check_conversion(void)
{
	static const uint8_t pound_utf8[] = { 0xc2, 0xa3 };
	static const uint8_t pound_t61[] = { 0xa3 };
	static const uint8_t pound_bmp[] = { 0x00, 0xa3 };
	static const uint8_t pound_universal[] = { 0x00, 0x00, 0x00, 0xa3 };
	static const uint8_t printable[] = { 'A', 'B', 'C' };
	static const uint8_t ia5_ff[] = { 0xff };
	static const uint8_t ff_utf8[] = { 0xc3, 0xbf };
	static const uint8_t embedded_nul[] = { 'A', 0x00, 'B' };
	static const uint8_t invalid_utf8[] = { 0xc0, 0x80 };
	static const uint8_t odd_bmp[] = { 0x00 };
	static const uint8_t surrogate_bmp[] = { 0xd8, 0x00 };
	static const uint8_t invalid_universal[] = { 0x00, 0x11, 0x00, 0x00 };
	static const uint8_t bit_string[] = { 0x00, 0x41 };
	const struct {
		uint8_t tag;
		const uint8_t *input;
		size_t input_size;
		const uint8_t *expected;
		size_t expected_size;
	} vectors[] = {
		{ 0x0c, pound_utf8, sizeof(pound_utf8), pound_utf8, sizeof(pound_utf8) },
		{ 0x14, pound_t61, sizeof(pound_t61), pound_utf8, sizeof(pound_utf8) },
		{ 0x1e, pound_bmp, sizeof(pound_bmp), pound_utf8, sizeof(pound_utf8) },
		{ 0x1c, pound_universal, sizeof(pound_universal), pound_utf8,
		  sizeof(pound_utf8) },
		{ 0x13, printable, sizeof(printable), printable, sizeof(printable) },
		{ 0x16, ia5_ff, sizeof(ia5_ff), ff_utf8, sizeof(ff_utf8) },
		{ 0x0c, embedded_nul, sizeof(embedded_nul), embedded_nul,
		  sizeof(embedded_nul) },
	};
	const struct {
		uint8_t tag;
		const uint8_t *input;
		size_t input_size;
	} invalid[] = {
		{ 0x0c, invalid_utf8, sizeof(invalid_utf8) },
		{ 0x1e, odd_bmp, sizeof(odd_bmp) },
		{ 0x1e, surrogate_bmp, sizeof(surrogate_bmp) },
		{ 0x1c, invalid_universal, sizeof(invalid_universal) },
		{ 0x15, pound_t61, sizeof(pound_t61) },
		{ 0x03, bit_string, sizeof(bit_string) },
	};
	uint8_t output[127];
	size_t output_size;

	for (size_t index = 0U; index < ARRAY_SIZE(vectors); index++) {
		memset(output, 0xa5, sizeof(output));
		output_size = SIZE_MAX;
		assert(payload_mm_auth_test_private_binding_convert_cn(vectors[index].tag,
			vectors[index].input, vectors[index].input_size, output,
			&output_size) == PAYLOAD_MM_VERIFY_OK);
		assert(output_size == vectors[index].expected_size);
		assert(!memcmp(output, vectors[index].expected, output_size));
	}
	for (size_t index = 0U; index < ARRAY_SIZE(invalid); index++) {
		uint8_t sentinel[sizeof(output)];
		size_t sentinel_size;

		memset(output, 0xa5, sizeof(output));
		memcpy(sentinel, output, sizeof(output));
		output_size = SIZE_MAX;
		sentinel_size = output_size;
		assert(payload_mm_auth_test_private_binding_convert_cn(invalid[index].tag,
			invalid[index].input, invalid[index].input_size, output,
			&output_size) == PAYLOAD_MM_VERIFY_INVALID);
		assert(!memcmp(output, sentinel, sizeof(output)));
		assert(output_size == sentinel_size);
	}
	{
		uint8_t long_cn[200];
		uint8_t split_cn[128];

		memset(long_cn, 'L', sizeof(long_cn));
		assert(payload_mm_auth_test_private_binding_convert_cn(0x0c, long_cn,
			sizeof(long_cn), output, &output_size) == PAYLOAD_MM_VERIFY_OK);
		assert(output_size == sizeof(output));
		for (size_t index = 0U; index < sizeof(output); index++)
			assert(output[index] == 'L');
		memset(split_cn, 'A', 126U);
		split_cn[126] = 0xc2;
		split_cn[127] = 0xa3;
		assert(payload_mm_auth_test_private_binding_convert_cn(0x0c, split_cn,
			sizeof(split_cn), output, &output_size) == PAYLOAD_MM_VERIFY_OK);
		assert(output_size == sizeof(output));
		assert(output[126] == 0xc2);
	}
}

static void check_hash_semantics(void)
{
	static const uint8_t tbs_bytes[] = { 'T', 'B', 'S' };
	static const struct payload_mm_crypto_span tbs = {
		.data = tbs_bytes,
		.size = sizeof(tbs_bytes),
	};
	static const uint8_t pound_utf8[] = { 0xc2, 0xa3 };
	static const uint8_t pound_t61[] = { 0xa3 };
	static const uint8_t pound_bmp[] = { 0x00, 0xa3 };
	static const uint8_t pound_universal[] = { 0x00, 0x00, 0x00, 0xa3 };
	static const uint8_t printable[] = { 'A', 'B', 'C' };
	static const uint8_t ia5_ff[] = { 0xff };
	static const uint8_t embedded_nul[] = { 'A', 0x00, 'B' };
	static const uint8_t leading_nul[] = { 0x00, 'B' };
	static const struct {
		uint8_t tag;
		const uint8_t *input;
		size_t input_size;
		const char *hex;
	} vectors[] = {
		{ 0x0c, pound_utf8, sizeof(pound_utf8),
		  "dc10d9878fa17798f2342f31f1e3d7df344ea770438a69a51030f7e105b04b2c" },
		{ 0x14, pound_t61, sizeof(pound_t61),
		  "dc10d9878fa17798f2342f31f1e3d7df344ea770438a69a51030f7e105b04b2c" },
		{ 0x1e, pound_bmp, sizeof(pound_bmp),
		  "dc10d9878fa17798f2342f31f1e3d7df344ea770438a69a51030f7e105b04b2c" },
		{ 0x1c, pound_universal, sizeof(pound_universal),
		  "dc10d9878fa17798f2342f31f1e3d7df344ea770438a69a51030f7e105b04b2c" },
		{ 0x13, printable, sizeof(printable),
		  "e87df4d168d7d920bcee9b8dc0abfb5b69f0ddde596c8abfa7507c7c756f31c3" },
		{ 0x16, ia5_ff, sizeof(ia5_ff),
		  "b8b04eb8da8d94799055641158480b034a53cd8f7287de15c21117caa66a601b" },
		{ 0x0c, embedded_nul, sizeof(embedded_nul),
		  "4b00ee521d3eb8a960410c18e92b3a2251a759b61eca7314066d5350afa4e728" },
		{ 0x0c, leading_nul, sizeof(leading_nul),
		  "c9d7b0fdc67ffe18e257b15a52259d0f02bc25b384fd93adaf610553a8d482fc" },
	};
	uint8_t digest[PAYLOAD_MM_MAX_DIGEST_SIZE];
	uint8_t expected[PAYLOAD_MM_SHA256_SIZE];
	uint8_t long_cn[200];
	uint8_t split_cn[128];

	for (size_t index = 0U; index < ARRAY_SIZE(vectors); index++) {
		decode_hex(vectors[index].hex, expected, sizeof(expected));
		assert(payload_mm_auth_test_private_binding_hash(PAYLOAD_MM_HASH_SHA256,
			vectors[index].tag, vectors[index].input,
			vectors[index].input_size, &tbs, digest) == PAYLOAD_MM_VERIFY_OK);
		assert(!memcmp(digest, expected, sizeof(expected)));
	}
	memset(long_cn, 'L', sizeof(long_cn));
	decode_hex("35a43b07e3ce07463ef86e0238b54682773dfaf820dcbe31fd937be5e7ea267d",
		expected, sizeof(expected));
	assert(payload_mm_auth_test_private_binding_hash(PAYLOAD_MM_HASH_SHA256,
		0x0c, long_cn, sizeof(long_cn), &tbs, digest) == PAYLOAD_MM_VERIFY_OK);
	assert(!memcmp(digest, expected, sizeof(expected)));
	memset(split_cn, 'A', 126U);
	split_cn[126] = 0xc2;
	split_cn[127] = 0xa3;
	decode_hex("fb52aefa6d1a79dbb5b7a1e3a29fa49da1c41ad787485db758dea0f57c07f487",
		expected, sizeof(expected));
	assert(payload_mm_auth_test_private_binding_hash(PAYLOAD_MM_HASH_SHA256,
		0x0c, split_cn, sizeof(split_cn), &tbs, digest) == PAYLOAD_MM_VERIFY_OK);
	assert(!memcmp(digest, expected, sizeof(expected)));
}

static void check_legacy(struct payload_mm_crypto_owner *owner,
	struct payload_mm_crypto_span *signed_data,
	struct payload_mm_cms_verified_signer *verified,
	const struct buffer *intermediate, const struct buffer *signer,
	uint8_t *storage)
{
	struct payload_mm_crypto_span existing;
	struct payload_mm_crypto_owner blocker = { 0 };
	uint8_t *legacy = malloc(signer->size + 5U);
	uint8_t *saved = malloc(signer->size);
	size_t oid_matches = 0U;

	assert(legacy != NULL && saved != NULL);
	legacy[0] = 1U;
	legacy[1] = (uint8_t)signer->size;
	legacy[2] = (uint8_t)(signer->size >> 8);
	legacy[3] = (uint8_t)(signer->size >> 16);
	legacy[4] = (uint8_t)(signer->size >> 24);
	memcpy(legacy + 5U, signer->data, signer->size);
	existing = (struct payload_mm_crypto_span) { legacy, signer->size + 5U };
	for (enum payload_mm_hash_algorithm algorithm = PAYLOAD_MM_HASH_SHA256;
	     algorithm <= PAYLOAD_MM_HASH_SHA512; algorithm++) {
		verified->digest_algorithm = algorithm;
		assert(payload_mm_authvar_private_binding_match(owner, signed_data,
			verified, &existing) == PAYLOAD_MM_VERIFY_OK);
		expect_owner_clean(owner);
	}
	verified->digest_algorithm = PAYLOAD_MM_HASH_SHA256;
	{
		size_t owner_allocations = owner->allocation_count;
		size_t blocker_allocations = blocker.allocation_count;

		assert(payload_mm_crypto_begin(&blocker) == PAYLOAD_MM_VERIFY_OK);
		assert(payload_mm_authvar_private_binding_match(owner, signed_data,
			verified, &existing) == PAYLOAD_MM_VERIFY_OK);
		assert(owner->allocation_count == owner_allocations);
		assert(blocker.allocation_count == blocker_allocations);
		assert(blocker.busy);
		assert(payload_mm_crypto_end(&blocker, PAYLOAD_MM_VERIFY_OK) ==
			PAYLOAD_MM_VERIFY_OK);
	}
	for (size_t index = 0U; index < existing.size; index++) {
		legacy[index] ^= 1U;
		assert(payload_mm_authvar_private_binding_match(owner, signed_data,
			verified, &existing) == PAYLOAD_MM_VERIFY_REJECTED);
		legacy[index] ^= 1U;
	}
	memcpy(saved, verified->signer_certificate.data, signer->size);
	for (size_t offset = 0U; offset + 5U <= signer->size; offset++) {
		uint8_t *byte = (uint8_t *)verified->signer_certificate.data + offset;

		if (byte[0] == 0x06U && byte[1] == 0x03U && byte[2] == 0x55U &&
		    byte[3] == 0x04U && byte[4] == 0x03U && ++oid_matches == 2U) {
			byte[4] = 0x0bU;
			break;
		}
	}
	assert(oid_matches == 2U);
	memcpy(legacy + 5U, verified->signer_certificate.data, signer->size);
	assert(payload_mm_authvar_private_binding_derive(owner, signed_data, verified,
		&(struct payload_mm_authvar_private_binding){ 0 }) ==
		PAYLOAD_MM_VERIFY_REJECTED);
	assert(payload_mm_authvar_private_binding_match(owner, signed_data, verified,
		&existing) == PAYLOAD_MM_VERIFY_OK);
	memcpy(storage + intermediate->size, saved, signer->size);
	free(saved);
	free(legacy);
}

static void check_failures(struct payload_mm_crypto_owner *owner,
	struct payload_mm_crypto_span *signed_data,
	struct payload_mm_cms_verified_signer *verified)
{
	struct payload_mm_authvar_private_binding output;
	struct payload_mm_authvar_private_binding sentinel;
	struct payload_mm_cms_verified_signer verified_saved;
	struct payload_mm_crypto_span signed_saved;
	uint8_t signer_first;
	uint8_t digest[32];
	struct payload_mm_crypto_span existing = { digest, sizeof(digest) };
	uint8_t nonsigner_first;
	uint8_t *descriptor_backup;
	uint8_t misaligned_storage[sizeof(struct payload_mm_crypto_span) + 1U];
	const struct payload_mm_crypto_span *misaligned =
		(const void *)(misaligned_storage + 1U);
	struct payload_mm_crypto_span wrapped_signed = *signed_data;
	struct payload_mm_crypto_span wrapped_existing = existing;
	struct payload_mm_crypto_span forged = {
		.data = (const void *)(uintptr_t)PAYLOAD_MM_HASH_SHA256,
		.size = PAYLOAD_MM_SHA256_SIZE,
	};
	size_t allocation_count;

	attack_signed_data = signed_data;
	attack_verified = verified;
	attack_output = &output;
	attack_existing = &existing;
	allocation_count = owner->allocation_count;
	memset(&output, 0xa5, sizeof(output));
	sentinel = output;
	assert(payload_mm_authvar_private_binding_derive(owner, misaligned, verified,
		&output) == PAYLOAD_MM_VERIFY_INVALID);
	assert(!memcmp(&output, &sentinel, sizeof(output)));
	assert(payload_mm_authvar_private_binding_match(owner, misaligned, verified,
		&existing) == PAYLOAD_MM_VERIFY_INVALID);
	assert(payload_mm_authvar_private_binding_derive(
		(struct payload_mm_crypto_owner *)(void *)signed_data, signed_data,
		verified, &output) == PAYLOAD_MM_VERIFY_INVALID);
	assert(payload_mm_authvar_private_binding_derive(
		(struct payload_mm_crypto_owner *)(void *)verified, signed_data,
		verified, &output) == PAYLOAD_MM_VERIFY_INVALID);
	assert(payload_mm_authvar_private_binding_derive(owner,
		(const struct payload_mm_crypto_span *)(const void *)verified,
		verified, &output) == PAYLOAD_MM_VERIFY_INVALID);
	assert(payload_mm_authvar_private_binding_derive(owner, &forged,
		(const struct payload_mm_cms_verified_signer *)(const void *)&forged,
		&output) == PAYLOAD_MM_VERIFY_INVALID);
	assert(payload_mm_authvar_private_binding_derive(owner, signed_data, verified,
		(struct payload_mm_authvar_private_binding *)(void *)signed_data) ==
		PAYLOAD_MM_VERIFY_INVALID);
	assert(payload_mm_authvar_private_binding_derive(owner, signed_data, verified,
		(struct payload_mm_authvar_private_binding *)(void *)verified) ==
		PAYLOAD_MM_VERIFY_INVALID);
	assert(payload_mm_authvar_private_binding_match(owner, signed_data, verified,
		(const struct payload_mm_crypto_span *)(const void *)owner) ==
		PAYLOAD_MM_VERIFY_INVALID);
	assert(payload_mm_authvar_private_binding_match(owner, signed_data, verified,
		(const struct payload_mm_crypto_span *)(const void *)verified) ==
		PAYLOAD_MM_VERIFY_INVALID);
	wrapped_signed.data = (const void *)(UINTPTR_MAX - 15U);
	wrapped_signed.size = 32U;
	assert(payload_mm_authvar_private_binding_derive(owner, &wrapped_signed,
		verified, &output) == PAYLOAD_MM_VERIFY_INVALID);
	wrapped_existing.data = (const void *)(UINTPTR_MAX - 15U);
	wrapped_existing.size = 32U;
	assert(payload_mm_authvar_private_binding_match(owner, signed_data, verified,
		&wrapped_existing) == PAYLOAD_MM_VERIFY_INVALID);
	assert(!memcmp(&output, &sentinel, sizeof(output)));
	assert(owner->allocation_count == allocation_count);
	expect_owner_clean(owner);
	for (enum attack_kind current = ATTACK_ALGORITHM; current <= ATTACK_OUTPUT;
	     current++) {
		verified_saved = *verified;
		signed_saved = *signed_data;
		signer_first = *(uint8_t *)verified->signer_certificate.data;
		nonsigner_first = *(uint8_t *)verified->certificates[0].data;
		memset(&output, 0xa5, sizeof(output));
		sentinel = output;
		attack = current;
		assert(payload_mm_authvar_private_binding_derive(owner, signed_data,
			verified, &output) == PAYLOAD_MM_VERIFY_CHANGED);
		assert(!memcmp(&output, &sentinel, sizeof(output)));
		*verified = verified_saved;
		*signed_data = signed_saved;
		*(uint8_t *)verified->signer_certificate.data = signer_first;
		*(uint8_t *)verified->certificates[0].data = nonsigner_first;
		expect_owner_clean(owner);
	}
	verified_saved = *verified;
	memset(&output, 0xa5, sizeof(output));
	sentinel = output;
	owner->busy = true;
	attack = ATTACK_ALGORITHM;
	assert(payload_mm_authvar_private_binding_derive(owner, signed_data, verified,
		&output) == PAYLOAD_MM_VERIFY_CHANGED);
	assert(owner->busy);
	assert(!memcmp(&output, &sentinel, sizeof(output)));
	owner->busy = false;
	*verified = verified_saved;
	owner->busy = true;
	attack = ATTACK_OUTPUT;
	assert(payload_mm_authvar_private_binding_derive(owner, signed_data, verified,
		&output) == PAYLOAD_MM_VERIFY_CHANGED);
	assert(owner->busy);
	assert(!memcmp(&output, &sentinel, sizeof(output)));
	owner->busy = false;
	verified->digest_algorithm = PAYLOAD_MM_HASH_SHA256;
	assert(payload_mm_authvar_private_binding_derive(owner, signed_data, verified,
		&output) == PAYLOAD_MM_VERIFY_OK);
	memcpy(digest, output.digest, sizeof(digest));
	assert(payload_mm_authvar_private_binding_match(owner, signed_data, verified,
		&existing) == PAYLOAD_MM_VERIFY_OK);
	digest[0] ^= 1U;
	assert(payload_mm_authvar_private_binding_match(owner, signed_data, verified,
		&existing) == PAYLOAD_MM_VERIFY_REJECTED);
	digest[0] ^= 1U;
	attack = ATTACK_EXISTING;
	assert(payload_mm_authvar_private_binding_match(owner, signed_data, verified,
		&existing) == PAYLOAD_MM_VERIFY_CHANGED);
	digest[0] ^= 1U;
	attack = ATTACK_EXISTING_DESCRIPTOR;
	assert(payload_mm_authvar_private_binding_match(owner, signed_data, verified,
		&existing) == PAYLOAD_MM_VERIFY_CHANGED);
	existing.size++;
	owner->busy = true;
	attack = ATTACK_EXISTING;
	assert(payload_mm_authvar_private_binding_match(owner, signed_data, verified,
		&existing) == PAYLOAD_MM_VERIFY_CHANGED);
	assert(owner->busy);
	owner->busy = false;
	digest[0] ^= 1U;
	owner->fail_allocation = 1U;
	attack = ATTACK_EXISTING_DESCRIPTOR;
	assert(payload_mm_authvar_private_binding_match(owner, signed_data, verified,
		&existing) == PAYLOAD_MM_VERIFY_CHANGED);
	existing.size++;
	owner->fail_allocation = 0U;
	expect_owner_clean(owner);

	descriptor_backup = malloc(sizeof(existing));
	assert(descriptor_backup != NULL);
	memcpy(descriptor_backup, signed_data->data, sizeof(existing));
	*(struct payload_mm_crypto_span *)(void *)signed_data->data = existing;
	allocation_count = owner->allocation_count;
	assert(payload_mm_authvar_private_binding_match(owner, signed_data, verified,
		(const struct payload_mm_crypto_span *)(const void *)signed_data->data) ==
		PAYLOAD_MM_VERIFY_INVALID);
	assert(owner->allocation_count == allocation_count);
	memcpy((uint8_t *)signed_data->data, descriptor_backup, sizeof(existing));
	free(descriptor_backup);

	memset(&output, 0xa5, sizeof(output));
	sentinel = output;
	owner->fail_allocation = 1U;
	assert(payload_mm_authvar_private_binding_derive(owner, signed_data, verified,
		&output) == PAYLOAD_MM_VERIFY_NO_MEMORY);
	owner->fail_allocation = 0U;
	assert(!memcmp(&output, &sentinel, sizeof(output)));
	expect_owner_clean(owner);
	assert(payload_mm_crypto_begin(owner) == PAYLOAD_MM_VERIFY_OK);
	assert(payload_mm_authvar_private_binding_derive(owner, signed_data, verified,
		&output) == PAYLOAD_MM_VERIFY_BUSY);
	assert(payload_mm_crypto_end(owner, PAYLOAD_MM_VERIFY_OK) ==
		PAYLOAD_MM_VERIFY_OK);
	expect_owner_clean(owner);

	verified_saved = *verified;
	verified->certificate_count = 0U;
	assert(payload_mm_authvar_private_binding_derive(owner, signed_data, verified,
		&output) == PAYLOAD_MM_VERIFY_INVALID);
	*verified = verified_saved;
	verified->signer_certificate.data++;
	assert(payload_mm_authvar_private_binding_derive(owner, signed_data, verified,
		&output) == PAYLOAD_MM_VERIFY_INVALID);
	*verified = verified_saved;
	verified->certificates[1] = verified->certificates[0];
	assert(payload_mm_authvar_private_binding_derive(owner, signed_data, verified,
		&output) == PAYLOAD_MM_VERIFY_INVALID);
	*verified = verified_saved;
	assert(payload_mm_authvar_private_binding_derive(owner, signed_data, verified,
		(struct payload_mm_authvar_private_binding *)(void *)owner) ==
		PAYLOAD_MM_VERIFY_INVALID);
}

int main(int argc, char **argv)
{
	struct buffer intermediate;
	struct buffer signer;
	struct buffer duplicate_cn;
	struct payload_mm_crypto_owner *owner;
	struct payload_mm_crypto_span signed_data;
	struct payload_mm_cms_verified_signer verified;
	uint8_t *storage;

	assert(argc == 4);
	intermediate = read_file(argv[1]);
	signer = read_file(argv[2]);
	duplicate_cn = read_file(argv[3]);
	assert(signer.size == 833U);
	owner = calloc(1U, sizeof(*owner));
	storage = malloc(intermediate.size + signer.size);
	assert(owner != NULL && storage != NULL);
	setup_verified(&verified, &signed_data, storage, &intermediate, &signer);
	check_goldens(owner, &signed_data, &verified);
	check_duplicate_cn(owner, &duplicate_cn);
	check_conversion();
	check_hash_semantics();
	check_legacy(owner, &signed_data, &verified, &intermediate, &signer, storage);
	check_failures(owner, &signed_data, &verified);
	free(storage);
	free(owner);
	free(signer.data);
	free(intermediate.data);
	free(duplicate_cn.data);
	return 0;
}
