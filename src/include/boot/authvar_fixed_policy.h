/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_AUTHVAR_FIXED_POLICY_H
#define BOOT_AUTHVAR_FIXED_POLICY_H

#include <boot/payload_mm_authvar_format.h>
#include <stddef.h>
#include <stdint.h>

struct payload_mm_authvar_store_index;

struct authvar_fixed_certificate {
	struct payload_mm_authvar_span data;
	const uint8_t *owner_guid;
};

struct authvar_fixed_policy {
	const struct authvar_fixed_certificate *pk;
	size_t pk_count;
	const struct authvar_fixed_certificate *kek;
	size_t kek_count;
	const struct authvar_fixed_certificate *db;
	size_t db_count;
	struct payload_mm_authvar_span dbx_baseline;
	struct payload_mm_authvar_span dbx_append;
	struct payload_mm_authvar_span dbx_update;
};

enum authvar_fixed_key {
	AUTHVAR_FIXED_ORDINARY,
	AUTHVAR_FIXED_LOCKED,
	AUTHVAR_FIXED_REVOCATIONS,
};

/* Name is canonical UTF-16LE, including its terminating NUL. */
enum authvar_fixed_key authvar_fixed_classify(const uint8_t guid[16],
	const void *name, size_t name_size);

/* Policy assets and the store are immutable for the call; no pointers are retained. */
uint64_t authvar_fixed_validate(const struct payload_mm_authvar_store_index *index,
	const struct authvar_fixed_policy *policy);
uint64_t authvar_fixed_prepare_dbx_update(const struct payload_mm_authvar_store_index *index,
	const struct authvar_fixed_policy *policy, const uint8_t **data, size_t *size);

#endif
