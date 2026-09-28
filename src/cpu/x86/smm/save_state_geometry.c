/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_save_state.h>
#include <stdint.h>
#include <string.h>

enum cb_err smm_save_state_native_span(uintptr_t top, size_t allocation_size,
	size_t reserved_size, struct smm_save_state_span *span)
{
	size_t native_size;

	if (!span)
		return CB_ERR;
	*span = (struct smm_save_state_span) { 0 };
	if (allocation_size <= reserved_size)
		return CB_ERR;
	native_size = allocation_size - reserved_size;
	if (top <= native_size)
		return CB_ERR;
	*span = (struct smm_save_state_span) {
		.base = top - native_size,
		.size = native_size,
	};
	return CB_SUCCESS;
}

enum cb_err smm_save_state_revision_at(
	const struct smm_save_state_span *span, size_t offset_from_top,
	uint32_t *revision)
{
	if (revision)
		*revision = 0;
	if (!span || !revision || !span->base ||
	    span->base > ~(uintptr_t)0 - span->size ||
	    offset_from_top < sizeof(*revision) ||
	    span->size < offset_from_top)
		return CB_ERR;
	memcpy(revision, (const void *)(span->base + span->size - offset_from_top),
		sizeof(*revision));
	return CB_SUCCESS;
}
