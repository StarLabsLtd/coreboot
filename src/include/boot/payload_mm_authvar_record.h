/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_RECORD_H
#define BOOT_PAYLOAD_MM_AUTHVAR_RECORD_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define PAYLOAD_MM_AUTHVAR_RECORD_MAX_DATA_SPANS 2U

struct payload_mm_authvar_record_descriptor {
	u8 vendor_guid[16];
	const void *name;
	size_t name_size;
	u32 attributes;
	u8 timestamp[16];
};

struct payload_mm_authvar_record_span {
	const void *data;
	size_t size;
};

enum payload_mm_authvar_record_timestamp {
	PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED,
	PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO,
};

bool payload_mm_authvar_record_layout(size_t name_size, size_t data_size,
				      size_t *record_size, size_t *data_offset);

/*
 * Encode one canonical erased-state record. The caller owns semantic policy
 * validation. TRUSTED_ZERO is reserved for EDK2-compatible internal records
 * which intentionally carry the all-zero EFI_TIME sentinel.
 */
bool payload_mm_authvar_record_encode(
	const struct payload_mm_authvar_record_descriptor *source,
	const struct payload_mm_authvar_record_span *spans, size_t span_count,
	enum payload_mm_authvar_record_timestamp timestamp_mode,
	void *record, size_t capacity, uint32_t *output_size);

#endif
