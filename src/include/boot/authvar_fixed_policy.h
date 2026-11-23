/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_AUTHVAR_FIXED_POLICY_H
#define BOOT_AUTHVAR_FIXED_POLICY_H

#include <boot/payload_mm_authvar_format.h>
#include <stddef.h>
#include <stdint.h>

struct payload_mm_authvar_store_index;

struct authvar_fixed_policy {
	struct payload_mm_authvar_span factory_pk, factory_kek, factory_db;
	struct payload_mm_authvar_span dbx_baseline;
	struct payload_mm_authvar_span dbx_append;
	struct payload_mm_authvar_span dbx_update;
};

/* Count canonical single-certificate X.509 lists, up to maximum. Zero means invalid. */
size_t authvar_fixed_certificate_count(struct payload_mm_authvar_span asset, size_t maximum);
bool authvar_fixed_keys_valid(const struct authvar_fixed_policy *policy);
/* Append device revocations and the enable preference to a private copy of a proven factory-default store. */
uint64_t authvar_fixed_seed(const struct authvar_fixed_policy *policy, void *store, size_t size,
	size_t offset);

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
uint64_t authvar_fixed_preserve_dbx(const struct payload_mm_authvar_store_index *previous,
	const struct payload_mm_authvar_store_index *next);
uint64_t authvar_fixed_prepare_dbx_update(const struct payload_mm_authvar_store_index *index,
	const struct authvar_fixed_policy *policy, const uint8_t **data, size_t *size);

#endif
