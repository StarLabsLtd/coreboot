/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_cms.h"
#include "crypto.h"

#include <commonlib/bsd/helpers.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct buffer {
	uint8_t *data;
	size_t size;
};

static int failures;

static void expect(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}

static struct buffer read_file(const char *directory, const char *name)
{
	struct buffer result = { 0 };
	char path[4096];
	long length;
	FILE *file = NULL;

	if (snprintf(path, sizeof(path), "%s/%s", directory, name) < 0)
		return result;
	file = fopen(path, "rb");
	if (!file || fseek(file, 0L, SEEK_END) || (length = ftell(file)) <= 0L ||
	    fseek(file, 0L, SEEK_SET))
		goto out;
	result.data = malloc((size_t)length);
	if (!result.data)
		goto out;
	if (fread(result.data, 1, (size_t)length, file) != (size_t)length) {
		free(result.data);
		result.data = NULL;
		goto out;
	}
	result.size = (size_t)length;
out:
	if (file)
		fclose(file);
	return result;
}

static bool arena_is_zero(const struct payload_mm_crypto_owner *owner)
{
	for (size_t i = 0; i < sizeof(owner->arena); i++)
		if (owner->arena[i])
			return false;
	return true;
}

static bool result_is_fill(const struct payload_mm_cms_verified_signer *result,
	uint8_t fill)
{
	const uint8_t *bytes = (const uint8_t *)result;

	for (size_t i = 0; i < sizeof(*result); i++)
		if (bytes[i] != fill)
			return false;
	return true;
}

static bool span_inside(const struct payload_mm_crypto_span *span,
	const struct buffer *container)
{
	uintptr_t data = (uintptr_t)span->data;
	uintptr_t start = (uintptr_t)container->data;

	return data >= start && data - start <= container->size &&
		span->size <= container->size - (data - start);
}

static void make_content_spans(const struct buffer *content,
	struct payload_mm_crypto_span spans[PAYLOAD_MM_HASH_MAX_SPANS])
{
	static const size_t sizes[PAYLOAD_MM_HASH_MAX_SPANS - 1U] = {
		18U, 16U, 4U, 16U,
	};
	size_t offset = 0;

	for (size_t i = 0; i < PAYLOAD_MM_HASH_MAX_SPANS - 1U; i++) {
		spans[i].data = content->data + offset;
		spans[i].size = sizes[i];
		offset += sizes[i];
	}
	spans[PAYLOAD_MM_HASH_MAX_SPANS - 1U].data = content->data + offset;
	spans[PAYLOAD_MM_HASH_MAX_SPANS - 1U].size = content->size - offset;
}

static enum payload_mm_verify_status verify(
	struct payload_mm_crypto_owner *owner, const struct buffer *cms,
	const struct payload_mm_crypto_span *spans, size_t span_count,
	struct payload_mm_cms_verified_signer *result)
{
	const struct payload_mm_crypto_span signed_data = {
		.data = cms->data,
		.size = cms->size,
	};

	return payload_mm_cms_verify_detached_untrusted(owner, &signed_data,
		spans, span_count, result);
}

static enum payload_mm_verify_status verify_direct(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *spans, size_t span_count,
	struct payload_mm_cms_verified_signer *result)
{
	return payload_mm_cms_verify_detached_untrusted(owner, signed_data,
		spans, span_count, result);
}

static void check_success(const struct buffer *cms, const struct buffer *content,
	enum payload_mm_hash_algorithm expected_algorithm)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_crypto_span spans[PAYLOAD_MM_HASH_MAX_SPANS];
	struct payload_mm_cms_verified_signer result;

	memset(&owner, 0, sizeof(owner));
	memset(&result, 0xa5, sizeof(result));
	make_content_spans(content, spans);
	expect(verify(&owner, cms, spans, ARRAY_SIZE(spans), &result) ==
		PAYLOAD_MM_VERIFY_OK, "valid five-span detached CMS rejected");
	expect(result.digest_algorithm == expected_algorithm,
		"wrong digest algorithm published");
	expect(result.certificate_count == 1U,
		"unexpected generated certificate count");
	expect(span_inside(&result.signer_certificate, cms),
		"signer certificate does not alias CMS input");
	expect(result.certificate_count == 1U &&
		span_inside(&result.certificates[0], cms),
		"certificate view does not alias CMS input");
	expect(result.certificate_count == 1U &&
		result.signer_certificate.data == result.certificates[0].data &&
		result.signer_certificate.size == result.certificates[0].size,
		"wrong signer certificate selected");
	expect(!owner.busy && owner.arena_used == 0U && arena_is_zero(&owner),
		"successful verification retained protected state");

	spans[0] = (struct payload_mm_crypto_span){ 0 };
	spans[1] = (struct payload_mm_crypto_span) {
		.data = content->data,
		.size = 18U,
	};
	spans[2] = (struct payload_mm_crypto_span){ 0 };
	spans[3] = (struct payload_mm_crypto_span) {
		.data = content->data + 18U,
		.size = content->size - 18U,
	};
	spans[4] = (struct payload_mm_crypto_span){ 0 };
	memset(&owner, 0, sizeof(owner));
	memset(&result, 0xa5, sizeof(result));
	expect(verify(&owner, cms, spans, ARRAY_SIZE(spans), &result) ==
		PAYLOAD_MM_VERIFY_OK,
		"valid CMS with zero-length content spans was rejected");
	expect(result.digest_algorithm == expected_algorithm,
		"zero-length span call published the wrong algorithm");
	expect(!owner.busy && arena_is_zero(&owner),
		"zero-length span verification retained protected state");
}

static void check_content_mutations(const struct buffer *cms,
	const struct buffer *content)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_crypto_span spans[PAYLOAD_MM_HASH_MAX_SPANS];
	struct payload_mm_cms_verified_signer result;
	struct buffer changed = { malloc(content->size), content->size };

	expect(changed.data != NULL, "could not allocate content mutation");
	if (!changed.data)
		return;
	for (size_t i = 0; i < PAYLOAD_MM_HASH_MAX_SPANS; i++) {
		memcpy(changed.data, content->data, content->size);
		make_content_spans(&changed, spans);
		changed.data[(size_t)(spans[i].data - changed.data) +
			spans[i].size / 2U] ^= 0x80U;
		memset(&owner, 0, sizeof(owner));
		memset(&result, 0xa5, sizeof(result));
		expect(verify(&owner, cms, spans, ARRAY_SIZE(spans), &result) ==
			PAYLOAD_MM_VERIFY_REJECTED,
			"mutated detached content was not rejected");
		expect(result_is_fill(&result, 0xa5),
			"content failure partially published output");
		expect(!owner.busy && arena_is_zero(&owner),
			"content failure retained protected state");
	}
	free(changed.data);
}

static bool mutate_second_sha256_oid(struct buffer *cms)
{
	static const uint8_t oid[] = {
		0x60U, 0x86U, 0x48U, 0x01U, 0x65U,
		0x03U, 0x04U, 0x02U, 0x01U,
	};
	size_t matches = 0;

	for (size_t i = 0; i + sizeof(oid) <= cms->size; i++) {
		if (memcmp(cms->data + i, oid, sizeof(oid)))
			continue;
		if (++matches == 2U) {
			cms->data[i + sizeof(oid) - 1U] = 0x02U;
			return true;
		}
	}
	return false;
}

static void check_cms_mutations(const struct buffer *original,
	const struct buffer *content)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_crypto_span spans[PAYLOAD_MM_HASH_MAX_SPANS];
	struct payload_mm_cms_verified_signer result;
	struct buffer cms = { malloc(original->size), original->size };

	expect(cms.data != NULL, "could not allocate CMS mutation");
	if (!cms.data)
		return;
	make_content_spans(content, spans);
	memcpy(cms.data, original->data, original->size);
	cms.data[cms.size - 1U] ^= 1U;
	memset(&owner, 0, sizeof(owner));
	memset(&result, 0xa5, sizeof(result));
	expect(verify(&owner, &cms, spans, ARRAY_SIZE(spans), &result) ==
		PAYLOAD_MM_VERIFY_REJECTED, "mutated RSA signature was accepted");
	expect(result_is_fill(&result, 0xa5),
		"signature failure partially published output");
	expect(!owner.busy && arena_is_zero(&owner),
		"signature failure retained protected state");

	memcpy(cms.data, original->data, original->size);
	expect(mutate_second_sha256_oid(&cms),
		"could not locate both SHA-256 algorithm identifiers");
	memset(&owner, 0, sizeof(owner));
	memset(&result, 0xa5, sizeof(result));
	expect(verify(&owner, &cms, spans, ARRAY_SIZE(spans), &result) ==
		PAYLOAD_MM_VERIFY_UNSUPPORTED,
		"mismatched SignedData and SignerInfo algorithms accepted");
	expect(result_is_fill(&result, 0xa5),
		"algorithm mismatch partially published output");
	expect(!owner.busy && arena_is_zero(&owner),
		"algorithm mismatch retained protected state");
	free(cms.data);
}

static void check_boundaries(const struct buffer *cms,
	const struct buffer *content)
{
	static struct payload_mm_crypto_owner owner;
	static struct payload_mm_crypto_owner blocker;
	struct payload_mm_crypto_span spans[PAYLOAD_MM_HASH_MAX_SPANS + 1U];
	struct payload_mm_cms_verified_signer result;

	make_content_spans(content, spans);
	spans[PAYLOAD_MM_HASH_MAX_SPANS] = (struct payload_mm_crypto_span){ 0 };
	memset(&result, 0xa5, sizeof(result));
	expect(verify(&owner, cms, spans, ARRAY_SIZE(spans), &result) ==
		PAYLOAD_MM_VERIFY_INVALID, "too many content spans accepted");
	expect(result_is_fill(&result, 0xa5),
		"preflight failure modified output");

	make_content_spans(content, spans);
	spans[2].data = NULL;
	memset(&owner, 0, sizeof(owner));
	memset(&result, 0xa5, sizeof(result));
	expect(verify(&owner, cms, spans, PAYLOAD_MM_HASH_MAX_SPANS, &result) ==
		PAYLOAD_MM_VERIFY_INVALID, "nonempty null content span accepted");
	expect(result_is_fill(&result, 0xa5),
		"invalid span partially published output");
	expect(!owner.busy && arena_is_zero(&owner),
		"invalid span retained protected state");

	make_content_spans(content, spans);
	memset(&owner, 0, sizeof(owner));
	memset(&blocker, 0, sizeof(blocker));
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_crypto_begin(&blocker) == PAYLOAD_MM_VERIFY_OK,
		"could not acquire crypto blocker");
	expect(verify(&owner, cms, spans, PAYLOAD_MM_HASH_MAX_SPANS, &result) ==
		PAYLOAD_MM_VERIFY_BUSY, "nested crypto verification was not busy");
	expect(result_is_fill(&result, 0xa5), "busy call modified output");
	expect(payload_mm_crypto_end(&blocker, PAYLOAD_MM_VERIFY_OK) ==
		PAYLOAD_MM_VERIFY_OK && arena_is_zero(&blocker),
		"crypto blocker did not unwind");

	memset(&owner, 0, sizeof(owner));
	owner.fail_allocation = 1U;
	memset(&result, 0xa5, sizeof(result));
	expect(verify(&owner, cms, spans, PAYLOAD_MM_HASH_MAX_SPANS, &result) ==
		PAYLOAD_MM_VERIFY_NO_MEMORY,
		"injected first allocation failure was not reported");
	expect(result_is_fill(&result, 0xa5),
		"allocation failure partially published output");
	expect(!owner.busy && owner.arena_used == 0U && arena_is_zero(&owner),
		"allocation failure retained protected state");
}

static void expect_alias_status(enum payload_mm_verify_status status,
	enum payload_mm_verify_status expected, const char *message)
{
	expect(status == expected, message);
}

static void check_signed_data_byte_aliases(const struct buffer *cms,
	const struct buffer *content)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_crypto_span spans[PAYLOAD_MM_HASH_MAX_SPANS];
	struct payload_mm_crypto_span signed_data;
	struct payload_mm_cms_verified_signer *result;
	size_t alignment = _Alignof(struct payload_mm_cms_verified_signer);
	size_t prefix = (alignment - (cms->size % alignment)) % alignment;
	uint8_t *storage = malloc(prefix + cms->size + sizeof(*result));

	expect(storage != NULL, "could not allocate signed-data alias storage");
	if (!storage)
		return;
	memcpy(storage, cms->data, cms->size);
	signed_data = (struct payload_mm_crypto_span) {
		.data = storage,
		.size = cms->size,
	};
	make_content_spans(content, spans);
	memset(&owner, 0, sizeof(owner));
	result = (void *)signed_data.data;
	expect_alias_status(verify_direct(&owner, &signed_data, spans,
		ARRAY_SIZE(spans), result), PAYLOAD_MM_VERIFY_INVALID,
		"result exactly aliasing signed-data bytes was accepted");
	result = (void *)(signed_data.data + alignment);
	expect_alias_status(verify_direct(&owner, &signed_data, spans,
		ARRAY_SIZE(spans), result), PAYLOAD_MM_VERIFY_INVALID,
		"result partially aliasing signed-data bytes was accepted");
	memcpy(storage + prefix, cms->data, cms->size);
	signed_data.data = storage + prefix;
	result = (void *)(signed_data.data + signed_data.size);
	expect_alias_status(verify_direct(&owner, &signed_data, spans,
		ARRAY_SIZE(spans), result), PAYLOAD_MM_VERIFY_OK,
		"result adjacent to signed-data bytes was rejected");
	expect(!owner.busy && arena_is_zero(&owner),
		"signed-data byte alias checks retained protected state");
	free(storage);
}

static void check_content_byte_aliases(const struct buffer *cms,
	const struct buffer *content)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_crypto_span signed_data = { cms->data, cms->size };
	struct payload_mm_crypto_span spans[PAYLOAD_MM_HASH_MAX_SPANS];
	struct payload_mm_cms_verified_signer *result;
	size_t alignment = _Alignof(struct payload_mm_cms_verified_signer);
	size_t prefix = (alignment - (content->size % alignment)) % alignment;
	struct buffer copied;
	uint8_t *storage;

	storage = malloc(prefix + content->size + sizeof(*result));
	copied.data = storage;
	copied.size = content->size;
	expect(copied.data != NULL, "could not allocate content alias storage");
	if (!copied.data)
		return;
	copied.data += prefix;
	memcpy(copied.data, content->data, content->size);
	make_content_spans(&copied, spans);
	memset(&owner, 0, sizeof(owner));
	result = (void *)copied.data;
	expect_alias_status(verify_direct(&owner, &signed_data, spans,
		ARRAY_SIZE(spans), result), PAYLOAD_MM_VERIFY_INVALID,
		"result exactly aliasing content bytes was accepted");
	result = (void *)(copied.data + alignment);
	expect_alias_status(verify_direct(&owner, &signed_data, spans,
		ARRAY_SIZE(spans), result), PAYLOAD_MM_VERIFY_INVALID,
		"result partially aliasing content bytes was accepted");
	copied.data = storage + prefix;
	memcpy(copied.data, content->data, content->size);
	make_content_spans(&copied, spans);
	result = (void *)(copied.data + copied.size);
	expect_alias_status(verify_direct(&owner, &signed_data, spans,
		ARRAY_SIZE(spans), result), PAYLOAD_MM_VERIFY_OK,
		"result adjacent to content bytes was rejected");
	expect(!owner.busy && arena_is_zero(&owner),
		"content byte alias checks retained protected state");
	free(storage);
}

static void check_descriptor_aliases(const struct buffer *cms,
	const struct buffer *content)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_crypto_span signed_data = { cms->data, cms->size };
	struct payload_mm_crypto_span content_spans[PAYLOAD_MM_HASH_MAX_SPANS];
	struct payload_mm_cms_verified_signer result;
	union {
		max_align_t alignment;
		uint8_t bytes[sizeof(content_spans) +
			sizeof(struct payload_mm_cms_verified_signer)];
	} content_storage;
	union {
		max_align_t alignment;
		uint8_t bytes[sizeof(signed_data) +
			sizeof(struct payload_mm_cms_verified_signer)];
	} signed_storage;
	struct payload_mm_crypto_span *stored_content =
		(void *)content_storage.bytes;
	struct payload_mm_crypto_span *stored_signed =
		(void *)signed_storage.bytes;
	size_t alignment = _Alignof(struct payload_mm_cms_verified_signer);

	make_content_spans(content, content_spans);
	memcpy(stored_content, content_spans, sizeof(content_spans));
	memset(&owner, 0, sizeof(owner));
	expect_alias_status(verify_direct(&owner, &signed_data, stored_content,
		ARRAY_SIZE(content_spans), (void *)stored_content),
		PAYLOAD_MM_VERIFY_INVALID,
		"result exactly aliasing content descriptors was accepted");
	expect_alias_status(verify_direct(&owner, &signed_data, stored_content,
		ARRAY_SIZE(content_spans), (void *)((uint8_t *)stored_content + alignment)),
		PAYLOAD_MM_VERIFY_INVALID,
		"result partially aliasing content descriptors was accepted");
	expect_alias_status(verify_direct(&owner, &signed_data, stored_content,
		ARRAY_SIZE(content_spans), (void *)(stored_content +
		ARRAY_SIZE(content_spans))), PAYLOAD_MM_VERIFY_OK,
		"result adjacent to content descriptors was rejected");

	*stored_signed = signed_data;
	memset(&result, 0xa5, sizeof(result));
	expect_alias_status(verify_direct(&owner, stored_signed, content_spans,
		ARRAY_SIZE(content_spans), (void *)stored_signed),
		PAYLOAD_MM_VERIFY_INVALID,
		"result exactly aliasing signed-data descriptor was accepted");
	expect_alias_status(verify_direct(&owner, stored_signed, content_spans,
		ARRAY_SIZE(content_spans), (void *)((uint8_t *)stored_signed + alignment)),
		PAYLOAD_MM_VERIFY_INVALID,
		"result partially aliasing signed-data descriptor was accepted");
	expect_alias_status(verify_direct(&owner, stored_signed, content_spans,
		ARRAY_SIZE(content_spans), (void *)(stored_signed + 1U)),
		PAYLOAD_MM_VERIFY_OK,
		"result adjacent to signed-data descriptor was rejected");
	expect(!owner.busy && arena_is_zero(&owner),
		"descriptor alias checks retained protected state");
}

static void check_owner_aliases(const struct buffer *cms,
	const struct buffer *content)
{
	struct owner_storage {
		struct payload_mm_crypto_owner owner;
		struct payload_mm_cms_verified_signer adjacent;
	};
	static struct owner_storage storage;
	struct payload_mm_crypto_span signed_data = { cms->data, cms->size };
	struct payload_mm_crypto_span spans[PAYLOAD_MM_HASH_MAX_SPANS];
	size_t alignment = _Alignof(struct payload_mm_cms_verified_signer);

	make_content_spans(content, spans);
	memset(&storage, 0, sizeof(storage));
	expect_alias_status(verify_direct(&storage.owner, &signed_data, spans,
		ARRAY_SIZE(spans), (void *)&storage.owner), PAYLOAD_MM_VERIFY_INVALID,
		"result exactly aliasing owner was accepted");
	expect_alias_status(verify_direct(&storage.owner, &signed_data, spans,
		ARRAY_SIZE(spans), (void *)((uint8_t *)&storage.owner + alignment)),
		PAYLOAD_MM_VERIFY_INVALID,
		"result partially aliasing owner was accepted");
	expect_alias_status(verify_direct(&storage.owner, &signed_data, spans,
		ARRAY_SIZE(spans), (void *)storage.owner.arena),
		PAYLOAD_MM_VERIFY_INVALID,
		"result exactly aliasing owner arena was accepted");
	expect_alias_status(verify_direct(&storage.owner, &signed_data, spans,
		ARRAY_SIZE(spans), (void *)(storage.owner.arena + alignment)),
		PAYLOAD_MM_VERIFY_INVALID,
		"result partially aliasing owner arena was accepted");
	expect_alias_status(verify_direct(&storage.owner, &signed_data, spans,
		ARRAY_SIZE(spans), (void *)(storage.owner.arena +
		sizeof(storage.owner.arena))), PAYLOAD_MM_VERIFY_INVALID,
		"result adjacent to arena but inside owner was accepted");
	expect_alias_status(verify_direct(&storage.owner, &signed_data, spans,
		ARRAY_SIZE(spans), &storage.adjacent), PAYLOAD_MM_VERIFY_OK,
		"result adjacent to owner was rejected");
	expect(!storage.owner.busy && arena_is_zero(&storage.owner),
		"owner alias checks retained protected state");
}

static void check_allocation_sweep(const struct buffer *cms,
	const struct buffer *content)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_crypto_span spans[PAYLOAD_MM_HASH_MAX_SPANS];
	struct payload_mm_cms_verified_signer result;
	enum payload_mm_verify_status status = PAYLOAD_MM_VERIFY_INTERNAL;
	size_t fail_at;

	make_content_spans(content, spans);
	for (fail_at = 1U; fail_at <= PAYLOAD_MM_CRYPTO_MAX_ALLOCATIONS;
	     fail_at++) {
		memset(&owner, 0, sizeof(owner));
		owner.fail_allocation = fail_at;
		memset(&result, 0xa5, sizeof(result));
		status = verify(&owner, cms, spans, ARRAY_SIZE(spans), &result);
		if (status == PAYLOAD_MM_VERIFY_OK)
			break;
		expect(status == PAYLOAD_MM_VERIFY_NO_MEMORY,
			"allocation sweep returned the wrong failure");
		expect(result_is_fill(&result, 0xa5),
			"allocation sweep partially published output");
		expect(!owner.busy && arena_is_zero(&owner),
			"allocation sweep retained protected state");
	}
	expect(fail_at > 1U && fail_at <= PAYLOAD_MM_CRYPTO_MAX_ALLOCATIONS &&
		owner.allocation_count == fail_at - 1U,
		"allocation sweep did not reach the first successful ordinal");
	expect(status == PAYLOAD_MM_VERIFY_OK && !owner.busy && arena_is_zero(&owner),
		"allocation sweep success did not unwind");
}

static void check_hash_edges(const struct buffer *content,
	const struct buffer *sha384_digest)
{
	struct payload_mm_crypto_span spans[PAYLOAD_MM_HASH_MAX_SPANS];
	struct {
		uint8_t digest[PAYLOAD_MM_MAX_DIGEST_SIZE];
		uint8_t canary[16];
	} output;
	uint8_t full_digest[PAYLOAD_MM_MAX_DIGEST_SIZE];
	static const uint8_t canary = 0xa5U;

	spans[0] = (struct payload_mm_crypto_span) {
		.data = (const void *)(uintptr_t)1U,
		.size = PAYLOAD_MM_CRYPTO_MAX_MESSAGE_SIZE,
	};
	spans[1] = (struct payload_mm_crypto_span) {
		.data = (const void *)(uintptr_t)1U,
		.size = 1U,
	};
	expect(payload_mm_hash_spans(PAYLOAD_MM_HASH_SHA256, spans, 2U,
		full_digest) == PAYLOAD_MM_VERIFY_INVALID,
		"cumulative message maximum plus one was accepted");
	spans[0].size = SIZE_MAX;
	expect(payload_mm_hash_spans(PAYLOAD_MM_HASH_SHA256, spans, 1U,
		full_digest) == PAYLOAD_MM_VERIFY_INVALID,
		"SIZE_MAX span was accepted or read");

	spans[0] = (struct payload_mm_crypto_span){ 0 };
	spans[1] = (struct payload_mm_crypto_span) {
		.data = content->data,
		.size = 18U,
	};
	spans[2] = (struct payload_mm_crypto_span){ 0 };
	spans[3] = (struct payload_mm_crypto_span) {
		.data = content->data + 18U,
		.size = content->size - 18U,
	};
	spans[4] = (struct payload_mm_crypto_span){ 0 };
	expect(payload_mm_hash_spans(PAYLOAD_MM_HASH_SHA384, spans,
		ARRAY_SIZE(spans), full_digest) == PAYLOAD_MM_VERIFY_OK,
		"zero-length spans were rejected");
	expect(sha384_digest->size == PAYLOAD_MM_SHA384_SIZE &&
		!memcmp(full_digest, sha384_digest->data, PAYLOAD_MM_SHA384_SIZE),
		"SHA-384 digest across zero-length spans was wrong");

	memset(&output, canary, sizeof(output));
	spans[0] = (struct payload_mm_crypto_span) {
		.data = content->data,
		.size = content->size,
	};
	expect(payload_mm_hash_spans(PAYLOAD_MM_HASH_SHA384, spans, 1U,
		output.digest) == PAYLOAD_MM_VERIFY_OK,
		"direct SHA-384 hash failed");
	expect(sha384_digest->size == PAYLOAD_MM_SHA384_SIZE &&
		!memcmp(output.digest, sha384_digest->data, PAYLOAD_MM_SHA384_SIZE),
		"direct SHA-384 digest was wrong");
	for (size_t i = PAYLOAD_MM_SHA384_SIZE; i < sizeof(output.digest); i++)
		expect(output.digest[i] == 0U,
			"SHA-384 did not clear its unused digest tail");
	for (size_t i = 0; i < sizeof(output.canary); i++)
		expect(output.canary[i] == canary,
			"SHA-384 wrote beyond its 48-byte digest");
}

int main(int argc, char **argv)
{
	static const char * const cms_names[] = {
		"sha256.der", "sha384.der", "sha512.der",
	};
	static const enum payload_mm_hash_algorithm algorithms[] = {
		PAYLOAD_MM_HASH_SHA256,
		PAYLOAD_MM_HASH_SHA384,
		PAYLOAD_MM_HASH_SHA512,
	};
	struct buffer content;
	struct buffer sha384_digest;
	struct buffer cms[ARRAY_SIZE(cms_names)];

	if (argc != 2) {
		fprintf(stderr, "usage: %s FIXTURE_DIRECTORY\n", argv[0]);
		return 2;
	}
	content = read_file(argv[1], "content.bin");
	sha384_digest = read_file(argv[1], "content.sha384");
	expect(content.data != NULL && content.size > 54U,
		"could not read detached content fixture");
	for (size_t i = 0; i < ARRAY_SIZE(cms); i++) {
		cms[i] = read_file(argv[1], cms_names[i]);
		expect(cms[i].data != NULL, "could not read CMS fixture");
		if (cms[i].data && content.data)
			check_success(&cms[i], &content, algorithms[i]);
		if (cms[i].data && content.data)
			check_allocation_sweep(&cms[i], &content);
	}
	if (cms[0].data && content.data) {
		check_content_mutations(&cms[0], &content);
		check_cms_mutations(&cms[0], &content);
		check_boundaries(&cms[0], &content);
		check_signed_data_byte_aliases(&cms[0], &content);
		check_content_byte_aliases(&cms[0], &content);
		check_descriptor_aliases(&cms[0], &content);
		check_owner_aliases(&cms[0], &content);
	}
	if (content.data && sha384_digest.data)
		check_hash_edges(&content, &sha384_digest);
	for (size_t i = 0; i < ARRAY_SIZE(cms); i++)
		free(cms[i].data);
	free(content.data);
	free(sha384_digest.data);
	if (failures)
		return 1;
	puts("payload-mm Auth2 CMS verification tests: PASS");
	return 0;
}
