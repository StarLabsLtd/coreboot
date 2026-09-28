/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_X86_SMM_SAVE_STATE_H
#define CPU_X86_SMM_SAVE_STATE_H

#include <commonlib/bsd/cb_err.h>
#include <stddef.h>
#include <stdint.h>

struct smm_save_state_span {
	uintptr_t base;
	size_t size;
};

/*
 * Derive the architectural save-state span from its exclusive top and the
 * containing allocation. Reserved metadata, such as an STM PSD, is excluded.
 */
enum cb_err smm_save_state_native_span(uintptr_t top, size_t allocation_size,
	size_t reserved_size, struct smm_save_state_span *span);
enum cb_err smm_save_state_revision_at(
	const struct smm_save_state_span *span, size_t offset_from_top,
	uint32_t *revision);

#endif
