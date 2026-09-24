/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRIVATE_TRUST_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRIVATE_TRUST_H

#include <boot/payload_mm_authvar_route.h>
#include <boot/payload_mm_authvar_store.h>
#include <payload_mm_cms.h>

struct payload_mm_authvar_private_trust_decision {
	enum payload_mm_authvar_authority accepted_authority;
	size_t new_binding_size;
	uint8_t new_binding[PAYLOAD_MM_MAX_DIGEST_SIZE];
};

/*
 * Verify detached CMS and one EDK2 26.09 private-variable signer route in one
 * protected call. content is the exact five-span authority serialization.
 * Resolve the persistent certdb only from the immutable scanner index. The
 * decision is published only after verification and protected cleanup. A new
 * binding is present only for a non-empty NEW_PRIVATE_SIGNER payload; this
 * function never mutates certdb or media.
 */
enum payload_mm_verify_status payload_mm_authvar_private_trust_verify(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content, size_t content_count,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan,
	struct payload_mm_authvar_private_trust_decision *decision);

#endif
