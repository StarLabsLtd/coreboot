/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <string.h>

#include "native_capsule_source.h"

void ass_capsule_source_close(struct ass_capsule_source *source)
{
	mbedtls_platform_zeroize(source, sizeof(*source));
}

static enum payload_mm_verify_status hash_source(void *context,
	enum payload_mm_hash_algorithm algorithm,
	uint8_t digest[PAYLOAD_MM_MAX_DIGEST_SIZE])
{
	struct ass_capsule_source *source = context;
	mbedtls_sha256_context sha256;
	mbedtls_sha512_context sha512;
	const bool narrow = algorithm == PAYLOAD_MM_HASH_SHA256;
	int ret;

	if (!narrow && algorithm != PAYLOAD_MM_HASH_SHA384 &&
	    algorithm != PAYLOAD_MM_HASH_SHA512)
		return PAYLOAD_MM_VERIFY_UNSUPPORTED;
	mbedtls_sha256_init(&sha256);
	mbedtls_sha512_init(&sha512);
	ret = narrow ? mbedtls_sha256_starts(&sha256, 0) :
		mbedtls_sha512_starts(&sha512, algorithm == PAYLOAD_MM_HASH_SHA384);
	for (size_t offset = 0; !ret && offset < source->size; offset += sizeof(source->block)) {
		const size_t bytes = MIN(source->size - offset, sizeof(source->block));

		memcpy(source->block, source->external + offset, bytes);
		ret = mbedtls_sha256(source->block, bytes,
			source->receipts[offset / sizeof(source->block)], 0);
		if (!ret)
			ret = narrow ? mbedtls_sha256_update(&sha256, source->block, bytes) :
				mbedtls_sha512_update(&sha512, source->block, bytes);
	}
	if (!ret)
		ret = narrow ? mbedtls_sha256_finish(&sha256, digest) :
			mbedtls_sha512_finish(&sha512, digest);
	mbedtls_sha256_free(&sha256);
	mbedtls_sha512_free(&sha512);
	mbedtls_platform_zeroize(source->block, sizeof(source->block));
	return ret ? PAYLOAD_MM_VERIFY_INTERNAL : PAYLOAD_MM_VERIFY_OK;
}

enum payload_mm_verify_status ass_capsule_source_verify(struct ass_capsule_source *source,
	struct payload_mm_crypto_owner *crypto, const void *external, size_t size,
	const struct payload_mm_crypto_span *cms, const struct payload_mm_crypto_span *root)
{
	struct payload_mm_cms_verified_signer signer;
	enum payload_mm_verify_status status = PAYLOAD_MM_VERIFY_INVALID;

	if (!source)
		return status;
	ass_capsule_source_close(source);
	if (!crypto || !external || !size || (uintptr_t)external > UINTPTR_MAX - size ||
	    size > ASS_CAPSULE_SOURCE_BLOCKS * ASS_CAPSULE_SOURCE_BLOCK ||
	    !cms || !root)
		return status;
	source->external = external;
	source->size = size;
	status = payload_mm_cms_verify_streamed_untrusted(crypto, cms, hash_source,
		source, &signer);
	if (status == PAYLOAD_MM_VERIFY_OK)
		status = payload_mm_authvar_trust_anchor_verify(crypto, cms, &signer, root);
	if (status == PAYLOAD_MM_VERIFY_OK)
		source->authorized = true;
	else
		ass_capsule_source_close(source);
	return status;
}

bool ass_capsule_source_read(struct ass_capsule_source *source, size_t offset,
	void *destination, size_t size)
{
	uint8_t digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t *output = destination;

	if (!source || !source->authorized || !destination || offset > source->size ||
	    size > source->size - offset)
		return false;
	while (size) {
		const size_t index = offset / sizeof(source->block);
		const size_t base = index * sizeof(source->block);
		const size_t length = MIN(source->size - base, sizeof(source->block));
		const size_t prefix = offset - base;
		const size_t part = MIN(length - prefix, size);

		memcpy(source->block, source->external + base, length);
		if (mbedtls_sha256(source->block, length, digest, 0) ||
		    memcmp(digest, source->receipts[index], sizeof(digest))) {
			ass_capsule_source_close(source);
			return false;
		}
		memcpy(output, source->block + prefix, part);
		output += part;
		offset += part;
		size -= part;
	}
	mbedtls_platform_zeroize(source->block, sizeof(source->block));
	return true;
}
