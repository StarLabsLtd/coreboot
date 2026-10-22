/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */
/* Copyright (c) 2026 Star Labs Systems. All rights reserved. */
/* UEFI capsule framing follows payload_mm_authenticate_image from the cached
 * coreboot capsule implementation d960d256e2c3c14ff4aa46d6443ba20a2fab77f7
 * (GPL-2.0-only). CMS and trust verification remain in that existing library.
 */

#include "capsule_image.h"
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <string.h>
#include "../../lib/payload_mm_crypto/crypto.h"

static const uint8_t pkcs7_guid[16] = {
	0x9d, 0xd2, 0xaf, 0x4a, 0xdf, 0x68, 0xee, 0x49,
	0x8a, 0xa9, 0x34, 0x7d, 0x37, 0x56, 0x65, 0xa7,
};

static bool range_valid(const void *data, size_t size)
{
	return data && (uintptr_t)data <= UINTPTR_MAX - size;
}

static bool overlap(const void *a, size_t a_size, const void *b, size_t b_size)
{
	return (uintptr_t)a < (uintptr_t)b + b_size &&
		(uintptr_t)b < (uintptr_t)a + a_size;
}

static bool equal(const uint8_t *a, const uint8_t *b, size_t size)
{
	uint8_t difference = 0;

	for (size_t i = 0; i < size; i++)
		difference |= a[i] ^ b[i];
	return difference == 0;
}

void smm_capsule_image_close(struct smm_capsule_image *image)
{
	if (image)
		mbedtls_platform_zeroize(image, sizeof(*image));
}

/* Receipts and both whole-image digests must describe the very same copies.
 * DMA changes during or after copying can cause rejection, not unsigned writes.
 */
static enum payload_mm_verify_status hash_image(void *context,
	enum payload_mm_hash_algorithm algorithm,
	uint8_t digest[PAYLOAD_MM_MAX_DIGEST_SIZE])
{
	struct smm_capsule_image *image = context;
	mbedtls_sha256_context transport, body256;
	mbedtls_sha512_context body512;
	uint8_t transport_digest[32];
	enum payload_mm_verify_status status = PAYLOAD_MM_VERIFY_INTERNAL;
	int result;

	if (algorithm != PAYLOAD_MM_HASH_SHA256 && algorithm != PAYLOAD_MM_HASH_SHA384 &&
	    algorithm != PAYLOAD_MM_HASH_SHA512)
		return PAYLOAD_MM_VERIFY_UNSUPPORTED;
	mbedtls_sha256_init(&transport);
	mbedtls_sha256_init(&body256);
	mbedtls_sha512_init(&body512);
	memset(digest, 0, PAYLOAD_MM_MAX_DIGEST_SIZE);
	result = mbedtls_sha256_starts(&transport, 0);
	if (!result) {
		if (algorithm == PAYLOAD_MM_HASH_SHA256)
			result = mbedtls_sha256_starts(&body256, 0);
		else
			result = mbedtls_sha512_starts(&body512,
				algorithm == PAYLOAD_MM_HASH_SHA384);
	}
	for (size_t offset = 0; !result && offset < image->size;
	     offset += SMM_CAPSULE_IMAGE_BLOCK_SIZE) {
		size_t size = image->size - offset;
		size_t start, end;

		if (size > sizeof(image->block))
			size = sizeof(image->block);
		memcpy(image->block, image->external + offset, size);
		if (!offset && memcmp(image->block, image->header, sizeof(image->header))) {
			status = PAYLOAD_MM_VERIFY_CHANGED;
			goto out;
		}
		if (payload_mm_sha256(image->block, size,
			image->receipts[offset / SMM_CAPSULE_IMAGE_BLOCK_SIZE]) !=
			PAYLOAD_MM_VERIFY_OK)
			goto out;
		/* The separately retained CMS must equal its corresponding copied bytes. */
		start = offset > sizeof(image->header) ? offset : sizeof(image->header);
		end = offset + size < image->payload_offset ? offset + size : image->payload_offset;
		if (start < end && memcmp(image->block + start - offset,
			image->cms + start - sizeof(image->header), end - start)) {
			status = PAYLOAD_MM_VERIFY_CHANGED;
			goto out;
		}
		result = mbedtls_sha256_update(&transport, image->block, size);
		start = offset > image->payload_offset ? offset : image->payload_offset;
		if (!result && start < offset + size) {
			if (algorithm == PAYLOAD_MM_HASH_SHA256)
				result = mbedtls_sha256_update(&body256,
					image->block + start - offset, offset + size - start);
			else
				result = mbedtls_sha512_update(&body512,
					image->block + start - offset, offset + size - start);
		}
	}
	if (!result) {
		if (algorithm == PAYLOAD_MM_HASH_SHA256) {
			result = mbedtls_sha256_update(&body256, image->header, 8);
			if (!result)
				result = mbedtls_sha256_finish(&body256, digest);
		} else {
			result = mbedtls_sha512_update(&body512, image->header, 8);
			if (!result)
				result = mbedtls_sha512_finish(&body512, digest);
		}
	}
	if (!result)
		result = mbedtls_sha256_finish(&transport, transport_digest);
	if (!result) {
		if (equal(transport_digest, image->expected_sha256, sizeof(transport_digest)))
			status = PAYLOAD_MM_VERIFY_OK;
		else
			status = PAYLOAD_MM_VERIFY_CHANGED;
	}
out:
	mbedtls_sha256_free(&transport);
	mbedtls_sha256_free(&body256);
	mbedtls_sha512_free(&body512);
	mbedtls_platform_zeroize(transport_digest, sizeof(transport_digest));
	mbedtls_platform_zeroize(image->block, sizeof(image->block));
	if (status != PAYLOAD_MM_VERIFY_OK)
		mbedtls_platform_zeroize(digest, PAYLOAD_MM_MAX_DIGEST_SIZE);
	return status;
}

enum payload_mm_verify_status smm_capsule_image_open(struct smm_capsule_image *image,
	const void *external, size_t size, const uint8_t expected_sha256[32],
	struct payload_mm_crypto_owner *crypto,
	const struct payload_mm_crypto_span *root_der)
{
	struct payload_mm_cms_verified_signer signer = {0};
	struct payload_mm_crypto_span cms;
	enum payload_mm_verify_status status = PAYLOAD_MM_VERIFY_MALFORMED;
	uint32_t certificate_size;

	if (!range_valid(image, sizeof(*image)))
		return PAYLOAD_MM_VERIFY_INVALID;
	if (!range_valid(external, size) ||
	    size <= sizeof(image->header) || size > SMM_CAPSULE_IMAGE_MAX_SIZE ||
	    !range_valid(expected_sha256, 32) || !range_valid(crypto, sizeof(*crypto)) ||
	    !range_valid(root_der, sizeof(*root_der)) ||
	    !range_valid(root_der->data, root_der->size) || !root_der->size ||
	    root_der->size > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE ||
	    overlap(image, sizeof(*image), external, size) ||
	    overlap(image, sizeof(*image), expected_sha256, 32) ||
	    overlap(image, sizeof(*image), crypto, sizeof(*crypto)) ||
	    overlap(image, sizeof(*image), root_der, sizeof(*root_der)) ||
	    overlap(image, sizeof(*image), root_der->data, root_der->size)) {
		smm_capsule_image_close(image);
		return PAYLOAD_MM_VERIFY_INVALID;
	}
	smm_capsule_image_close(image);
	image->external = external;
	image->size = size;
	memcpy(image->expected_sha256, expected_sha256, 32);
	memcpy(image->header, external, sizeof(image->header));
	certificate_size = (uint32_t)image->header[8] | (uint32_t)image->header[9] << 8 |
		(uint32_t)image->header[10] << 16 | (uint32_t)image->header[11] << 24;
	if (certificate_size <= 24 || certificate_size - 24 > sizeof(image->cms) ||
	    certificate_size >= size - 8 || image->header[12] != 0 || image->header[13] != 2 ||
	    image->header[14] != 0xf1 || image->header[15] != 0x0e ||
	    memcmp(image->header + 16, pkcs7_guid, sizeof(pkcs7_guid)))
		goto out;
	image->payload_offset = 8U + certificate_size;
	image->payload_size = size - image->payload_offset;
	cms = (struct payload_mm_crypto_span) { image->cms, certificate_size - 24U };
	memcpy(image->cms, (const uint8_t *)external + sizeof(image->header), cms.size);
	status = payload_mm_cms_verify_streamed_untrusted(crypto, &cms, hash_image, image,
		&signer);
	if (status == PAYLOAD_MM_VERIFY_OK)
		status = payload_mm_authvar_trust_anchor_verify(crypto, &cms, &signer, root_der);
	if (status == PAYLOAD_MM_VERIFY_OK)
		image->open = true;
out:
	mbedtls_platform_zeroize(&signer, sizeof(signer));
	if (status != PAYLOAD_MM_VERIFY_OK)
		smm_capsule_image_close(image);
	return status;
}

bool smm_capsule_image_read(struct smm_capsule_image *image, size_t offset,
	void *output, size_t size)
{
	uint8_t digest[32];
	size_t copied = 0;

	if (!image || !image->open || offset > image->size || size > image->size - offset ||
	    (size && (!range_valid(output, size) ||
	     overlap(image, sizeof(*image), output, size) ||
	     overlap(image->external, image->size, output, size))))
		return false;
	while (copied < size) {
		size_t position = offset + copied;
		size_t base = position / sizeof(image->block) * sizeof(image->block);
		size_t block_size = image->size - base;
		size_t part;

		if (block_size > sizeof(image->block))
			block_size = sizeof(image->block);
		if (!image->cached || image->cached_block != base) {
			image->cached = false;
			memcpy(image->block, image->external + base, block_size);
			if (payload_mm_sha256(image->block, block_size, digest) != PAYLOAD_MM_VERIFY_OK ||
			    !equal(digest, image->receipts[base / sizeof(image->block)], sizeof(digest))) {
				mbedtls_platform_zeroize(output, size);
				mbedtls_platform_zeroize(digest, sizeof(digest));
				smm_capsule_image_close(image);
				return false;
			}
			image->cached_block = base;
			image->cached = true;
		}
		part = block_size - (position - base);
		if (part > size - copied)
			part = size - copied;
		memcpy((uint8_t *)output + copied, image->block + position - base, part);
		copied += part;
	}
	mbedtls_platform_zeroize(digest, sizeof(digest));
	return true;
}
