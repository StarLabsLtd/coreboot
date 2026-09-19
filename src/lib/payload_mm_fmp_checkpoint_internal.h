/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_PAYLOAD_MM_FMP_CHECKPOINT_INTERNAL_H
#define LIB_PAYLOAD_MM_FMP_CHECKPOINT_INTERNAL_H

#include "payload_mm_fmp_owner_internal.h"

struct payload_mm_fmp_checkpoint_workspace {
	struct payload_mm_fmp_owner_record current;
	struct payload_mm_fmp_owner_record candidate;
	struct payload_mm_fmp_owner_record verified;
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_fmp_checkpoint_workspace) == 144,
	"Payload-MM FMP checkpoint workspace layout");
_Static_assert(_Alignof(struct payload_mm_fmp_checkpoint_workspace) == 8,
	"Payload-MM FMP checkpoint workspace alignment");

enum cb_err payload_mm_fmp_checkpoint_owner_bind(uint64_t broker_generation,
	struct payload_mm_fmp_checkpoint_workspace *trusted_workspace,
	payload_mm_authvar_protected_storage storage_is_protected, void *context);
#if ENV_TEST
const void *payload_mm_fmp_checkpoint_test_authority(size_t *size);
bool payload_mm_fmp_checkpoint_test_storage_overlaps(const void *buffer,
	size_t size);
#endif
enum cb_err payload_mm_fmp_checkpoint_commit(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version);

#endif
