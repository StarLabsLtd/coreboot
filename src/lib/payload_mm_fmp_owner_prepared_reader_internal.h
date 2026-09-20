/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_PAYLOAD_MM_FMP_OWNER_PREPARED_READER_INTERNAL_H
#define LIB_PAYLOAD_MM_FMP_OWNER_PREPARED_READER_INTERNAL_H

#include <commonlib/region.h>
#include <security/tpm/capsule_anchor_transition.h>

#include "payload_mm_fmp_owner_journal_internal.h"

#define PAYLOAD_MM_FMP_OWNER_PREPARED_READER_REVISION 1U
#define PAYLOAD_MM_FMP_OWNER_PREPARED_READER_CONTEXT_SIZE 128U

typedef enum cb_err payload_mm_fmp_owner_prepared_sha256_fn(
	const void *context, const void *buffer, size_t size,
	uint8_t digest[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE]);

struct payload_mm_fmp_owner_prepared_media {
	struct region_device state;
	const struct region_device *root_device;
	struct region_device root;
	struct region_device_ops ops;
};

struct payload_mm_fmp_owner_prepared_policy {
	u32 revision;
	u32 size;
	struct fmp_owner_layout layout;
	struct payload_mm_fmp_owner_prepared_media
		media[PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS];
	struct payload_mm_fmp_owner_journal_anchor current;
	u8 storage_domain[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE];
	payload_mm_fmp_owner_prepared_sha256_fn *sha256;
	size_t context_size;
	u8 context[PAYLOAD_MM_FMP_OWNER_PREPARED_READER_CONTEXT_SIZE] __aligned(8);
};

/*
 * The platform must zero-initialize this object and initialize it before any
 * payload or option ROM. The current anchor is an exact immutable snapshot
 * read by the pre-OS TPM owner through its acquired lifecycle token.
 */
struct payload_mm_fmp_owner_prepared_reader {
	struct payload_mm_fmp_owner_prepared_policy policy;
	struct payload_mm_fmp_owner_prepared_policy sealed_policy;
	u32 control;
	bool initialized;
	bool poisoned;
};

enum cb_err payload_mm_fmp_owner_prepared_reader_init(
	struct payload_mm_fmp_owner_prepared_reader *reader,
	const struct fmp_owner_layout *layout,
	const struct region_device state[PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS],
	const struct payload_mm_fmp_owner_journal_anchor *current,
	payload_mm_fmp_owner_prepared_sha256_fn *sha256,
	const void *context, size_t context_size);
enum cb_err payload_mm_fmp_owner_prepared_reader_prove(const void *context,
	const struct capsule_tpm_anchor_grant *grant);

#endif
