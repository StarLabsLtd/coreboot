/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_VIEW_H
#define BOOT_PAYLOAD_MM_AUTHVAR_VIEW_H

#include <boot/payload_mm_authvar_store_semantics.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

struct payload_mm_authvar_view {
	const struct payload_mm_authvar_store_index *persistent;
	uint8_t volatile_modes;
	uint8_t at_runtime;
	uint8_t reserved[6];
};

/* A borrowed immutable variable value, never a synthetic store entry. */
struct payload_mm_authvar_view_value {
	const uint8_t *vendor_guid;
	const void *name;
	const void *data;
	uint32_t name_size;
	uint32_t data_size;
	uint32_t attributes;
};

enum cb_err payload_mm_authvar_view_init(struct payload_mm_authvar_view *view,
	const struct payload_mm_authvar_store_index *persistent,
	uint8_t volatile_modes, bool at_runtime);
uint64_t payload_mm_authvar_view_get(const struct payload_mm_authvar_view *view,
	const uint8_t vendor_guid[16], const void *name, size_t name_size,
	uint32_t data_capacity, struct payload_mm_authvar_view_value *value);
uint64_t payload_mm_authvar_view_get_next(
	const struct payload_mm_authvar_view *view,
	const uint8_t vendor_guid[16], const void *name, size_t name_size,
	uint32_t name_capacity, struct payload_mm_authvar_view_value *value);
uint64_t payload_mm_authvar_view_query(const struct payload_mm_authvar_view *view,
	const struct payload_mm_authvar_store_policy *policy, uint32_t attributes,
	struct payload_mm_authvar_query_result *result);

/* Shared with SET policy so the synthetic identities cannot drift. */
bool payload_mm_authvar_view_key_reserved(const uint8_t vendor_guid[16],
	const void *name, size_t name_size);

#endif
