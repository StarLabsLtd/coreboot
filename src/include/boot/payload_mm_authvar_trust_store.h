/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_TRUST_STORE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_TRUST_STORE_H

#include <boot/payload_mm_authvar_route.h>
#include <boot/payload_mm_authvar_store.h>
#include <payload_mm_cms.h>

/*
 * Verify detached CMS and its current PK or KEK trust route in one protected
 * call. Consume only an immutable index produced by
 * payload_mm_authvar_store_scan() in the same transaction. Success is not
 * reusable mutation authority.
 */
enum payload_mm_verify_status payload_mm_authvar_trust_store_verify(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content, size_t content_count,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan);

#endif
