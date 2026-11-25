/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EFI_VARIABLE_SERVICE_NATIVE_CAPSULE_SOURCE_H
#define EFI_VARIABLE_SERVICE_NATIVE_CAPSULE_SOURCE_H

#include <payload_mm_cms.h>

#define ASS_CAPSULE_SOURCE_BLOCK (64U * 1024U)
#define ASS_CAPSULE_SOURCE_BLOCKS 513U

struct ass_capsule_source {
	const uint8_t *external;
	size_t size;
	uint8_t block[ASS_CAPSULE_SOURCE_BLOCK];
	uint8_t receipts[ASS_CAPSULE_SOURCE_BLOCKS][PAYLOAD_MM_SHA256_SIZE];
	bool authorized;
};

/* The SMM caller validates the external range and provides a private CMS copy. */
enum payload_mm_verify_status ass_capsule_source_verify(struct ass_capsule_source *source,
	struct payload_mm_crypto_owner *crypto, const void *external, size_t size,
	const struct payload_mm_crypto_span *cms, const struct payload_mm_crypto_span *root);
bool ass_capsule_source_read(struct ass_capsule_source *source, size_t offset,
	void *destination, size_t size);
void ass_capsule_source_close(struct ass_capsule_source *source);

#endif
