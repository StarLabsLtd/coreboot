/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_PAYLOAD_MM_FMP_OWNER_AUTHVAR_INTERNAL_H
#define LIB_PAYLOAD_MM_FMP_OWNER_AUTHVAR_INTERNAL_H

#include "payload_mm_fmp_owner_internal.h"

/* Seal policy-derived identities before the FMP state authority closes. */
enum cb_err payload_mm_fmp_owner_authvar_identity_install(
	payload_mm_authvar_protected_storage storage_is_protected, void *context);
enum cb_err payload_mm_fmp_owner_authvar_identity(uint32_t key,
	struct payload_mm_fmp_state_identity *identity);
bool payload_mm_fmp_owner_authvar_storage_overlaps(const void *buffer,
	size_t size);
enum payload_mm_fmp_owner_authvar_reservation {
	PAYLOAD_MM_FMP_OWNER_AUTHVAR_AUTHORITY_INVALID = 0,
	PAYLOAD_MM_FMP_OWNER_AUTHVAR_NOT_RESERVED,
	PAYLOAD_MM_FMP_OWNER_AUTHVAR_RESERVED,
};
enum payload_mm_fmp_owner_authvar_reservation
payload_mm_fmp_owner_authvar_reservation(const uint8_t vendor_guid[16],
	const void *name, size_t name_size);
#if ENV_TEST
const void *payload_mm_fmp_owner_authvar_test_storage(size_t *size);
void payload_mm_fmp_owner_authvar_test_corrupt_identity(bool sealed);
void payload_mm_fmp_owner_authvar_test_corrupt_control(bool sealed);
#endif

#endif
