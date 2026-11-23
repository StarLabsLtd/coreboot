/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRIVATE_TRUST_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRIVATE_TRUST_H

#include <boot/payload_mm_authvar_authority.h>

/*
 * Verify detached CMS and one EDK2 26.09 private-variable signer route in one
 * serialized call. content is the exact five-span authority serialization.
 * Resolve certdb or certdbv from the immutable scanner index according to the
 * request's actual NON_VOLATILE attribute. The caller owns snapshot isolation.
 * The decision is published only after verification and workspace cleanup. A new
 * binding is present only for a non-empty NEW_PRIVATE_SIGNER payload; this
 * function never mutates certdb or media.
 */
enum payload_mm_verify_status payload_mm_authvar_private_trust_verify(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content, size_t content_count,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan,
	struct payload_mm_authvar_authority_verification *verification);

#endif
