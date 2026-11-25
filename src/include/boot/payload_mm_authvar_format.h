/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_FORMAT_H
#define BOOT_PAYLOAD_MM_AUTHVAR_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum payload_mm_authvar_format_result {
	PAYLOAD_MM_AUTHVAR_FORMAT_OK = 0,
	PAYLOAD_MM_AUTHVAR_FORMAT_DONE = 1,
	PAYLOAD_MM_AUTHVAR_FORMAT_MALFORMED = 2,
};

struct payload_mm_authvar_span {
	const uint8_t *data;
	size_t size;
};

struct payload_mm_authvar_auth2_view {
	uint8_t timestamp[16];
	uint16_t certificate_revision;
	uint16_t certificate_type;
	uint8_t certificate_guid[16];
	struct payload_mm_authvar_span pkcs7;
	struct payload_mm_authvar_span payload;
};

/*
 * Decode only the bounded EFI_VARIABLE_AUTHENTICATION_2 envelope. EDK2 does
 * not require a particular WIN_CERTIFICATE revision, so neither does this
 * parser. No digest, signature, trust or replay decision is made here.
 */
enum payload_mm_authvar_format_result payload_mm_authvar_auth2_parse(
	const void *data, size_t size, struct payload_mm_authvar_auth2_view *view);

/* Match EDK2's signed-path reserved-field and certificate metadata checks. */
bool payload_mm_authvar_auth2_metadata_valid(
	const struct payload_mm_authvar_auth2_view *view);

/*
 * Require the real calendar accepted by the native store scanner, or its
 * all-zero initialization sentinel. A future authority must apply this before
 * authorizing a write so it cannot create a store the next scan rejects.
 */
bool payload_mm_authvar_timestamp_store_valid(const uint8_t timestamp[16]);

struct payload_mm_authvar_signature_list_cursor {
	const uint8_t *next;
	size_t remaining;
};

struct payload_mm_authvar_signature_list_view {
	uint8_t type_guid[16];
	struct payload_mm_authvar_span header;
	struct payload_mm_authvar_span signatures;
	uint32_t signature_size;
	uint32_t signature_count;
};

struct payload_mm_authvar_signature_view {
	uint8_t owner_guid[16];
	struct payload_mm_authvar_span data;
};

bool payload_mm_authvar_signature_list_begin(
	struct payload_mm_authvar_signature_list_cursor *cursor,
	const void *data, size_t size);
enum payload_mm_authvar_format_result payload_mm_authvar_signature_list_next(
	struct payload_mm_authvar_signature_list_cursor *cursor,
	struct payload_mm_authvar_signature_list_view *view);
bool payload_mm_authvar_signature_at(
	const struct payload_mm_authvar_signature_list_view *list, uint32_t index,
	struct payload_mm_authvar_signature_view *signature);

#endif
