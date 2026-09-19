/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_cms.h"
#include "crypto.h"
#include "payload_mm_crypto_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct buffer {
	uint8_t *data;
	size_t size;
};

enum {
	GENERATED_CMS_CERTIFICATES_OFFSET = 58,
	GENERATED_AUTH_CERTIFICATES_OFFSET = 90,
	GENERATED_FIRST_CERTIFICATE_SIZE = 816,
	GENERATED_SECOND_CERTIFICATE_SIZE = 833,
	GENERATED_FIRST_CERTIFICATE_VALUE_OFFSET = 62,
	GENERATED_FIRST_CERTIFICATE_VALUE_SIZE = 812,
	GENERATED_CERTIFICATE_SET_END = 1707,
};

static int failures;

static void expect(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}

static struct buffer read_file(const char *path)
{
	struct buffer result = { 0 };
	long length;
	FILE *file;

	file = fopen(path, "rb");
	if (file == NULL || fseek(file, 0L, SEEK_END) != 0)
		goto out;
	length = ftell(file);
	if (length <= 0L || fseek(file, 0L, SEEK_SET) != 0)
		goto out;
	result.data = malloc((size_t)length);
	if (result.data == NULL)
		goto out;
	if (fread(result.data, 1U, (size_t)length, file) != (size_t)length) {
		free(result.data);
		result.data = NULL;
		goto out;
	}
	result.size = (size_t)length;
out:
	if (file != NULL)
		fclose(file);
	return result;
}

static struct buffer read_fixture(const char *directory, const char *name)
{
	char path[4096];

	if (snprintf(path, sizeof(path), "%s/%s", directory, name) < 0)
		return (struct buffer){ 0 };
	return read_file(path);
}

static bool arena_is_zero(const struct payload_mm_crypto_owner *owner)
{
	size_t index;

	for (index = 0U; index < sizeof(owner->arena); index++) {
		if (owner->arena[index] != 0U)
			return false;
	}
	return true;
}

static enum payload_mm_verify_status authenticate(
	struct payload_mm_crypto_owner *owner, const struct buffer *image,
	const struct buffer *trust, struct payload_mm_authenticated_image *result)
{
	const struct payload_mm_crypto_span image_span = {
		.data = image->data,
		.size = image->size,
	};
	const struct payload_mm_crypto_span trust_span = {
		.data = trust->data,
		.size = trust->size,
	};

	return payload_mm_authenticate_image(owner, &image_span, &trust_span,
		result);
}

static void check_cms(struct payload_mm_crypto_owner *owner,
	const struct buffer *cms, const struct buffer *digest,
	const struct buffer *trust, enum payload_mm_verify_status expected,
	const char *message)
{
	const struct payload_mm_crypto_span cms_span = {
		.data = cms->data,
		.size = cms->size,
	};
	const struct payload_mm_crypto_span trust_span = {
		.data = trust->data,
		.size = trust->size,
	};

	expect(payload_mm_cms_verify(owner, &cms_span, digest->data,
		&trust_span) == expected, message);
	expect(arena_is_zero(owner) && !owner->busy,
		"CMS verification retained protected state");
}

static void sweep_cms_allocations(struct payload_mm_crypto_owner *owner,
	const struct buffer *cms, const struct buffer *digest,
	const struct buffer *trust, const char *message)
{
	const struct payload_mm_crypto_span cms_span = {
		.data = cms->data,
		.size = cms->size,
	};
	const struct payload_mm_crypto_span trust_span = {
		.data = trust->data,
		.size = trust->size,
	};
	enum payload_mm_verify_status status = PAYLOAD_MM_VERIFY_INTERNAL;
	size_t fail_at;

	for (fail_at = 1U; fail_at <= PAYLOAD_MM_CRYPTO_MAX_ALLOCATIONS;
	     fail_at++) {
		owner->fail_allocation = fail_at;
		status = payload_mm_cms_verify(owner, &cms_span, digest->data,
			&trust_span);
		if (status == PAYLOAD_MM_VERIFY_OK)
			break;
		expect(status == PAYLOAD_MM_VERIFY_NO_MEMORY, message);
		expect(arena_is_zero(owner) && !owner->busy,
			"CMS allocation failure retained protected state");
	}
	expect(fail_at > 1U && fail_at <= PAYLOAD_MM_CRYPTO_MAX_ALLOCATIONS &&
		owner->allocation_count == fail_at - 1U,
		"CMS allocation-failure sweep skipped an allocation ordinal");
	owner->fail_allocation = 0U;
}

static void check_arena_bounds(void)
{
	static struct payload_mm_crypto_owner owner;
	size_t index;

	expect(payload_mm_crypto_begin(&owner) == PAYLOAD_MM_VERIFY_OK,
		"could not start single-allocation boundary test");
	expect(payload_mm_crypto_calloc(1U,
		PAYLOAD_MM_CRYPTO_MAX_ALLOCATION_SIZE) != NULL,
		"maximum single allocation rejected");
	expect(payload_mm_crypto_calloc(1U,
		PAYLOAD_MM_CRYPTO_MAX_ALLOCATION_SIZE + 1U) == NULL,
		"single allocation maximum plus one accepted");
	expect(payload_mm_crypto_end(&owner, PAYLOAD_MM_VERIFY_OK) ==
		PAYLOAD_MM_VERIFY_NO_MEMORY && arena_is_zero(&owner),
		"single-allocation failure did not unwind");

	expect(payload_mm_crypto_begin(&owner) == PAYLOAD_MM_VERIFY_OK,
		"could not start allocation-count boundary test");
	for (index = 0U; index < PAYLOAD_MM_CRYPTO_MAX_ALLOCATIONS; index++)
		expect(payload_mm_crypto_calloc(1U, 1U) != NULL,
			"allocation below count maximum rejected");
	expect(payload_mm_crypto_calloc(1U, 1U) == NULL,
		"allocation-count maximum plus one accepted");
	expect(payload_mm_crypto_end(&owner, PAYLOAD_MM_VERIFY_OK) ==
		PAYLOAD_MM_VERIFY_NO_MEMORY && arena_is_zero(&owner),
		"allocation-count failure did not unwind");

	expect(payload_mm_crypto_begin(&owner) == PAYLOAD_MM_VERIFY_OK,
		"could not start arena-size boundary test");
	for (index = 0U; index < PAYLOAD_MM_CRYPTO_ARENA_SIZE /
		PAYLOAD_MM_CRYPTO_MAX_ALLOCATION_SIZE; index++)
		expect(payload_mm_crypto_calloc(1U,
			PAYLOAD_MM_CRYPTO_MAX_ALLOCATION_SIZE) != NULL,
			"allocation below arena maximum rejected");
	expect(payload_mm_crypto_calloc(1U, 1U) == NULL,
		"arena-size maximum plus one accepted");
	expect(payload_mm_crypto_end(&owner, PAYLOAD_MM_VERIFY_OK) ==
		PAYLOAD_MM_VERIFY_NO_MEMORY && arena_is_zero(&owner),
		"arena-size failure did not unwind");
}

static size_t wrap_sequence(uint8_t *storage, size_t offset, size_t size)
{
	storage[offset - 2U] = 0x30U;
	storage[offset - 1U] = (uint8_t)size;
	return offset - 2U;
}

static bool swap_certificate_elements(struct buffer *buffer, size_t offset,
	size_t first_size, size_t second_size)
{
	uint8_t *certificates;
	size_t total;

	if (first_size > SIZE_MAX - second_size)
		return false;
	total = first_size + second_size;
	if (offset > buffer->size || total > buffer->size - offset)
		return false;
	certificates = malloc(total);
	if (certificates == NULL)
		return false;
	memcpy(certificates, buffer->data + offset, total);
	memcpy(buffer->data + offset, certificates + first_size, second_size);
	memcpy(buffer->data + offset + second_size, certificates, first_size);
	free(certificates);
	return true;
}

static void add_be16(uint8_t *bytes, size_t offset, size_t amount)
{
	size_t value = ((size_t)bytes[offset] << 8) | bytes[offset + 1U];

	value += amount;
	bytes[offset] = (uint8_t)(value >> 8);
	bytes[offset + 1U] = (uint8_t)value;
}

static struct buffer duplicate_first_certificate(const struct buffer *cms)
{
	struct buffer duplicate = { 0 };

	if (cms->size < GENERATED_CERTIFICATE_SET_END ||
	    cms->size > SIZE_MAX - GENERATED_FIRST_CERTIFICATE_SIZE)
		return duplicate;
	duplicate.size = cms->size + GENERATED_FIRST_CERTIFICATE_SIZE;
	duplicate.data = malloc(duplicate.size);
	if (duplicate.data == NULL)
		return duplicate;
	memcpy(duplicate.data, cms->data, GENERATED_CERTIFICATE_SET_END);
	memcpy(duplicate.data + GENERATED_CERTIFICATE_SET_END,
		cms->data + GENERATED_CMS_CERTIFICATES_OFFSET,
		GENERATED_FIRST_CERTIFICATE_SIZE);
	memcpy(duplicate.data + GENERATED_CERTIFICATE_SET_END +
		GENERATED_FIRST_CERTIFICATE_SIZE,
		cms->data + GENERATED_CERTIFICATE_SET_END,
		cms->size - GENERATED_CERTIFICATE_SET_END);
	add_be16(duplicate.data, 2U, GENERATED_FIRST_CERTIFICATE_SIZE);
	add_be16(duplicate.data, 17U, GENERATED_FIRST_CERTIFICATE_SIZE);
	add_be16(duplicate.data, 21U, GENERATED_FIRST_CERTIFICATE_SIZE);
	add_be16(duplicate.data, 56U, GENERATED_FIRST_CERTIFICATE_SIZE);
	return duplicate;
}

static void check_time_bounds(void)
{
	static const uint8_t utc_1950[] = "500101000000Z";
	static const uint8_t utc_2026[] = "260906172709Z";
	static const uint8_t utc_2049[] = "491231235959Z";
	static const uint8_t generalized_1949[] = "19491231235959Z";
	static const uint8_t generalized_1950[] = "19500101000000Z";
	static const uint8_t generalized_2026[] = "20260906172709Z";
	static const uint8_t generalized_2049[] = "20491231235959Z";
	static const uint8_t generalized_2050[] = "20500101000000Z";

	expect(payload_mm_auth_test_time_is_canonical(0x17U, utc_1950,
		sizeof(utc_1950) - 1U), "canonical 1950 UTCTime rejected");
	expect(payload_mm_auth_test_time_is_canonical(0x17U, utc_2026,
		sizeof(utc_2026) - 1U), "canonical 2026 UTCTime rejected");
	expect(payload_mm_auth_test_time_is_canonical(0x17U, utc_2049,
		sizeof(utc_2049) - 1U), "canonical 2049 UTCTime rejected");
	expect(payload_mm_auth_test_time_is_canonical(0x18U,
		generalized_1949, sizeof(generalized_1949) - 1U),
		"canonical 1949 GeneralizedTime rejected");
	expect(payload_mm_auth_test_time_is_canonical(0x18U,
		generalized_2050, sizeof(generalized_2050) - 1U),
		"canonical 2050 GeneralizedTime rejected");
	expect(!payload_mm_auth_test_time_is_canonical(0x18U,
		generalized_1950, sizeof(generalized_1950) - 1U),
		"noncanonical 1950 GeneralizedTime accepted");
	expect(!payload_mm_auth_test_time_is_canonical(0x18U,
		generalized_2026, sizeof(generalized_2026) - 1U),
		"noncanonical 2026 GeneralizedTime accepted");
	expect(!payload_mm_auth_test_time_is_canonical(0x18U,
		generalized_2049, sizeof(generalized_2049) - 1U),
		"noncanonical 2049 GeneralizedTime accepted");
	expect(!payload_mm_auth_test_time_is_canonical(0x17U,
		generalized_1949, sizeof(generalized_1949) - 1U),
		"GeneralizedTime encoded with UTCTime tag accepted");
}

static void check_der_bounds(void)
{
	uint8_t storage[PAYLOAD_MM_MAX_DER_OBJECTS * 2U + 4U] = { 0 };
	uint8_t nested[64] = { 0 };
	static const uint8_t short_long[] = { 0x04U, 0x81U, 0x7fU };
	static const uint8_t leading_zero_length[] = {
		0x04U, 0x82U, 0x00U, 0x80U,
	};
	static const uint8_t nested_indefinite[] = {
		0x30U, 0x02U, 0x04U, 0x80U,
	};
	static const uint8_t nested_high_tag[] = {
		0x30U, 0x02U, 0x1fU, 0x00U,
	};
	static const uint8_t sorted_set[] = {
		0x31U, 0x06U, 0x02U, 0x01U, 0x01U, 0x02U, 0x01U, 0x02U,
	};
	static const uint8_t reversed_set[] = {
		0x31U, 0x06U, 0x02U, 0x01U, 0x02U, 0x02U, 0x01U, 0x01U,
	};
	size_t index;
	size_t object_bytes;
	size_t offset = sizeof(nested) - 2U;
	size_t size = 2U;

	expect(!payload_mm_auth_test_der_is_canonical(short_long,
		sizeof(short_long)), "non-shortest DER length accepted");
	expect(!payload_mm_auth_test_der_is_canonical(leading_zero_length,
		sizeof(leading_zero_length)), "leading-zero DER length accepted");
	expect(!payload_mm_auth_test_der_is_canonical(nested_indefinite,
		sizeof(nested_indefinite)), "nested indefinite DER accepted");
	expect(!payload_mm_auth_test_der_is_canonical(nested_high_tag,
		sizeof(nested_high_tag)), "nested high-tag DER accepted");
	expect(payload_mm_auth_test_der_is_canonical(sorted_set,
		sizeof(sorted_set)), "canonical DER SET rejected");
	expect(!payload_mm_auth_test_der_is_canonical(reversed_set,
		sizeof(reversed_set)), "misordered DER SET accepted");
	storage[0] = 0x30U;
	storage[1] = 0x82U;
	object_bytes = (PAYLOAD_MM_MAX_DER_OBJECTS - 1U) * 2U;
	storage[2] = (uint8_t)(object_bytes >> 8);
	storage[3] = (uint8_t)object_bytes;
	for (index = 0U; index < PAYLOAD_MM_MAX_DER_OBJECTS - 1U; index++) {
		storage[4U + index * 2U] = 0x05U;
		storage[5U + index * 2U] = 0U;
	}
	expect(payload_mm_auth_test_der_is_canonical(storage, object_bytes + 4U),
		"maximum DER object count rejected");
	object_bytes = PAYLOAD_MM_MAX_DER_OBJECTS * 2U;
	storage[2] = (uint8_t)(object_bytes >> 8);
	storage[3] = (uint8_t)object_bytes;
	for (index = 0U; index < PAYLOAD_MM_MAX_DER_OBJECTS; index++) {
		storage[4U + index * 2U] = 0x05U;
		storage[5U + index * 2U] = 0U;
	}
	expect(!payload_mm_auth_test_der_is_canonical(storage, object_bytes + 4U),
		"DER object maximum plus one accepted");
	nested[offset] = 0x05U;
	nested[offset + 1U] = 0U;
	for (index = 0U; index < PAYLOAD_MM_MAX_DER_DEPTH; index++) {
		offset = wrap_sequence(nested, offset, size);
		size += 2U;
	}
	expect(payload_mm_auth_test_der_is_canonical(nested + offset, size),
		"maximum DER depth rejected");
	offset = wrap_sequence(nested, offset, size);
	size += 2U;
	expect(!payload_mm_auth_test_der_is_canonical(nested + offset, size),
		"DER depth maximum plus one accepted");
	expect(payload_mm_auth_test_attribute_size_is_valid(
		PAYLOAD_MM_MAX_CMS_ATTRIBUTE_SIZE), "attribute maximum rejected");
	expect(!payload_mm_auth_test_attribute_size_is_valid(
		PAYLOAD_MM_MAX_CMS_ATTRIBUTE_SIZE + 1U),
		"attribute maximum plus one accepted");
}

static void check_signed_artifacts(const char *directory)
{
	static struct payload_mm_crypto_owner owner;
	static struct payload_mm_crypto_owner second_owner;
	struct payload_mm_authenticated_image authenticated;
	struct buffer image = read_fixture(directory, "auth.bin");
	struct buffer image_zero = read_fixture(directory, "auth-zero.bin");
	struct buffer image_max = read_fixture(directory, "auth-max.bin");
	struct buffer payload = read_fixture(directory, "payload.bin");
	struct buffer trust = read_fixture(directory, "trust.xdr");
	struct buffer other_trust = read_fixture(directory, "other-trust.xdr");
	struct buffer cms = read_fixture(directory, "cms.der");
	struct buffer digest = read_fixture(directory, "content-digest.bin");
	struct buffer duplicate = read_fixture(directory, "duplicate-md.der");
	struct buffer unknown = read_fixture(directory, "unknown-attr.der");
	struct buffer attached = read_fixture(directory, "attached.der");
	struct buffer expired = read_fixture(directory, "expired.der");
	struct buffer future = read_fixture(directory, "future.der");
	struct buffer purpose = read_fixture(directory, "purpose.der");
	struct buffer partial_trust = read_fixture(directory, "partial-trust.xdr");
	struct buffer real_cms = read_fixture(directory, "real-cms.der");
	struct buffer real_digest = read_fixture(directory, "real-digest.bin");
	struct buffer real_trust = read_fixture(directory, "real-trust.xdr");
	struct buffer duplicate_certificate_cms = { 0 };
	struct buffer copy = { 0 };
	struct buffer trust_copy = { 0 };
	struct buffer cms_copy = { 0 };
	struct buffer excessive_trust = { 0 };
	uint8_t nested_certificate[32] = { 0 };
	uint8_t wrong_digest[PAYLOAD_MM_SHA256_SIZE];
	struct payload_mm_crypto_span cms_span;
	struct payload_mm_crypto_span digest_span;
	struct payload_mm_crypto_span trust_span;
	enum payload_mm_verify_status status;
	size_t fail_at;
	size_t index;
	size_t offset;
	size_t size;

	expect(image.data != NULL && image_zero.data != NULL &&
		image_max.data != NULL && payload.data != NULL && trust.data != NULL &&
		other_trust.data != NULL && cms.data != NULL && duplicate.data != NULL &&
		unknown.data != NULL && attached.data != NULL && expired.data != NULL &&
		future.data != NULL && purpose.data != NULL && partial_trust.data != NULL &&
		real_cms.data != NULL && real_trust.data != NULL &&
		digest.size == PAYLOAD_MM_SHA256_SIZE &&
		real_digest.size == PAYLOAD_MM_SHA256_SIZE, "fixture load failed");
	if (failures != 0)
		goto out;
	duplicate_certificate_cms = duplicate_first_certificate(&cms);
	expect(duplicate_certificate_cms.data != NULL,
		"duplicate-certificate CMS construction failed");
	if (duplicate_certificate_cms.data == NULL)
		goto out;
	expect(payload_mm_auth_test_der_is_canonical(cms.data, cms.size),
		"generated CMS is not bounded canonical DER");
	expect(payload_mm_auth_test_der_is_canonical(real_cms.data, real_cms.size),
		"real CMS failed bounded DER structural validation");
	status = authenticate(&owner, &image, &trust, &authenticated);
	expect(status == PAYLOAD_MM_VERIFY_OK, "EDK2 capsule rejected");
	expect(authenticated.payload.size == payload.size &&
		memcmp(authenticated.payload.data, payload.data, payload.size) == 0,
		"authenticated payload differs");
	expect(authenticate(&owner, &image_zero, &trust, &authenticated) ==
		PAYLOAD_MM_VERIFY_OK && authenticated.monotonic_count == 0U,
		"zero monotonic-count capsule rejected");
	expect(authenticate(&owner, &image_max, &trust, &authenticated) ==
		PAYLOAD_MM_VERIFY_OK && authenticated.monotonic_count == UINT64_MAX,
		"maximum monotonic-count capsule rejected");
	expect(arena_is_zero(&owner) && !owner.busy,
		"successful verification retained protected state");
	for (fail_at = 1U; fail_at <= PAYLOAD_MM_CRYPTO_MAX_ALLOCATIONS; fail_at++) {
		owner.fail_allocation = fail_at;
		status = authenticate(&owner, &image, &trust, &authenticated);
		if (status == PAYLOAD_MM_VERIFY_OK)
			break;
		expect(status == PAYLOAD_MM_VERIFY_NO_MEMORY,
			"allocation failure returned the wrong status");
		expect(authenticated.failure_source ==
			PAYLOAD_MM_AUTH_FAILURE_TRANSIENT,
			"allocation failure was not classified as transient");
		expect(arena_is_zero(&owner) && !owner.busy,
			"allocation failure retained protected state");
	}
	expect(fail_at > 1U && fail_at <= PAYLOAD_MM_CRYPTO_MAX_ALLOCATIONS,
		"allocation-failure sweep did not converge");
	expect(owner.allocation_count == fail_at - 1U,
		"allocation-failure sweep skipped an allocation ordinal");
	owner.fail_allocation = 0U;

	expect(payload_mm_crypto_begin(&owner) == PAYLOAD_MM_VERIFY_OK,
		"could not establish reentry test owner");
	expect(authenticate(&owner, &image, &trust, &authenticated) ==
		PAYLOAD_MM_VERIFY_BUSY, "reentrant verification accepted");
	expect(authenticate(&second_owner, &image, &trust, &authenticated) ==
		PAYLOAD_MM_VERIFY_BUSY, "parallel verification owner accepted");
	expect(payload_mm_crypto_end(&owner, PAYLOAD_MM_VERIFY_OK) ==
		PAYLOAD_MM_VERIFY_OK, "could not release reentry test owner");

	copy.data = malloc(image.size);
	trust_copy.data = malloc(trust.size);
	cms_copy.data = malloc(cms.size);
	excessive_trust.size = trust.size *
		(PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES + 1U);
	excessive_trust.data = malloc(excessive_trust.size);
	expect(copy.data != NULL && trust_copy.data != NULL &&
		cms_copy.data != NULL && excessive_trust.data != NULL,
		"mutation buffer allocation failed");
	if (copy.data == NULL || trust_copy.data == NULL ||
	    cms_copy.data == NULL || excessive_trust.data == NULL)
		goto out;
	copy.size = image.size;
	trust_copy.size = trust.size;
	cms_copy.size = cms.size;
	memcpy(copy.data, image.data, image.size);
	expect(swap_certificate_elements(&copy,
		GENERATED_AUTH_CERTIFICATES_OFFSET,
		GENERATED_FIRST_CERTIFICATE_SIZE,
		GENERATED_SECOND_CERTIFICATE_SIZE),
		"could not construct swapped capsule CertificateSet");
	expect(authenticate(&owner, &copy, &trust, &authenticated) ==
		PAYLOAD_MM_VERIFY_OK && authenticated.payload.size == payload.size,
		"capsule CertificateSet order changed authentication result");
	memcpy(cms_copy.data, cms.data, cms.size);
	expect(swap_certificate_elements(&cms_copy,
		GENERATED_CMS_CERTIFICATES_OFFSET,
		GENERATED_FIRST_CERTIFICATE_SIZE,
		GENERATED_SECOND_CERTIFICATE_SIZE),
		"could not construct swapped CMS CertificateSet");
	check_cms(&owner, &cms_copy, &digest, &trust,
		PAYLOAD_MM_VERIFY_OK,
		"CMS CertificateSet order changed verification result");
	check_cms(&owner, &duplicate_certificate_cms, &digest, &trust,
		PAYLOAD_MM_VERIFY_MALFORMED,
		"duplicate CMS certificate accepted");

	memcpy(cms_copy.data, cms.data, cms.size);
	nested_certificate[sizeof(nested_certificate) - 2U] = 0x05U;
	nested_certificate[sizeof(nested_certificate) - 1U] = 0U;
	offset = sizeof(nested_certificate) - 2U;
	size = 2U;
	for (index = 0U; index <= PAYLOAD_MM_MAX_DER_DEPTH; index++) {
		offset = wrap_sequence(nested_certificate, offset, size);
		size += 2U;
	}
	memcpy(cms_copy.data + GENERATED_FIRST_CERTIFICATE_VALUE_OFFSET,
		nested_certificate + offset, size);
	expect(!payload_mm_auth_test_der_is_canonical(cms_copy.data,
		cms_copy.size), "over-depth nested certificate remained canonical");
	check_cms(&owner, &cms_copy, &digest, &trust,
		PAYLOAD_MM_VERIFY_MALFORMED,
		"over-depth nested certificate accepted");

	memcpy(cms_copy.data, cms.data, cms.size);
	for (index = 0U;
	     index < GENERATED_FIRST_CERTIFICATE_VALUE_SIZE / 2U; index++) {
		cms_copy.data[GENERATED_FIRST_CERTIFICATE_VALUE_OFFSET +
			index * 2U] = 0x05U;
		cms_copy.data[GENERATED_FIRST_CERTIFICATE_VALUE_OFFSET +
			index * 2U + 1U] = 0U;
	}
	expect(!payload_mm_auth_test_der_is_canonical(cms_copy.data,
		cms_copy.size), "over-count nested certificate remained canonical");
	check_cms(&owner, &cms_copy, &digest, &trust,
		PAYLOAD_MM_VERIFY_MALFORMED,
		"over-count nested certificate accepted");

	memcpy(cms_copy.data, cms.data, cms.size);
	cms_copy.data[GENERATED_FIRST_CERTIFICATE_VALUE_OFFSET] = 0x30U;
	cms_copy.data[GENERATED_FIRST_CERTIFICATE_VALUE_OFFSET + 1U] = 0x81U;
	cms_copy.data[GENERATED_FIRST_CERTIFICATE_VALUE_OFFSET + 2U] = 0x7fU;
	expect(!payload_mm_auth_test_der_is_canonical(cms_copy.data,
		cms_copy.size), "non-short nested certificate length remained canonical");
	check_cms(&owner, &cms_copy, &digest, &trust,
		PAYLOAD_MM_VERIFY_MALFORMED,
		"non-short nested certificate length accepted");
	for (index = 0U; index < image.size; index += image.size / 7U) {
		memcpy(copy.data, image.data, image.size);
		copy.data[index] ^= 1U;
		expect(authenticate(&owner, &copy, &trust, &authenticated) !=
			PAYLOAD_MM_VERIFY_OK, "capsule mutation accepted");
	}
	expect(authenticate(&owner, &image, &other_trust, &authenticated) ==
		PAYLOAD_MM_VERIFY_REJECTED, "wrong root accepted");
	memcpy(trust_copy.data, trust.data, trust.size);
	trust_copy.data[trust_copy.size / 2U] ^= 1U;
	expect(authenticate(&owner, &image, &trust_copy, &authenticated) !=
		PAYLOAD_MM_VERIFY_OK, "mutated root accepted");
	for (index = 0U; index < image.size; index++) {
		copy.size = index;
		expect(authenticate(&owner, &copy, &trust, &authenticated) !=
			PAYLOAD_MM_VERIFY_OK, "truncated capsule accepted");
	}

	for (index = 0U; index <= PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES; index++)
		memcpy(excessive_trust.data + index * trust.size,
			trust.data, trust.size);
	check_cms(&owner, &cms, &digest, &excessive_trust,
		PAYLOAD_MM_VERIFY_MALFORMED, "excessive trust-anchor count accepted");
	cms_span = (struct payload_mm_crypto_span){
		cms.data, PAYLOAD_MM_MAX_CMS_SIZE + 1U };
	trust_span = (struct payload_mm_crypto_span){ trust.data, trust.size };
	expect(payload_mm_cms_verify(&owner, &cms_span, digest.data,
		&trust_span) == PAYLOAD_MM_VERIFY_INVALID,
		"oversize CMS accepted");
	trust_span.size = PAYLOAD_MM_MAX_TRUST_XDR_SIZE + 1U;
	cms_span.size = cms.size;
	expect(payload_mm_cms_verify(&owner, &cms_span, digest.data,
		&trust_span) == PAYLOAD_MM_VERIFY_INVALID,
		"oversize trust set accepted");

	digest_span = (struct payload_mm_crypto_span){ digest.data, digest.size };
	check_cms(&owner, &cms, &digest, &trust, PAYLOAD_MM_VERIFY_OK,
		"CDK2 detached CMS artifact rejected");
	memcpy(wrong_digest, digest.data, sizeof(wrong_digest));
	wrong_digest[0] ^= 1U;
	digest_span.data = wrong_digest;
	cms_span = (struct payload_mm_crypto_span){ cms.data, cms.size };
	trust_span = (struct payload_mm_crypto_span){ trust.data, trust.size };
	expect(payload_mm_cms_verify(&owner, &cms_span, digest_span.data,
		&trust_span) == PAYLOAD_MM_VERIFY_REJECTED,
		"mutated content digest accepted");
	digest_span.data = digest.data;
	check_cms(&owner, &duplicate, &digest, &trust,
		PAYLOAD_MM_VERIFY_MALFORMED,
		"duplicate messageDigest accepted");
	check_cms(&owner, &unknown, &digest, &trust,
		PAYLOAD_MM_VERIFY_MALFORMED,
		"unknown authenticated attribute accepted");
	check_cms(&owner, &attached, &digest, &trust,
		PAYLOAD_MM_VERIFY_UNSUPPORTED,
		"attached CMS content accepted");
	check_cms(&owner, &expired, &digest, &trust, PAYLOAD_MM_VERIFY_OK,
		"expired signer rejected despite no-time primitive policy");
	check_cms(&owner, &future, &digest, &trust, PAYLOAD_MM_VERIFY_OK,
		"future signer rejected despite no-time primitive policy");
	check_cms(&owner, &purpose, &digest, &trust, PAYLOAD_MM_VERIFY_OK,
		"purpose-restricted signer rejected by purpose-neutral primitive");
	check_cms(&owner, &cms, &digest, &partial_trust, PAYLOAD_MM_VERIFY_OK,
		"partial-chain trust anchor rejected");
	sweep_cms_allocations(&owner, &cms, &digest, &trust,
		"generated CMS allocation failure returned the wrong status");
	sweep_cms_allocations(&owner, &cms, &digest, &partial_trust,
		"partial-chain allocation failure returned the wrong status");
	sweep_cms_allocations(&owner, &real_cms, &real_digest, &real_trust,
		"real CMS allocation failure returned the wrong status");
	trust_span = (struct payload_mm_crypto_span){ trust.data, trust.size };
	cms_span = (struct payload_mm_crypto_span){ cms.data, cms.size };
	memcpy(cms_copy.data, cms.data, cms.size);
	cms_copy.data[cms_copy.size / 3U] ^= 1U;
	check_cms(&owner, &cms_copy, &digest, &trust,
		PAYLOAD_MM_VERIFY_REJECTED, "mutated certificate chain accepted");
	cms.data[cms.size - 1U] ^= 1U;
	expect(payload_mm_cms_verify(&owner, &cms_span, digest_span.data,
		&trust_span) != PAYLOAD_MM_VERIFY_OK, "CMS signature mutation accepted");
	cms_span = (struct payload_mm_crypto_span){ real_cms.data, real_cms.size };
	trust_span = (struct payload_mm_crypto_span){
		real_trust.data, real_trust.size };
	expect(payload_mm_cms_verify(&owner, &cms_span, real_digest.data,
		&trust_span) == PAYLOAD_MM_VERIFY_OK,
		"real EDK2 CMS/root artifact rejected");
out:
	free(duplicate_certificate_cms.data);
	free(excessive_trust.data);
	free(cms_copy.data);
	free(trust_copy.data);
	free(real_trust.data);
	free(real_digest.data);
	free(real_cms.data);
	free(partial_trust.data);
	free(purpose.data);
	free(future.data);
	free(expired.data);
	free(attached.data);
	free(unknown.data);
	free(duplicate.data);
	free(copy.data);
	free(digest.data);
	free(cms.data);
	free(other_trust.data);
	free(trust.data);
	free(payload.data);
	free(image_max.data);
	free(image_zero.data);
	free(image.data);
}

static void check_real_capsule(const char *directory, const char *image_path)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_authenticated_image authenticated;
	struct buffer image = read_file(image_path);
	struct buffer trust = read_fixture(directory, "real-trust.xdr");

	expect(image.data != NULL && trust.data != NULL,
		"real capsule fixture load failed");
	if (image.data != NULL && trust.data != NULL) {
		expect(authenticate(&owner, &image, &trust, &authenticated) ==
			PAYLOAD_MM_VERIFY_OK,
			"real EDK2 capsule authentication image rejected");
		expect(authenticated.payload.size == image.size - 3650U,
			"real EDK2 capsule payload boundary differs");
	}
	free(trust.data);
	free(image.data);
}

int main(int argc, char **argv)
{
	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: %s FIXTURE-DIRECTORY [REAL-AUTH-IMAGE]\n",
			argv[0]);
		return 2;
	}
	check_der_bounds();
	check_time_bounds();
	check_arena_bounds();
	check_signed_artifacts(argv[1]);
	if (argc == 3)
		check_real_capsule(argv[1], argv[2]);
	if (failures != 0)
		return 1;
	puts("payload_mm CMS/X.509 verifier: PASS");
	return 0;
}
