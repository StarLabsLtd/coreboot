/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_SIGNATURE_DB_H
#define BOOT_PAYLOAD_MM_AUTHVAR_SIGNATURE_DB_H

#include <payload_mm_cms.h>

#define PAYLOAD_MM_AUTHVAR_SIGNATURE_DB_MAX_X509 64U

enum payload_mm_authvar_signature_db_profile {
	PAYLOAD_MM_AUTHVAR_SIGNATURE_DB = 0,
	PAYLOAD_MM_AUTHVAR_PLATFORM_KEY,
};

struct payload_mm_authvar_signature_db_stats {
	uint32_t list_count;
	uint32_t signature_count;
	uint32_t x509_count;
};

/*
 * Validate one immutable protected snapshot against the EDK2 26.09 supported
 * signature-type table. Every X.509 entry must contain one exact RSA DER
 * certificate. A native PK is exactly one list containing one X.509 entry.
 */
enum payload_mm_verify_status payload_mm_authvar_signature_db_validate(
	struct payload_mm_crypto_owner *owner, const void *data, size_t size,
	enum payload_mm_authvar_signature_db_profile profile,
	uint32_t maximum_x509,
	struct payload_mm_authvar_signature_db_stats *stats);

/*
 * Emit only append entries absent from current. Equality is the complete
 * EFI_SIGNATURE_DATA (owner and data), and is considered only between lists
 * with equal type and stride. Inputs and output must be disjoint protected
 * ranges. output_size and output bytes are changed only on success.
 */
enum payload_mm_verify_status payload_mm_authvar_signature_db_filter_append(
	struct payload_mm_crypto_owner *owner,
	const void *current, size_t current_size,
	const void *append, size_t append_size,
	void *output, size_t output_capacity, size_t *output_size);

#endif
