/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef PAYLOAD_MM_AUTHVAR_INTERNAL_H
#define PAYLOAD_MM_AUTHVAR_INTERNAL_H

#include <boot/payload_mm_authvar.h>

struct payload_mm_authvar_candidate_binding;
struct payload_mm_authvar_store_index;

bool payload_mm_authvar_range_end(uint64_t base, uint64_t size, uint64_t *end);
bool payload_mm_authvar_range_within(uint64_t base, uint64_t size,
	uint64_t outer_base, uint64_t outer_size);
bool payload_mm_authvar_contract_valid(
	const struct payload_mm_authvar_contract *contract);
bool payload_mm_authvar_authority_ready(void);
bool payload_mm_authvar_authority_snapshot(
	struct payload_mm_authvar_contract *contract);
bool payload_mm_authvar_smram_buffer(const void *buffer, size_t size);
bool payload_mm_authvar_buffers_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size);
/* Independently bind the packed volatile projection to source and candidate. */
bool payload_mm_authvar_candidate_projection_valid(
	const struct payload_mm_authvar_store_index *source,
	const struct payload_mm_authvar_store_index *candidate,
	const struct payload_mm_authvar_candidate_binding *binding,
	u8 volatile_modes);

/*
 * The executor brackets policy callbacks; media rejects every public entry
 * while this scope is active, including entries without backend I/O. The scope
 * capability is not a sandbox against arbitrary same-address-space access.
 */
struct payload_mm_authvar_media_provider_scope {
	uint64_t cookie;
	uint64_t check;
};

bool payload_mm_authvar_media_provider_enter(
	struct payload_mm_authvar_media_provider_scope *scope);
bool payload_mm_authvar_media_provider_leave(
	struct payload_mm_authvar_media_provider_scope *scope);
bool payload_mm_authvar_media_provider_violated(void);

#endif
