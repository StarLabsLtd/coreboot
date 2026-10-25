/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_AUTHVAR_OWNER_H
#define BOOT_AUTHVAR_OWNER_H

#include <boot/authvar_fixed_policy.h>
#include <boot/payload_mm_authvar_candidate.h>
#include <boot/payload_mm_authvar_view.h>
#include <boot/payload_mm_authvar_writer.h>

#define AUTHVAR_OWNER_MAX_STORE_SIZE (512U * 1024U)

typedef uint64_t authvar_owner_commit_fn(void *context, const void *store, size_t size,
	const struct payload_mm_authvar_write_plan *append, const void *record);

struct authvar_owner {
	struct payload_mm_crypto_owner crypto;
	struct payload_mm_authvar_store_index index;
	struct payload_mm_authvar_store_index volatile_index;
	struct payload_mm_authvar_store_limits limits;
	struct payload_mm_authvar_write_policy policy;
	size_t storage_offset, entries_offset, copies_offset;
	authvar_owner_commit_fn *commit;
	void *commit_context;
	uint64_t generation;
	uint8_t active, volatile_active, modes;
	bool ready, runtime, failed, restart_required;
	bool secure_boot, fixed_keys;
};

/* Actual initialization observations, consumed by the native diagnostic adapter. */
struct authvar_owner_initialization {
	uint32_t interrupted_offset;
	uint64_t repair_status;
	bool repair_attempted;
	size_t dbx_update_size;
	uint64_t dbx_update_status;
};

/* Caller owns the allocation and serializes every operation, including reads. */
size_t authvar_owner_workspace_size(size_t store_size);
uint64_t authvar_owner_init(struct authvar_owner *owner,
	const void *store, size_t size, authvar_owner_commit_fn *commit, void *context,
	void *storage, size_t storage_size, bool secure_boot,
	const struct authvar_fixed_policy *fixed, struct authvar_owner_initialization *report);
uint64_t authvar_owner_get(struct authvar_owner *owner,
	const uint16_t *name, const void *guid, uint32_t *attributes,
	size_t *data_size, void *data);
uint64_t authvar_owner_next(struct authvar_owner *owner,
	size_t *name_size, uint16_t *name, void *guid);
/*
 * Fixed assets are borrowed for this call and must match the selected owner policy.
 * reclaimed_records is SIZE_MAX unless an ordinary reclaim was attempted.
 */
uint64_t authvar_owner_set(struct authvar_owner *owner,
	const uint16_t *name, const void *guid, uint32_t attributes, size_t data_size,
	const void *data, bool trusted_presence, const struct authvar_fixed_policy *fixed,
	size_t *reclaimed_records);
uint64_t authvar_owner_delete_pk(struct authvar_owner *owner);
uint64_t authvar_owner_confirmed_key(struct authvar_owner *owner,
	uint32_t key, uint32_t mutation, const void *data, size_t size);
uint64_t authvar_owner_query(struct authvar_owner *owner,
	uint32_t attributes, uint64_t *maximum_storage, uint64_t *remaining_storage,
	uint64_t *maximum_variable);
uint64_t authvar_owner_classify(struct authvar_owner *owner,
	const uint16_t *name, const void *guid,
	struct payload_mm_authvar_key_classification *classification);
uint64_t authvar_owner_ready(struct authvar_owner *owner,
	const struct authvar_fixed_policy *fixed);
void authvar_owner_enter_runtime(struct authvar_owner *owner);
/* Borrowed view expires on mutation; consume or copy it under the owner's lock. */
uint64_t authvar_owner_view(struct authvar_owner *owner,
	struct payload_mm_authvar_view *view);

#endif
