/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_PAYLOAD_MM_FMP_CHECKPOINT_INTERNAL_H
#define LIB_PAYLOAD_MM_FMP_CHECKPOINT_INTERNAL_H

#include "payload_mm_fmp_state_internal.h"

#define PAYLOAD_MM_FMP_CHECKPOINT_BACKEND_REVISION 1U
#define PAYLOAD_MM_FMP_CHECKPOINT_CONTEXT_SIZE 128U

struct payload_mm_fmp_checkpoint_record {
	uint64_t sequence;
	uint32_t attributes;
	uint32_t data_size;
	uint8_t data[PAYLOAD_MM_FMP_STATE_WIRE_SIZE];
	uint32_t reserved;
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_fmp_checkpoint_record) == 40,
	"Payload-MM FMP checkpoint record layout");
_Static_assert(_Alignof(struct payload_mm_fmp_checkpoint_record) == 8,
	"Payload-MM FMP checkpoint record alignment");

typedef enum cb_err payload_mm_fmp_checkpoint_read_fn(void *context,
	const struct payload_mm_fmp_state_identity *identity,
	struct payload_mm_fmp_checkpoint_record *record);
typedef enum cb_err payload_mm_fmp_checkpoint_commit_fn(void *context,
	const struct payload_mm_fmp_state_identity *identity,
	const struct payload_mm_fmp_checkpoint_record *current,
	const struct payload_mm_fmp_checkpoint_record *candidate);

struct payload_mm_fmp_checkpoint_backend {
	uint32_t revision;
	uint32_t size;
	uint64_t broker_generation;
	payload_mm_fmp_checkpoint_read_fn *read;
	payload_mm_fmp_checkpoint_commit_fn *commit;
	void *context;
	size_t context_size;
};

enum cb_err payload_mm_fmp_checkpoint_backend_install(
	const struct payload_mm_fmp_checkpoint_backend *trusted_backend,
	payload_mm_authvar_protected_storage storage_is_protected, void *context);
enum cb_err payload_mm_fmp_checkpoint_commit(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version);

#endif
