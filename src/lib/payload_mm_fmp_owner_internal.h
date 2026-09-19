/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_PAYLOAD_MM_FMP_OWNER_INTERNAL_H
#define LIB_PAYLOAD_MM_FMP_OWNER_INTERNAL_H

#include "payload_mm_fmp_state_internal.h"

#define PAYLOAD_MM_FMP_OWNER_REVISION 1U
#define PAYLOAD_MM_FMP_OWNER_CONTEXT_SIZE 128U

struct payload_mm_fmp_owner_record {
	uint64_t sequence;
	uint32_t present;
	uint32_t attributes;
	uint32_t data_size;
	uint32_t reserved;
	uint8_t data[PAYLOAD_MM_FMP_STATE_WIRE_SIZE];
	uint32_t reserved2;
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_fmp_owner_record) == 48,
	"Payload-MM FMP owner record layout");
_Static_assert(_Alignof(struct payload_mm_fmp_owner_record) == 8,
	"Payload-MM FMP owner record alignment");

typedef enum cb_err payload_mm_fmp_owner_read_fn(const void *context,
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	struct payload_mm_fmp_owner_record *record);
/* Commit status is advisory; a fresh read decides the authoritative outcome. */
typedef enum cb_err payload_mm_fmp_owner_commit_fn(const void *context,
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate);

struct payload_mm_fmp_owner_backend {
	uint32_t revision;
	uint32_t size;
	payload_mm_fmp_owner_read_fn *read;
	payload_mm_fmp_owner_commit_fn *commit;
	const void *context;
	size_t context_size;
};

bool payload_mm_fmp_owner_record_valid(uint32_t key,
	const struct payload_mm_fmp_owner_record *record);

enum cb_err payload_mm_fmp_owner_install(
	const struct payload_mm_fmp_owner_backend *trusted_backend,
	payload_mm_authvar_protected_storage storage_is_protected, void *context);
bool payload_mm_fmp_owner_ready(void);
bool payload_mm_fmp_owner_storage_overlaps(const void *buffer, size_t size);
bool payload_mm_fmp_owner_buffer_available(const void *buffer, size_t size);
enum cb_err payload_mm_fmp_owner_read(uint32_t key,
	struct payload_mm_fmp_owner_record *record);
enum cb_err payload_mm_fmp_owner_commit_state(
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate);
enum cb_err payload_mm_fmp_owner_remove_legacy(uint32_t key,
	const struct payload_mm_fmp_owner_record *current);

#endif
