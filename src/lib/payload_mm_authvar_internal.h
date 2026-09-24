/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef PAYLOAD_MM_AUTHVAR_INTERNAL_H
#define PAYLOAD_MM_AUTHVAR_INTERNAL_H

#include <boot/payload_mm_authvar.h>

struct payload_mm_authvar_candidate_binding;
struct payload_mm_authvar_store_index;

enum payload_mm_authvar_mode_key {
	PAYLOAD_MM_AUTHVAR_MODE_KEY_PK,
	PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE,
	PAYLOAD_MM_AUTHVAR_MODE_KEY_VENDOR_KEYS_NV,
	PAYLOAD_MM_AUTHVAR_MODE_KEY_CUSTOM_MODE,
};

struct payload_mm_authvar_bundle_mutation;

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
	uint8_t volatile_modes);
const struct payload_mm_authvar_store_entry *payload_mm_authvar_mode_find(
	const struct payload_mm_authvar_store_index *index,
	enum payload_mm_authvar_mode_key key);
bool payload_mm_authvar_mode_value(
	const struct payload_mm_authvar_store_index *index,
	enum payload_mm_authvar_mode_key key, uint32_t attributes, uint8_t *value);
bool payload_mm_authvar_mode_key_matches(const uint8_t vendor_guid[16],
	const void *name, size_t name_size, enum payload_mm_authvar_mode_key key);
bool payload_mm_authvar_mode_mutation_key(
	struct payload_mm_authvar_bundle_mutation *mutation,
	enum payload_mm_authvar_mode_key key);
bool payload_mm_authvar_coordinator_source_modes(
	const struct payload_mm_authvar_store_index *index, bool at_runtime,
	bool sealed_modes_valid, uint8_t sealed_modes, uint8_t *source_modes);
bool payload_mm_authvar_coordinator_reconcile_modes(
	const struct payload_mm_authvar_store_index *index, bool at_runtime,
	uint8_t sealed_modes, uint8_t *source_modes);

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
