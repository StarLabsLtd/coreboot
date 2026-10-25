/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */
/* Copyright (c) 2026 Star Labs Systems. All rights reserved. */
#ifndef DRIVERS_SMMSTORE_CAPSULE_IMAGE_H
#define DRIVERS_SMMSTORE_CAPSULE_IMAGE_H

#include <payload_mm_cms.h>

#define SMM_CAPSULE_IMAGE_BLOCK_SIZE (64U * 1024U)
#define SMM_CAPSULE_IMAGE_MAX_SIZE (CONFIG_ROM_SIZE + 1024U * 1024U)
#define SMM_CAPSULE_IMAGE_BLOCKS \
	((SMM_CAPSULE_IMAGE_MAX_SIZE + SMM_CAPSULE_IMAGE_BLOCK_SIZE - 1U) / \
	 SMM_CAPSULE_IMAGE_BLOCK_SIZE)

/* Caller-owned SMRAM storage; all operations share the SMM owner's lock. */
struct smm_capsule_image {
	size_t payload_offset, payload_size;
	const uint8_t *external;
	size_t size;
	uint8_t header[32];
	uint8_t expected_sha256[PAYLOAD_MM_SHA256_SIZE];
	uint8_t cms[PAYLOAD_MM_MAX_CMS_SIZE];
	uint8_t receipts[SMM_CAPSULE_IMAGE_BLOCKS][PAYLOAD_MM_SHA256_SIZE];
	uint8_t block[SMM_CAPSULE_IMAGE_BLOCK_SIZE];
	size_t cached_block;
	bool cached, open;
};

/* The adapter validates external against its admitted staging range and SMRAM;
 * root_der is the compiled public trust anchor, never a request field.
 */
enum payload_mm_verify_status smm_capsule_image_open(struct smm_capsule_image *image,
	const void *external, size_t size, const uint8_t expected_sha256[32],
	struct payload_mm_crypto_owner *crypto,
	const struct payload_mm_crypto_span *root_der);
/* Offsets are relative to the complete authentication envelope. */
bool smm_capsule_image_read(struct smm_capsule_image *image, size_t offset,
	void *output, size_t size);
void smm_capsule_image_close(struct smm_capsule_image *image);

#endif
